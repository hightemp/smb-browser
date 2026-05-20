#include "smb/NativeDfsReferralResolver.h"

#include "DirectTcpTransport.h"
#include "NativeSmbConnector.h"
#include "NtlmV2TokenProvider.h"
#include "smb/NativeSmbErrorMapper.h"

#include <QSysInfo>

#include <algorithm>
#include <array>
#include <memory>
#include <random>
#include <utility>

namespace smb::infrastructure {
namespace {

bool isCancelled(const smb::core::OperationContext &context) {
  return context.cancellationToken != nullptr &&
         context.cancellationToken->isCancellationRequested();
}

std::array<std::uint8_t, 16> randomClientGuid() {
  std::array<std::uint8_t, 16> guid{};
  std::random_device device;
  for (auto &byte : guid) {
    byte = static_cast<std::uint8_t>(device());
  }
  guid[6] = static_cast<std::uint8_t>((guid[6] & 0x0F) | 0x40);
  guid[8] = static_cast<std::uint8_t>((guid[8] & 0x3F) | 0x80);
  return guid;
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

smb::native_smb::ConnectionConfig nativeConfig(
    const smb::core::Connection &connection, int timeoutSeconds) {
  smb::native_smb::ConnectionConfig config;
  config.server = endpointForServer(connection.server).host;
  config.share = connection.share.toStdString();
  config.normalizedUri = connection.normalizedUri.toStdString();
  config.domain = connection.domain.toStdString();
  config.username = connection.username.toStdString();
  config.authMode = toNativeAuthMode(connection.authType);
  config.signing = smb::native_smb::SecurityPolicy::Preferred;
  config.encryption = smb::native_smb::SecurityPolicy::Preferred;
  config.timeout = std::chrono::seconds(std::max(1, timeoutSeconds));
  return config;
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
  return native;
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

QString normalizedUri(const QString &server, const QString &share) {
  return QStringLiteral("smb://%1/%2").arg(server, share);
}

QString normalizeRemotePath(QString remotePath) {
  remotePath = remotePath.trimmed();
  remotePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
  while (remotePath.contains(QStringLiteral("//"))) {
    remotePath.replace(QStringLiteral("//"), QStringLiteral("/"));
  }
  if (remotePath.isEmpty()) {
    remotePath = QStringLiteral("/");
  }
  if (!remotePath.startsWith(QLatin1Char('/'))) {
    remotePath.prepend(QLatin1Char('/'));
  }
  while (remotePath.size() > 1 && remotePath.endsWith(QLatin1Char('/'))) {
    remotePath.chop(1);
  }
  return remotePath;
}

QString pathFromSegments(const QStringList &segments, int count) {
  if (count <= 0) {
    return QStringLiteral("/");
  }
  return QStringLiteral("/%1")
      .arg(segments.mid(0, count).join(QLatin1Char('/')));
}

QString pathFromSegments(const QStringList &segments, int first, int count) {
  if (count <= 0 || first >= segments.size()) {
    return QStringLiteral("/");
  }
  return QStringLiteral("/%1")
      .arg(segments.mid(first, count).join(QLatin1Char('/')));
}

QStringList remotePathSegments(const QString &remotePath) {
  const auto normalized = normalizeRemotePath(remotePath);
  if (normalized == QStringLiteral("/")) {
    return {};
  }
  return normalized.mid(1).split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

QString requestUncPath(const smb::core::Connection &connection,
                       const QString &serverForUnc,
                       const QString &remotePath) {
  auto path = normalizeRemotePath(remotePath);
  path.replace(QLatin1Char('/'), QLatin1Char('\\'));
  if (path == QStringLiteral("\\")) {
    path.clear();
  }
  return QStringLiteral("\\\\%1\\%2%3")
      .arg(serverForUnc, connection.share, path);
}

bool sameShare(const smb::core::Connection &connection,
               const NativeDfsReferralTarget &target) {
  return connection.server.compare(target.server, Qt::CaseInsensitive) == 0 &&
         connection.share.compare(target.share, Qt::CaseInsensitive) == 0;
}

smb::core::Connection targetConnectionFor(
    const smb::core::Connection &connection,
    const NativeDfsReferralTarget &target) {
  auto resolved = connection;
  resolved.server = target.server;
  resolved.share = target.share;
  resolved.normalizedUri = normalizedUri(target.server, target.share);
  return resolved;
}

QString consumedOriginalPrefix(const QString &requestPath,
                               const QString &serverForUnc,
                               const QString &share,
                               std::uint16_t pathConsumedBytes,
                               const QString &fallbackRemotePath) {
  if (pathConsumedBytes == 0) {
    return normalizeRemotePath(fallbackRemotePath);
  }

  const auto consumedChars = static_cast<int>(pathConsumedBytes / 2);
  auto consumed = requestPath.left(std::min(consumedChars, requestPath.size()));
  consumed.replace(QLatin1Char('/'), QLatin1Char('\\'));
  const auto base = QStringLiteral("\\\\%1\\%2").arg(serverForUnc, share);
  if (consumed.compare(base, Qt::CaseInsensitive) == 0) {
    return QStringLiteral("/");
  }
  if (consumed.startsWith(base + QLatin1Char('\\'), Qt::CaseInsensitive)) {
    auto suffix = consumed.mid(base.size());
    suffix.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return normalizeRemotePath(suffix);
  }
  return normalizeRemotePath(fallbackRemotePath);
}

smb::native_smb::DecodeResult<smb::native_smb::NativeSmbConnectedState>
openIpcConnection(const smb::core::Connection &connection,
                  const smb::core::CredentialSecret *secret,
                  const smb::core::OperationContext &context,
                  int timeoutSeconds) {
  if (isCancelled(context)) {
    return smb::native_smb::DecodeResult<
        smb::native_smb::NativeSmbConnectedState>::failure(
        smb::native_smb::ErrorCode::Cancelled, "Operation was cancelled.");
  }

  auto ipcConnection = connection;
  ipcConnection.share = QStringLiteral("IPC$");
  ipcConnection.normalizedUri =
      QStringLiteral("smb://%1/IPC$").arg(connection.server);

  auto endpoint = endpointForServer(connection.server);
  auto transport =
      std::make_unique<smb::native_smb::DirectTcpTransport>(endpoint);

  smb::native_smb::NtlmV2TokenProviderOptions tokenOptions;
  tokenOptions.workstation = QSysInfo::machineHostName().toStdString();
  smb::native_smb::NtlmV2TokenProvider tokenProvider(nativeSecret(secret),
                                                     tokenOptions);

  smb::native_smb::NativeSmbConnectorOptions options;
  options.config = nativeConfig(ipcConnection, timeoutSeconds);
  options.negotiateOptions.dialects = {smb::native_smb::Dialect::Smb202,
                                       smb::native_smb::Dialect::Smb210,
                                       smb::native_smb::Dialect::Smb300,
                                       smb::native_smb::Dialect::Smb302,
                                       smb::native_smb::Dialect::Smb311};
  options.negotiateOptions.clientGuid = randomClientGuid();

  const smb::native_smb::NativeSmbConnector connector;
  return connector.connect(std::move(transport), tokenProvider, options,
                           nativeContext(context, timeoutSeconds));
}

} // namespace

std::optional<NativeDfsReferralTarget>
parseNativeDfsReferralTarget(const QString &networkAddress) {
  auto path = networkAddress.trimmed();
  path.replace(QLatin1Char('/'), QLatin1Char('\\'));
  while (path.startsWith(QStringLiteral("\\\\"))) {
    path.remove(0, 2);
  }
  while (path.startsWith(QLatin1Char('\\'))) {
    path.remove(0, 1);
  }

  const auto parts = path.split(QLatin1Char('\\'), Qt::KeepEmptyParts);
  if (parts.size() < 2 || parts.at(0).isEmpty() || parts.at(1).isEmpty()) {
    return std::nullopt;
  }

  NativeDfsReferralTarget target;
  target.server = parts.at(0);
  target.share = parts.at(1);
  target.targetPathPrefix =
      parts.size() > 2
          ? normalizeRemotePath(QStringLiteral("/%1")
                                    .arg(parts.mid(2).join(QLatin1Char('/'))))
          : QStringLiteral("/");
  return target;
}

NativeDfsReferralResolver::NativeDfsReferralResolver(
    int timeoutSeconds, smb::core::LogSanitizer sanitizer)
    : m_timeoutSeconds(std::max(1, timeoutSeconds)),
      m_sanitizer(std::move(sanitizer)) {}

smb::core::Result<std::optional<smb::core::Connection>>
NativeDfsReferralResolver::resolve(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context) {
  const auto targets = resolveTargets(connection, secret, context);
  if (!targets.ok()) {
    return smb::core::Result<std::optional<smb::core::Connection>>::failure(
        targets.error());
  }
  if (targets.value().isEmpty()) {
    return smb::core::Result<std::optional<smb::core::Connection>>::success(
        std::nullopt);
  }
  return smb::core::Result<std::optional<smb::core::Connection>>::success(
      targets.value().first().connection);
}

smb::core::Result<QVector<smb::core::DfsResolvedConnection>>
NativeDfsReferralResolver::resolveTargets(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context) {
  const auto endpoint = endpointForServer(connection.server);
  const auto requestPath =
      requestUncPath(connection, QString::fromStdString(endpoint.host),
                     QStringLiteral("/"));
  auto connected =
      openIpcConnection(connection, secret, context, m_timeoutSeconds);
  if (!connected.ok) {
    return smb::core::Result<QVector<smb::core::DfsResolvedConnection>>::failure(
        appError(connected.error, m_sanitizer, secret));
  }

  const auto referrals = connected.value.connection->getDfsReferrals(
      requestPath.toStdString(), nativeContext(context, m_timeoutSeconds));
  if (!referrals.ok) {
    return smb::core::Result<QVector<smb::core::DfsResolvedConnection>>::failure(
        appError(referrals.error, m_sanitizer, secret));
  }

  QVector<smb::core::DfsResolvedConnection> targets;
  for (const auto &entry : referrals.value.response.entries) {
    const auto target = parseNativeDfsReferralTarget(
        QString::fromStdString(entry.networkAddress));
    if (!target.has_value() || sameShare(connection, target.value())) {
      continue;
    }

    smb::core::DfsResolvedConnection resolved;
    resolved.connection = targetConnectionFor(connection, target.value());
    resolved.ttlSeconds =
        static_cast<int>(std::max<std::uint32_t>(1, entry.timeToLiveSeconds));
    targets.push_back(std::move(resolved));
  }

  return smb::core::Result<QVector<smb::core::DfsResolvedConnection>>::success(
      std::move(targets));
}

smb::core::Result<std::optional<smb::core::DfsResolvedPath>>
NativeDfsReferralResolver::resolvePath(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context) {
  const auto targets = resolvePathTargets(connection, secret, remotePath,
                                          context);
  if (!targets.ok()) {
    return smb::core::Result<
        std::optional<smb::core::DfsResolvedPath>>::failure(targets.error());
  }
  if (targets.value().isEmpty()) {
    return smb::core::Result<std::optional<smb::core::DfsResolvedPath>>::success(
        std::nullopt);
  }
  return smb::core::Result<std::optional<smb::core::DfsResolvedPath>>::success(
      targets.value().first());
}

smb::core::Result<QVector<smb::core::DfsResolvedPath>>
NativeDfsReferralResolver::resolvePathTargets(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context) {
  const auto endpoint = endpointForServer(connection.server);
  const auto serverForUnc = QString::fromStdString(endpoint.host);
  auto connected =
      openIpcConnection(connection, secret, context, m_timeoutSeconds);
  if (!connected.ok) {
    return smb::core::Result<QVector<smb::core::DfsResolvedPath>>::failure(
        appError(connected.error, m_sanitizer, secret));
  }

  const auto segments = remotePathSegments(remotePath);
  for (int count = segments.size(); count >= 0; --count) {
    const auto prefix = pathFromSegments(segments, count);
    const auto requestPath = requestUncPath(connection, serverForUnc, prefix);
    const auto referrals = connected.value.connection->getDfsReferrals(
        requestPath.toStdString(), nativeContext(context, m_timeoutSeconds));
    if (!referrals.ok) {
      if (referrals.error.code == smb::native_smb::ErrorCode::FileNotFound ||
          referrals.error.code ==
              smb::native_smb::ErrorCode::ShareUnavailable ||
          referrals.error.code == smb::native_smb::ErrorCode::NetworkError) {
        continue;
      }
      return smb::core::Result<QVector<smb::core::DfsResolvedPath>>::failure(
          appError(referrals.error, m_sanitizer, secret));
    }

    QVector<smb::core::DfsResolvedPath> paths;
    for (const auto &entry : referrals.value.response.entries) {
      const auto target = parseNativeDfsReferralTarget(
          QString::fromStdString(entry.networkAddress));
      if (!target.has_value() || sameShare(connection, target.value())) {
        continue;
      }

      smb::core::DfsResolvedPath path;
      path.connection = targetConnectionFor(connection, target.value());
      path.originalPathPrefix = consumedOriginalPrefix(
          requestPath, serverForUnc, connection.share,
          referrals.value.response.pathConsumedBytes, prefix);
      path.targetPathPrefix = target->targetPathPrefix;
      path.remotePath = pathFromSegments(segments, count,
                                         segments.size() - count);
      path.ttlSeconds =
          static_cast<int>(std::max<std::uint32_t>(1, entry.timeToLiveSeconds));
      paths.push_back(std::move(path));
    }
    if (!paths.isEmpty()) {
      return smb::core::Result<QVector<smb::core::DfsResolvedPath>>::success(
          std::move(paths));
    }
  }

  return smb::core::Result<QVector<smb::core::DfsResolvedPath>>::success({});
}

} // namespace smb::infrastructure
