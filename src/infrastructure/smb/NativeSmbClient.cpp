#include "smb/NativeSmbClient.h"

#include "DirectTcpTransport.h"
#include "NativeSmbConnector.h"
#include "NtlmV2TokenProvider.h"
#include "smb/NativeSmbErrorMapper.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QSysInfo>

#include <algorithm>
#include <memory>

namespace smb::infrastructure {
namespace {

constexpr std::uint64_t kUnixEpochAsFiletime = 116444736000000000ULL;
constexpr std::uint32_t kChunkSize = 64 * 1024;

smb::core::AppError localIoError(const QString &details) {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::LocalIoError,
                                       smb::core::ErrorCategory::Transfer,
                                       details, false);
}

smb::core::AppError appError(const smb::native_smb::ProtocolError &error,
                             const smb::core::LogSanitizer &baseSanitizer,
                             const smb::core::CredentialSecret *secret) {
  auto sanitizer = baseSanitizer;
  if (secret != nullptr && !secret->bytes.isEmpty()) {
    sanitizer.addSecretValue(QString::fromUtf8(secret->bytes));
  }
  return makeNativeSmbError(error, sanitizer);
}

smb::core::Result<bool> cancelledFailure() {
  return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
      smb::core::ErrorCode::OperationCancelled,
      smb::core::ErrorCategory::Transfer,
      QObject::tr("Operation was cancelled."), false));
}

bool isCancelled(const smb::core::OperationContext &context) {
  return context.cancellationToken != nullptr &&
         context.cancellationToken->isCancellationRequested();
}

smb::native_smb::AuthMode toNativeAuthMode(smb::core::AuthType authType) {
  switch (authType) {
  case smb::core::AuthType::Password:
    return smb::native_smb::AuthMode::Password;
  case smb::core::AuthType::Guest:
    return smb::native_smb::AuthMode::Guest;
  case smb::core::AuthType::Anonymous:
    return smb::native_smb::AuthMode::Anonymous;
  case smb::core::AuthType::CurrentUser:
    return smb::native_smb::AuthMode::CurrentUser;
  }
  return smb::native_smb::AuthMode::Password;
}

smb::native_smb::SecurityPolicy signingPolicyForAuthType(
    smb::core::AuthType /*authType*/) {
  return smb::native_smb::SecurityPolicy::Preferred;
}

smb::native_smb::DirectTcpEndpoint endpointForServer(const QString &server);

smb::native_smb::ConnectionConfig nativeConfig(
    const smb::core::Connection &connection, int timeoutSeconds) {
  smb::native_smb::ConnectionConfig config;
  config.server = endpointForServer(connection.server).host;
  config.share = connection.share.toStdString();
  config.normalizedUri = connection.normalizedUri.toStdString();
  config.domain = connection.domain.toStdString();
  config.username = connection.username.toStdString();
  config.authMode = toNativeAuthMode(connection.authType);
  config.signing = signingPolicyForAuthType(connection.authType);
  config.encryption = smb::native_smb::SecurityPolicy::Preferred;
  config.timeout = std::chrono::seconds(std::max(1, timeoutSeconds));
  return config;
}

smb::native_smb::SecretBuffer nativeSecret(
    const smb::core::CredentialSecret *secret) {
  if (secret == nullptr || secret->bytes.isEmpty()) {
    return smb::native_smb::SecretBuffer();
  }
  const auto *begin =
      reinterpret_cast<const std::uint8_t *>(secret->bytes.constData());
  return smb::native_smb::SecretBuffer(
      smb::native_smb::ByteVector(begin, begin + secret->bytes.size()));
}

smb::native_smb::DirectTcpEndpoint endpointForServer(const QString &server) {
  smb::native_smb::DirectTcpEndpoint endpoint;
  auto host = server.trimmed();

  if (host.startsWith(QLatin1Char('['))) {
    const auto closingBracket = host.indexOf(QLatin1Char(']'));
    if (closingBracket > 0 &&
        host.mid(closingBracket + 1).startsWith(QLatin1Char(':'))) {
      bool ok = false;
      const auto port = host.mid(closingBracket + 2).toUShort(&ok);
      if (ok && port > 0) {
        endpoint.host = host.mid(1, closingBracket - 1).toStdString();
        endpoint.port = port;
        return endpoint;
      }
    }
  }

  const auto colon = host.lastIndexOf(QLatin1Char(':'));
  if (colon > 0 && host.indexOf(QLatin1Char(':')) == colon) {
    bool ok = false;
    const auto port = host.mid(colon + 1).toUShort(&ok);
    if (ok && port > 0) {
      endpoint.host = host.left(colon).toStdString();
      endpoint.port = port;
      return endpoint;
    }
  }

  endpoint.host = host.toStdString();
  return endpoint;
}

smb::native_smb::OperationContext nativeContext(
    const smb::core::OperationContext &context, int timeoutSeconds) {
  smb::native_smb::OperationContext native;
  native.timeout = std::chrono::seconds(std::max(1, timeoutSeconds));
  if (context.cancellationToken != nullptr) {
    native.cancellationCallback = [&context]() {
      return context.cancellationToken != nullptr &&
             context.cancellationToken->isCancellationRequested();
    };
  }
  if (context.progressCallback) {
    native.progressCallback =
        [callback = context.progressCallback](
            const smb::native_smb::TransferProgress &progress) {
          callback(smb::core::TransferProgress{
              static_cast<qint64>(progress.bytesTransferred),
              static_cast<qint64>(progress.totalBytes)});
        };
  }
  return native;
}

QString normalizeRemotePath(const QString &remotePath) {
  auto normalized = remotePath.trimmed();
  while (normalized.startsWith(QLatin1Char('/')) ||
         normalized.startsWith(QLatin1Char('\\'))) {
    normalized.remove(0, 1);
  }
  normalized.replace(QLatin1Char('/'), QLatin1Char('\\'));
  return normalized;
}

QString childPath(const QString &parent, const QString &name) {
  const auto normalizedParent = normalizeRemotePath(parent);
  if (normalizedParent.isEmpty()) {
    return name;
  }
  return normalizedParent + QLatin1Char('\\') + name;
}

bool equalsCaseInsensitive(const QString &left, const QString &right) {
  return left.compare(right, Qt::CaseInsensitive) == 0;
}

bool sameRemoteTree(const smb::core::Connection &left,
                    const smb::core::Connection &right) {
  if (!left.id.isEmpty() && left.id == right.id) {
    return true;
  }

  const auto sameAddress =
      !left.server.isEmpty() && !left.share.isEmpty() &&
      equalsCaseInsensitive(left.server, right.server) &&
      equalsCaseInsensitive(left.share, right.share);
  const auto sameUri =
      !left.normalizedUri.isEmpty() &&
      equalsCaseInsensitive(left.normalizedUri, right.normalizedUri);
  if (!sameAddress && !sameUri) {
    return false;
  }

  return equalsCaseInsensitive(left.domain, right.domain) &&
         equalsCaseInsensitive(left.username, right.username) &&
         left.authType == right.authType;
}

QDateTime filetimeToDateTime(std::uint64_t filetime) {
  if (filetime <= kUnixEpochAsFiletime) {
    return {};
  }
  const auto msecs =
      static_cast<qint64>((filetime - kUnixEpochAsFiletime) / 10000);
  return QDateTime::fromMSecsSinceEpoch(msecs, Qt::UTC).toLocalTime();
}

QString attributesString(std::uint32_t attributes) {
  return QStringLiteral("0x%1").arg(attributes, 8, 16, QLatin1Char('0'));
}

smb::core::RemoteFileEntry toCoreEntry(
    const smb::native_smb::NativeRemoteEntry &entry, const QString &parentPath) {
  smb::core::RemoteFileEntry result;
  result.name = QString::fromStdString(entry.name);
  result.remotePath = childPath(parentPath, result.name);
  result.type = entry.reparsePoint
                    ? smb::core::RemoteFileType::Symlink
                    : (entry.directory ? smb::core::RemoteFileType::Directory
                                       : smb::core::RemoteFileType::File);
  result.size = static_cast<qint64>(entry.size);
  result.modifiedAt = filetimeToDateTime(entry.lastWriteTime);
  result.attributes = attributesString(entry.attributes);
  result.isHidden = result.name.startsWith(QLatin1Char('.'));
  return result;
}

smb::native_smb::DecodeResult<smb::native_smb::NativeSmbConnectedState>
openNativeConnection(const smb::core::Connection &connection,
                     const smb::core::CredentialSecret *secret,
                     const smb::core::OperationContext &context,
                     int timeoutSeconds) {
  if (isCancelled(context)) {
    return smb::native_smb::DecodeResult<
        smb::native_smb::NativeSmbConnectedState>::failure(
        smb::native_smb::ErrorCode::Cancelled, "Operation was cancelled.");
  }

  auto endpoint = endpointForServer(connection.server);
  auto transport =
      std::make_unique<smb::native_smb::DirectTcpTransport>(endpoint);

  smb::native_smb::NtlmV2TokenProviderOptions tokenOptions;
  tokenOptions.workstation = QSysInfo::machineHostName().toStdString();
  smb::native_smb::NtlmV2TokenProvider tokenProvider(nativeSecret(secret),
                                                     tokenOptions);

  smb::native_smb::NativeSmbConnectorOptions options;
  options.config = nativeConfig(connection, timeoutSeconds);
  options.negotiateOptions.dialects = {smb::native_smb::Dialect::Smb202,
                                       smb::native_smb::Dialect::Smb210,
                                       smb::native_smb::Dialect::Smb300,
                                       smb::native_smb::Dialect::Smb302};

  const smb::native_smb::NativeSmbConnector connector;
  return connector.connect(std::move(transport), tokenProvider, options,
                           nativeContext(context, timeoutSeconds));
}

} // namespace

NativeSmbClient::NativeSmbClient(int timeoutSeconds,
                                 smb::core::LogSanitizer sanitizer)
    : m_timeoutSeconds(std::max(1, timeoutSeconds)),
      m_sanitizer(std::move(sanitizer)) {}

smb::core::Result<bool> NativeSmbClient::checkConnection(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context) {
  const auto connected =
      openNativeConnection(connection, secret, context, m_timeoutSeconds);
  if (!connected.ok) {
    return smb::core::Result<bool>::failure(
        appError(connected.error, m_sanitizer, secret));
  }
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<QVector<smb::core::RemoteFileEntry>>
NativeSmbClient::listDirectory(const smb::core::Connection &connection,
                               const smb::core::CredentialSecret *secret,
                               const QString &remotePath,
                               const smb::core::OperationContext &context) {
  auto connected =
      openNativeConnection(connection, secret, context, m_timeoutSeconds);
  if (!connected.ok) {
    return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::failure(
        appError(connected.error, m_sanitizer, secret));
  }

  const auto normalizedPath = normalizeRemotePath(remotePath);
  const auto listing = connected.value.connection->listDirectory(
      normalizedPath.toStdString(), nativeContext(context, m_timeoutSeconds));
  if (!listing.ok) {
    return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::failure(
        appError(listing.error, m_sanitizer, secret));
  }

  QVector<smb::core::RemoteFileEntry> entries;
  entries.reserve(static_cast<int>(listing.value.entries.size()));
  for (const auto &entry : listing.value.entries) {
    entries.push_back(toCoreEntry(entry, normalizedPath));
  }
  return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::success(
      std::move(entries));
}

smb::core::Result<bool> NativeSmbClient::createDirectory(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context) {
  auto connected =
      openNativeConnection(connection, secret, context, m_timeoutSeconds);
  if (!connected.ok) {
    return smb::core::Result<bool>::failure(
        appError(connected.error, m_sanitizer, secret));
  }
  const auto result = connected.value.connection->createDirectory(
      normalizeRemotePath(remotePath).toStdString(),
      nativeContext(context, m_timeoutSeconds));
  if (!result.ok) {
    return smb::core::Result<bool>::failure(
        appError(result.error, m_sanitizer, secret));
  }
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> NativeSmbClient::remove(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context) {
  auto connected =
      openNativeConnection(connection, secret, context, m_timeoutSeconds);
  if (!connected.ok) {
    return smb::core::Result<bool>::failure(
        appError(connected.error, m_sanitizer, secret));
  }

  const auto path = normalizeRemotePath(remotePath);
  auto stat = connected.value.connection->statObject(
      path.toStdString(), nativeContext(context, m_timeoutSeconds));
  if (!stat.ok) {
    return smb::core::Result<bool>::failure(
        appError(stat.error, m_sanitizer, secret));
  }
  const auto nativePath = path.toStdString();
  const auto result =
      stat.value.directory && !stat.value.reparsePoint
          ? connected.value.connection->deleteTree(
                nativePath, nativeContext(context, m_timeoutSeconds))
          : connected.value.connection->deleteObject(
                nativePath, stat.value.directory,
                nativeContext(context, m_timeoutSeconds));
  if (!result.ok) {
    return smb::core::Result<bool>::failure(
        appError(result.error, m_sanitizer, secret));
  }
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> NativeSmbClient::rename(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const QString &sourceRemotePath, const QString &targetRemotePath,
    const smb::core::OperationContext &context) {
  auto connected =
      openNativeConnection(connection, secret, context, m_timeoutSeconds);
  if (!connected.ok) {
    return smb::core::Result<bool>::failure(
        appError(connected.error, m_sanitizer, secret));
  }

  const auto result = connected.value.connection->renameObject(
      normalizeRemotePath(sourceRemotePath).toStdString(),
      normalizeRemotePath(targetRemotePath).toStdString(), true,
      nativeContext(context, m_timeoutSeconds));
  if (!result.ok) {
    return smb::core::Result<bool>::failure(
        appError(result.error, m_sanitizer, secret));
  }
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> NativeSmbClient::downloadFile(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const QString &localPath, const smb::core::OperationContext &context) {
  auto connected =
      openNativeConnection(connection, secret, context, m_timeoutSeconds);
  if (!connected.ok) {
    return smb::core::Result<bool>::failure(
        appError(connected.error, m_sanitizer, secret));
  }

  const auto path = normalizeRemotePath(remotePath);
  const auto stat = connected.value.connection->statObject(
      path.toStdString(), nativeContext(context, m_timeoutSeconds));
  if (!stat.ok) {
    return smb::core::Result<bool>::failure(
        appError(stat.error, m_sanitizer, secret));
  }

  QFile file(localPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return smb::core::Result<bool>::failure(localIoError(file.errorString()));
  }

  std::uint64_t offset = 0;
  while (offset < stat.value.size) {
    if (isCancelled(context)) {
      return cancelledFailure();
    }
    const auto length = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(kChunkSize, stat.value.size - offset));
    const auto read = connected.value.connection->readFileOnce(
        path.toStdString(), length, offset,
        nativeContext(context, m_timeoutSeconds));
    if (!read.ok) {
      return smb::core::Result<bool>::failure(
          appError(read.error, m_sanitizer, secret));
    }
    if (file.write(reinterpret_cast<const char *>(read.value.data.data()),
                   static_cast<qint64>(read.value.data.size())) !=
        static_cast<qint64>(read.value.data.size())) {
      return smb::core::Result<bool>::failure(localIoError(file.errorString()));
    }
    offset += read.value.data.size();
    if (context.progressCallback) {
      context.progressCallback(smb::core::TransferProgress{
          static_cast<qint64>(offset), static_cast<qint64>(stat.value.size)});
    }
    if (read.value.data.empty()) {
      break;
    }
  }
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> NativeSmbClient::uploadFile(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &localPath,
    const QString &remotePath, const smb::core::OperationContext &context) {
  auto connected =
      openNativeConnection(connection, secret, context, m_timeoutSeconds);
  if (!connected.ok) {
    return smb::core::Result<bool>::failure(
        appError(connected.error, m_sanitizer, secret));
  }

  QFile file(localPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return smb::core::Result<bool>::failure(localIoError(file.errorString()));
  }

  const auto path = normalizeRemotePath(remotePath);
  const auto existing = connected.value.connection->statObject(
      path.toStdString(), nativeContext(context, m_timeoutSeconds));
  if (existing.ok) {
    const auto removed = connected.value.connection->deleteObject(
        path.toStdString(), existing.value.directory,
        nativeContext(context, m_timeoutSeconds));
    if (!removed.ok) {
      return smb::core::Result<bool>::failure(
          appError(removed.error, m_sanitizer, secret));
    }
  } else if (existing.error.code != smb::native_smb::ErrorCode::FileNotFound) {
    return smb::core::Result<bool>::failure(
        appError(existing.error, m_sanitizer, secret));
  }

  const auto total = file.size();
  std::uint64_t offset = 0;
  while (!file.atEnd()) {
    if (isCancelled(context)) {
      return cancelledFailure();
    }
    const auto chunk = file.read(kChunkSize);
    if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
      return smb::core::Result<bool>::failure(localIoError(file.errorString()));
    }
    const auto *begin =
        reinterpret_cast<const std::uint8_t *>(chunk.constData());
    const smb::native_smb::ByteVector data(begin, begin + chunk.size());
    const auto write = connected.value.connection->writeFileOnce(
        path.toStdString(), data, offset, nativeContext(context, m_timeoutSeconds));
    if (!write.ok) {
      return smb::core::Result<bool>::failure(
          appError(write.error, m_sanitizer, secret));
    }
    offset += static_cast<std::uint64_t>(chunk.size());
    if (context.progressCallback) {
      context.progressCallback(smb::core::TransferProgress{
          static_cast<qint64>(offset), total});
    }
  }
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> NativeSmbClient::copy(
    const smb::core::Connection &sourceConnection,
    const smb::core::CredentialSecret *sourceSecret,
    const QString &sourceRemotePath,
    const smb::core::Connection &targetConnection,
    const smb::core::CredentialSecret *targetSecret,
    const QString &targetRemotePath,
    const smb::core::OperationContext &context) {
  auto source =
      openNativeConnection(sourceConnection, sourceSecret, context, m_timeoutSeconds);
  if (!source.ok) {
    return smb::core::Result<bool>::failure(
        appError(source.error, m_sanitizer, sourceSecret));
  }
  auto target =
      openNativeConnection(targetConnection, targetSecret, context, m_timeoutSeconds);
  if (!target.ok) {
    return smb::core::Result<bool>::failure(
        appError(target.error, m_sanitizer, targetSecret));
  }

  const auto sourcePath = normalizeRemotePath(sourceRemotePath);
  const auto targetPath = normalizeRemotePath(targetRemotePath);
  const auto stat = source.value.connection->statObject(
      sourcePath.toStdString(), nativeContext(context, m_timeoutSeconds));
  if (!stat.ok) {
    return smb::core::Result<bool>::failure(
        appError(stat.error, m_sanitizer, sourceSecret));
  }
  if (stat.value.directory) {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::ProtocolUnsupported,
        smb::core::ErrorCategory::Smb,
        QObject::tr("Directory copy is not implemented by the native SMB "
                    "backend yet."),
        false));
  }

  const auto existing = target.value.connection->statObject(
      targetPath.toStdString(), nativeContext(context, m_timeoutSeconds));
  if (existing.ok) {
    const auto removed = target.value.connection->deleteObject(
        targetPath.toStdString(), existing.value.directory,
        nativeContext(context, m_timeoutSeconds));
    if (!removed.ok) {
      return smb::core::Result<bool>::failure(
          appError(removed.error, m_sanitizer, targetSecret));
    }
  } else if (existing.error.code != smb::native_smb::ErrorCode::FileNotFound) {
    return smb::core::Result<bool>::failure(
        appError(existing.error, m_sanitizer, targetSecret));
  }

  std::uint64_t offset = 0;
  while (offset < stat.value.size) {
    if (isCancelled(context)) {
      return cancelledFailure();
    }
    const auto length = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(kChunkSize, stat.value.size - offset));
    const auto read = source.value.connection->readFileOnce(
        sourcePath.toStdString(), length, offset,
        nativeContext(context, m_timeoutSeconds));
    if (!read.ok) {
      return smb::core::Result<bool>::failure(
          appError(read.error, m_sanitizer, sourceSecret));
    }
    const auto write = target.value.connection->writeFileOnce(
        targetPath.toStdString(), read.value.data, offset,
        nativeContext(context, m_timeoutSeconds));
    if (!write.ok) {
      return smb::core::Result<bool>::failure(
          appError(write.error, m_sanitizer, targetSecret));
    }
    offset += read.value.data.size();
    if (context.progressCallback) {
      context.progressCallback(smb::core::TransferProgress{
          static_cast<qint64>(offset), static_cast<qint64>(stat.value.size)});
    }
    if (read.value.data.empty()) {
      break;
    }
  }
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> NativeSmbClient::move(
    const smb::core::Connection &sourceConnection,
    const smb::core::CredentialSecret *sourceSecret,
    const QString &sourceRemotePath,
    const smb::core::Connection &targetConnection,
    const smb::core::CredentialSecret *targetSecret,
    const QString &targetRemotePath,
    const smb::core::OperationContext &context) {
  if (sameRemoteTree(sourceConnection, targetConnection)) {
    return rename(sourceConnection, sourceSecret, sourceRemotePath,
                  targetRemotePath, context);
  }

  const auto copied = copy(sourceConnection, sourceSecret, sourceRemotePath,
                          targetConnection, targetSecret, targetRemotePath,
                          context);
  if (!copied.ok()) {
    return copied;
  }
  return remove(sourceConnection, sourceSecret, sourceRemotePath, context);
}

} // namespace smb::infrastructure
