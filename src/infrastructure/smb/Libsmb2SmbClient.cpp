#include "smb/Libsmb2SmbClient.h"

#include "smb/Libsmb2ErrorMapper.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <cerrno>
#include <fcntl.h>
#include <utility>

extern "C" {
// clang-format off
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
// clang-format on
}

namespace smb::infrastructure {

namespace {

bool isCancelled(const smb::core::OperationContext &context) {
  return context.cancellationToken != nullptr &&
         context.cancellationToken->isCancellationRequested();
}

smb::core::AppError cancelledError() {
  return smb::core::AppError::fromCode(
      smb::core::ErrorCode::OperationCancelled, smb::core::ErrorCategory::Smb,
      QStringLiteral("Operation cancelled."), false);
}

QString normalizedRemotePath(QString remotePath) {
  remotePath =
      remotePath.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
  while (remotePath.startsWith(QLatin1Char('/'))) {
    remotePath.remove(0, 1);
  }
  while (remotePath.endsWith(QLatin1Char('/'))) {
    remotePath.chop(1);
  }
  return remotePath;
}

bool sameShare(const smb::core::Connection &left,
               const smb::core::Connection &right) {
  return left.server.compare(right.server, Qt::CaseInsensitive) == 0 &&
         left.share.compare(right.share, Qt::CaseInsensitive) == 0;
}

QString joinRemotePath(const QString &parent, const QString &name) {
  auto cleanParent =
      parent.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (cleanParent.isEmpty() || cleanParent == QStringLiteral("/")) {
    return QStringLiteral("/") + name;
  }
  while (cleanParent.endsWith(QLatin1Char('/'))) {
    cleanParent.chop(1);
  }
  if (!cleanParent.startsWith(QLatin1Char('/'))) {
    cleanParent.prepend(QLatin1Char('/'));
  }
  return cleanParent + QStringLiteral("/") + name;
}

smb::core::RemoteFileType remoteFileType(uint32_t smb2Type) {
  switch (smb2Type) {
  case SMB2_TYPE_FILE:
    return smb::core::RemoteFileType::File;
  case SMB2_TYPE_DIRECTORY:
    return smb::core::RemoteFileType::Directory;
  case SMB2_TYPE_LINK:
    return smb::core::RemoteFileType::Symlink;
  default:
    return smb::core::RemoteFileType::Unknown;
  }
}

bool isDotEntry(const QString &name) {
  return name == QStringLiteral(".") || name == QStringLiteral("..");
}

class AuthBuffers {
public:
  AuthBuffers(const smb::core::Connection &connection,
              const smb::core::CredentialSecret *secret)
      : server(connection.server.toUtf8()), share(connection.share.toUtf8()),
        domain(connection.domain.toUtf8()),
        username(connection.username.toUtf8()) {
    if (secret != nullptr) {
      password = secret->bytes;
    }

    if (connection.authType == smb::core::AuthType::Guest &&
        username.isEmpty()) {
      username = QByteArrayLiteral("guest");
    }
  }

  ~AuthBuffers() {
    if (!password.isEmpty()) {
      password.fill('\0');
    }
  }

  const char *serverPtr() const { return server.constData(); }
  const char *sharePtr() const { return share.constData(); }

  const char *domainPtr() const {
    return domain.isEmpty() ? nullptr : domain.constData();
  }

  const char *userPtr(smb::core::AuthType authType) const {
    if (authType == smb::core::AuthType::CurrentUser) {
      return nullptr;
    }
    return username.isEmpty() ? "" : username.constData();
  }

  const char *passwordPtr() const {
    return password.isEmpty() ? "" : password.constData();
  }

  QByteArray server;
  QByteArray share;
  QByteArray domain;
  QByteArray username;
  QByteArray password;
};

class Smb2Session {
public:
  explicit Smb2Session(int timeoutSeconds) : m_context(smb2_init_context()) {
    if (m_context != nullptr) {
      smb2_set_timeout(m_context, timeoutSeconds);
      smb2_set_security_mode(m_context, SMB2_NEGOTIATE_SIGNING_ENABLED);
    }
  }

  ~Smb2Session() {
    if (m_context == nullptr) {
      return;
    }
    if (m_connected) {
      smb2_disconnect_share(m_context);
    }
    smb2_destroy_context(m_context);
  }

  Smb2Session(const Smb2Session &) = delete;
  Smb2Session &operator=(const Smb2Session &) = delete;

  bool isValid() const { return m_context != nullptr; }
  smb2_context *context() const { return m_context; }

  int connectShare(const smb::core::Connection &connection,
                   const AuthBuffers &auth) {
    if (auth.domainPtr() != nullptr) {
      smb2_set_domain(m_context, auth.domainPtr());
    }

    if (connection.authType != smb::core::AuthType::CurrentUser) {
      smb2_set_user(m_context, auth.userPtr(connection.authType));
    }
    if (connection.authType == smb::core::AuthType::Password ||
        connection.authType == smb::core::AuthType::Guest ||
        connection.authType == smb::core::AuthType::Anonymous) {
      smb2_set_password(m_context, auth.passwordPtr());
    }

    const auto rc =
        smb2_connect_share(m_context, auth.serverPtr(), auth.sharePtr(),
                           auth.userPtr(connection.authType));
    m_connected = rc == 0;
    return rc;
  }

private:
  smb2_context *m_context = nullptr;
  bool m_connected = false;
};

class Smb2FileHandle {
public:
  Smb2FileHandle(smb2_context *context, smb2fh *handle)
      : m_context(context), m_handle(handle) {}

  ~Smb2FileHandle() {
    if (m_context != nullptr && m_handle != nullptr) {
      smb2_close(m_context, m_handle);
    }
  }

  Smb2FileHandle(const Smb2FileHandle &) = delete;
  Smb2FileHandle &operator=(const Smb2FileHandle &) = delete;

  smb2fh *get() const { return m_handle; }

private:
  smb2_context *m_context = nullptr;
  smb2fh *m_handle = nullptr;
};

int statusFromErrno() { return errno == 0 ? -EIO : -errno; }

QString smb2Details(smb2_context *context, int status, const QString &phase) {
  QString details =
      QStringLiteral("libsmb2 %1 failed with status %2").arg(phase).arg(status);
  if (context != nullptr) {
    const auto *error = smb2_get_error(context);
    if (error != nullptr && error[0] != '\0') {
      details += QStringLiteral(": ");
      details += QString::fromUtf8(error);
    }

    const auto ntStatus = smb2_get_nterror(context);
    if (ntStatus != 0) {
      details += QStringLiteral(" ntstatus=0x%1")
                     .arg(static_cast<uint>(ntStatus), 0, 16);
    }
  }
  return details;
}

smb::core::AppError smbError(smb::core::ErrorCode code, const QString &details,
                             const smb::core::LogSanitizer &baseSanitizer,
                             const smb::core::CredentialSecret *secret) {
  auto sanitizer = baseSanitizer;
  if (secret != nullptr && !secret->bytes.isEmpty()) {
    sanitizer.addSecretValue(QString::fromUtf8(secret->bytes));
  }

  return smb::core::AppError::fromCode(
      code, smb::core::ErrorCategory::Smb, sanitizer.sanitize(details),
      code == smb::core::ErrorCode::Timeout ||
          code == smb::core::ErrorCode::ServerUnavailable ||
      code == smb::core::ErrorCode::NetworkError);
}

smb::core::AppError
libsmb2Error(int status, const QString &details,
             Libsmb2ErrorContext context,
             const smb::core::LogSanitizer &baseSanitizer,
             const smb::core::CredentialSecret *secret) {
  auto sanitizer = baseSanitizer;
  if (secret != nullptr && !secret->bytes.isEmpty()) {
    sanitizer.addSecretValue(QString::fromUtf8(secret->bytes));
  }
  return makeLibsmb2Error(status, details, context, sanitizer);
}

smb::core::Result<bool>
validateConnectionInput(const smb::core::Connection &connection,
                        const smb::core::CredentialSecret *secret,
                        const smb::core::OperationContext &context) {
  if (isCancelled(context)) {
    return smb::core::Result<bool>::failure(cancelledError());
  }
  if (connection.server.isEmpty() || connection.share.isEmpty()) {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::InvalidPath, smb::core::ErrorCategory::Smb,
        QStringLiteral("Missing server/share."), false));
  }
  if (connection.authType == smb::core::AuthType::Password &&
      (secret == nullptr || secret->bytes.isEmpty())) {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::CredentialNotFound,
        smb::core::ErrorCategory::Credentials,
        QStringLiteral("Password authentication requires a secret."), false));
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool>
connectSession(Smb2Session &session, const smb::core::Connection &connection,
               const smb::core::CredentialSecret *secret,
               const smb::core::LogSanitizer &sanitizer) {
  if (!session.isValid()) {
    return smb::core::Result<bool>::failure(
        smbError(smb::core::ErrorCode::NetworkError,
                 QStringLiteral("Failed to initialize libsmb2 context."),
                 sanitizer, secret));
  }

  const AuthBuffers auth(connection, secret);
  const auto rc = session.connectShare(connection, auth);
  if (rc < 0) {
    const auto details =
        smb2Details(session.context(), rc, QStringLiteral("connect_share"));
    return smb::core::Result<bool>::failure(
        libsmb2Error(rc, details, Libsmb2ErrorContext::Connection, sanitizer, secret));
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::AppError localIoError(const QString &details) {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::LocalIoError,
                                       smb::core::ErrorCategory::Transfer,
                                       details, false);
}

} // namespace

Libsmb2SmbClient::Libsmb2SmbClient(int timeoutSeconds,
                                   smb::core::LogSanitizer sanitizer)
    : m_timeoutSeconds(timeoutSeconds), m_sanitizer(std::move(sanitizer)) {}

smb::core::Result<bool>
Libsmb2SmbClient::checkConnection(const smb::core::Connection &connection,
                                  const smb::core::CredentialSecret *secret,
                                  const smb::core::OperationContext &context) {
  const auto valid = validateConnectionInput(connection, secret, context);
  if (!valid.ok()) {
    return valid;
  }

  Smb2Session session(m_timeoutSeconds);
  const auto connected =
      connectSession(session, connection, secret, m_sanitizer);
  if (!connected.ok()) {
    return connected;
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<QVector<smb::core::RemoteFileEntry>>
Libsmb2SmbClient::listDirectory(const smb::core::Connection &connection,
                                const smb::core::CredentialSecret *secret,
                                const QString &remotePath,
                                const smb::core::OperationContext &context) {
  const auto valid = validateConnectionInput(connection, secret, context);
  if (!valid.ok()) {
    return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::failure(
        valid.error());
  }

  Smb2Session session(m_timeoutSeconds);
  const auto connected =
      connectSession(session, connection, secret, m_sanitizer);
  if (!connected.ok()) {
    return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::failure(
        connected.error());
  }

  if (isCancelled(context)) {
    return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::failure(
        cancelledError());
  }

  const auto path = normalizedRemotePath(remotePath);
  const auto pathBytes = path.toUtf8();
  auto *directory = smb2_opendir(session.context(), pathBytes.constData());
  if (directory == nullptr) {
    const auto status = statusFromErrno();
    const auto details =
        smb2Details(session.context(), status, QStringLiteral("opendir"));
    return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::failure(
        libsmb2Error(status, details, Libsmb2ErrorContext::Directory, m_sanitizer, secret));
  }

  QVector<smb::core::RemoteFileEntry> entries;
  while (auto *entry = smb2_readdir(session.context(), directory)) {
    if (isCancelled(context)) {
      smb2_closedir(session.context(), directory);
      return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::failure(
          cancelledError());
    }

    const auto name = QString::fromUtf8(entry->name);
    if (isDotEntry(name)) {
      continue;
    }

    smb::core::RemoteFileEntry remoteEntry;
    remoteEntry.name = name;
    remoteEntry.remotePath = joinRemotePath(remotePath, name);
    remoteEntry.type = remoteFileType(entry->st.smb2_type);
    remoteEntry.size = static_cast<qint64>(entry->st.smb2_size);
    if (entry->st.smb2_mtime > 0) {
      remoteEntry.modifiedAt = QDateTime::fromSecsSinceEpoch(
          static_cast<qint64>(entry->st.smb2_mtime), Qt::UTC);
    }
    entries.push_back(remoteEntry);
  }

  smb2_closedir(session.context(), directory);
  return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::success(
      std::move(entries));
}

smb::core::Result<bool>
Libsmb2SmbClient::createDirectory(const smb::core::Connection &connection,
                                  const smb::core::CredentialSecret *secret,
                                  const QString &remotePath,
                                  const smb::core::OperationContext &context) {
  const auto valid = validateConnectionInput(connection, secret, context);
  if (!valid.ok()) {
    return valid;
  }

  Smb2Session session(m_timeoutSeconds);
  const auto connected =
      connectSession(session, connection, secret, m_sanitizer);
  if (!connected.ok()) {
    return connected;
  }

  const auto pathBytes = normalizedRemotePath(remotePath).toUtf8();
  const auto rc = smb2_mkdir(session.context(), pathBytes.constData());
  if (rc < 0) {
    const auto details =
        smb2Details(session.context(), rc, QStringLiteral("mkdir"));
    return smb::core::Result<bool>::failure(libsmb2Error(rc, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, secret));
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool>
Libsmb2SmbClient::remove(const smb::core::Connection &connection,
                         const smb::core::CredentialSecret *secret,
                         const QString &remotePath,
                         const smb::core::OperationContext &context) {
  const auto valid = validateConnectionInput(connection, secret, context);
  if (!valid.ok()) {
    return valid;
  }

  Smb2Session session(m_timeoutSeconds);
  const auto connected =
      connectSession(session, connection, secret, m_sanitizer);
  if (!connected.ok()) {
    return connected;
  }

  const auto pathBytes = normalizedRemotePath(remotePath).toUtf8();
  smb2_stat_64 stat{};
  const auto statRc =
      smb2_stat(session.context(), pathBytes.constData(), &stat);
  if (statRc < 0) {
    const auto details =
        smb2Details(session.context(), statRc, QStringLiteral("stat"));
    return smb::core::Result<bool>::failure(libsmb2Error(statRc, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, secret));
  }

  const auto removeRc =
      stat.smb2_type == SMB2_TYPE_DIRECTORY
          ? smb2_rmdir(session.context(), pathBytes.constData())
          : smb2_unlink(session.context(), pathBytes.constData());
  if (removeRc < 0) {
    const auto details =
        smb2Details(session.context(), removeRc, QStringLiteral("remove"));
    return smb::core::Result<bool>::failure(libsmb2Error(removeRc, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, secret));
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool>
Libsmb2SmbClient::rename(const smb::core::Connection &connection,
                         const smb::core::CredentialSecret *secret,
                         const QString &sourceRemotePath,
                         const QString &targetRemotePath,
                         const smb::core::OperationContext &context) {
  const auto valid = validateConnectionInput(connection, secret, context);
  if (!valid.ok()) {
    return valid;
  }

  Smb2Session session(m_timeoutSeconds);
  const auto connected =
      connectSession(session, connection, secret, m_sanitizer);
  if (!connected.ok()) {
    return connected;
  }

  const auto sourceBytes = normalizedRemotePath(sourceRemotePath).toUtf8();
  const auto targetBytes = normalizedRemotePath(targetRemotePath).toUtf8();
  const auto rc = smb2_rename(session.context(), sourceBytes.constData(),
                              targetBytes.constData());
  if (rc < 0) {
    const auto details =
        smb2Details(session.context(), rc, QStringLiteral("rename"));
    return smb::core::Result<bool>::failure(libsmb2Error(rc, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, secret));
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> Libsmb2SmbClient::downloadFile(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const QString &localPath, const smb::core::OperationContext &context) {
  const auto valid = validateConnectionInput(connection, secret, context);
  if (!valid.ok()) {
    return valid;
  }

  Smb2Session session(m_timeoutSeconds);
  const auto connected =
      connectSession(session, connection, secret, m_sanitizer);
  if (!connected.ok()) {
    return connected;
  }

  const auto pathBytes = normalizedRemotePath(remotePath).toUtf8();
  smb2_stat_64 stat{};
  const auto statRc =
      smb2_stat(session.context(), pathBytes.constData(), &stat);
  const auto totalSize =
      statRc == 0 ? static_cast<qint64>(stat.smb2_size) : qint64{0};

  auto *rawHandle =
      smb2_open(session.context(), pathBytes.constData(), O_RDONLY);
  if (rawHandle == nullptr) {
    const auto status = statusFromErrno();
    const auto details =
        smb2Details(session.context(), status, QStringLiteral("open_read"));
    return smb::core::Result<bool>::failure(libsmb2Error(status, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, secret));
  }
  Smb2FileHandle remoteFile(session.context(), rawHandle);

  QFile localFile(localPath);
  if (!localFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return smb::core::Result<bool>::failure(
        localIoError(QStringLiteral("Unable to open local target file.")));
  }

  QByteArray buffer(256 * 1024, '\0');
  qint64 offset = 0;
  while (true) {
    if (isCancelled(context)) {
      localFile.remove();
      return smb::core::Result<bool>::failure(cancelledError());
    }

    const auto readRc = smb2_pread(session.context(), remoteFile.get(),
                                   reinterpret_cast<uint8_t *>(buffer.data()),
                                   static_cast<uint32_t>(buffer.size()),
                                   static_cast<uint64_t>(offset));
    if (readRc == -EAGAIN) {
      continue;
    }
    if (readRc < 0) {
      const auto details =
          smb2Details(session.context(), readRc, QStringLiteral("read"));
      return smb::core::Result<bool>::failure(libsmb2Error(readRc, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, secret));
    }
    if (readRc == 0) {
      break;
    }

    if (localFile.write(buffer.constData(), readRc) != readRc) {
      return smb::core::Result<bool>::failure(
          localIoError(QStringLiteral("Unable to write local target file.")));
    }

    offset += readRc;
    if (context.progressCallback) {
      context.progressCallback(smb::core::TransferProgress{offset, totalSize});
    }
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> Libsmb2SmbClient::uploadFile(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &localPath,
    const QString &remotePath, const smb::core::OperationContext &context) {
  const auto valid = validateConnectionInput(connection, secret, context);
  if (!valid.ok()) {
    return valid;
  }

  QFile localFile(localPath);
  if (!localFile.open(QIODevice::ReadOnly)) {
    return smb::core::Result<bool>::failure(
        localIoError(QStringLiteral("Unable to open local source file.")));
  }

  const auto totalSize = QFileInfo(localPath).size();
  Smb2Session session(m_timeoutSeconds);
  const auto connected =
      connectSession(session, connection, secret, m_sanitizer);
  if (!connected.ok()) {
    return connected;
  }

  const auto pathBytes = normalizedRemotePath(remotePath).toUtf8();
  auto *rawHandle =
      smb2_open(session.context(), pathBytes.constData(), O_WRONLY | O_CREAT);
  if (rawHandle == nullptr) {
    const auto status = statusFromErrno();
    const auto details =
        smb2Details(session.context(), status, QStringLiteral("open_write"));
    return smb::core::Result<bool>::failure(libsmb2Error(status, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, secret));
  }
  Smb2FileHandle remoteFile(session.context(), rawHandle);

  const auto truncateRc =
      smb2_ftruncate(session.context(), remoteFile.get(), 0);
  if (truncateRc < 0) {
    const auto details =
        smb2Details(session.context(), truncateRc, QStringLiteral("truncate"));
    return smb::core::Result<bool>::failure(
        libsmb2Error(truncateRc, details, Libsmb2ErrorContext::FileOperation,
                     m_sanitizer, secret));
  }

  QByteArray buffer(256 * 1024, '\0');
  qint64 offset = 0;
  while (true) {
    if (isCancelled(context)) {
      return smb::core::Result<bool>::failure(cancelledError());
    }

    const auto bytesRead = localFile.read(buffer.data(), buffer.size());
    if (bytesRead < 0) {
      return smb::core::Result<bool>::failure(
          localIoError(QStringLiteral("Unable to read local source file.")));
    }
    if (bytesRead == 0) {
      break;
    }

    qint64 chunkOffset = 0;
    while (chunkOffset < bytesRead) {
      if (isCancelled(context)) {
        return smb::core::Result<bool>::failure(cancelledError());
      }

      const auto remaining = bytesRead - chunkOffset;
      const auto writeRc = smb2_pwrite(
          session.context(), remoteFile.get(),
          reinterpret_cast<const uint8_t *>(buffer.constData() + chunkOffset),
          static_cast<uint32_t>(remaining), static_cast<uint64_t>(offset));
      if (writeRc == -EAGAIN) {
        continue;
      }
      if (writeRc <= 0) {
        const auto details =
            smb2Details(session.context(), writeRc, QStringLiteral("write"));
        return smb::core::Result<bool>::failure(
            libsmb2Error(writeRc, details, Libsmb2ErrorContext::FileOperation,
                         m_sanitizer, secret));
      }

      chunkOffset += writeRc;
      offset += writeRc;
      if (context.progressCallback) {
        context.progressCallback(
            smb::core::TransferProgress{offset, totalSize});
      }
    }
  }

  const auto fsyncRc = smb2_fsync(session.context(), remoteFile.get());
  if (fsyncRc < 0) {
    const auto details =
        smb2Details(session.context(), fsyncRc, QStringLiteral("fsync"));
    return smb::core::Result<bool>::failure(libsmb2Error(fsyncRc, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, secret));
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool>
Libsmb2SmbClient::copy(const smb::core::Connection &sourceConnection,
                       const smb::core::CredentialSecret *sourceSecret,
                       const QString &sourceRemotePath,
                       const smb::core::Connection &targetConnection,
                       const smb::core::CredentialSecret *targetSecret,
                       const QString &targetRemotePath,
                       const smb::core::OperationContext &context) {
  const auto validSource =
      validateConnectionInput(sourceConnection, sourceSecret, context);
  if (!validSource.ok()) {
    return validSource;
  }

  const auto validTarget =
      validateConnectionInput(targetConnection, targetSecret, context);
  if (!validTarget.ok()) {
    return validTarget;
  }

  Smb2Session sourceSession(m_timeoutSeconds);
  const auto sourceConnected = connectSession(sourceSession, sourceConnection,
                                              sourceSecret, m_sanitizer);
  if (!sourceConnected.ok()) {
    return sourceConnected;
  }

  if (isCancelled(context)) {
    return smb::core::Result<bool>::failure(cancelledError());
  }

  Smb2Session targetSession(m_timeoutSeconds);
  const auto targetConnected = connectSession(targetSession, targetConnection,
                                              targetSecret, m_sanitizer);
  if (!targetConnected.ok()) {
    return targetConnected;
  }

  const auto sourceBytes = normalizedRemotePath(sourceRemotePath).toUtf8();
  const auto targetBytes = normalizedRemotePath(targetRemotePath).toUtf8();

  smb2_stat_64 stat{};
  const auto statRc =
      smb2_stat(sourceSession.context(), sourceBytes.constData(), &stat);
  if (statRc < 0) {
    const auto details =
        smb2Details(sourceSession.context(), statRc, QStringLiteral("stat"));
    return smb::core::Result<bool>::failure(libsmb2Error(statRc, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, sourceSecret));
  }

  if (stat.smb2_type == SMB2_TYPE_DIRECTORY) {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::Unknown, smb::core::ErrorCategory::Smb,
        QStringLiteral("Directory copy is not supported yet."), false));
  }

  auto *rawSource =
      smb2_open(sourceSession.context(), sourceBytes.constData(), O_RDONLY);
  if (rawSource == nullptr) {
    const auto status = statusFromErrno();
    const auto details = smb2Details(sourceSession.context(), status,
                                     QStringLiteral("open_read"));
    return smb::core::Result<bool>::failure(libsmb2Error(status, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, sourceSecret));
  }
  Smb2FileHandle sourceFile(sourceSession.context(), rawSource);

  auto *rawTarget = smb2_open(targetSession.context(), targetBytes.constData(),
                              O_WRONLY | O_CREAT);
  if (rawTarget == nullptr) {
    const auto status = statusFromErrno();
    const auto details = smb2Details(targetSession.context(), status,
                                     QStringLiteral("open_write"));
    return smb::core::Result<bool>::failure(libsmb2Error(status, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, targetSecret));
  }
  Smb2FileHandle targetFile(targetSession.context(), rawTarget);

  const auto truncateRc =
      smb2_ftruncate(targetSession.context(), targetFile.get(), 0);
  if (truncateRc < 0) {
    const auto details = smb2Details(targetSession.context(), truncateRc,
                                     QStringLiteral("truncate"));
    return smb::core::Result<bool>::failure(
        libsmb2Error(truncateRc, details, Libsmb2ErrorContext::FileOperation,
                     m_sanitizer, targetSecret));
  }

  const auto totalSize = static_cast<qint64>(stat.smb2_size);
  QByteArray buffer(256 * 1024, '\0');
  qint64 offset = 0;
  while (true) {
    if (isCancelled(context)) {
      return smb::core::Result<bool>::failure(cancelledError());
    }

    const auto readRc = smb2_pread(sourceSession.context(), sourceFile.get(),
                                   reinterpret_cast<uint8_t *>(buffer.data()),
                                   static_cast<uint32_t>(buffer.size()),
                                   static_cast<uint64_t>(offset));
    if (readRc == -EAGAIN) {
      continue;
    }
    if (readRc < 0) {
      const auto details =
          smb2Details(sourceSession.context(), readRc, QStringLiteral("read"));
      return smb::core::Result<bool>::failure(libsmb2Error(readRc, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, sourceSecret));
    }
    if (readRc == 0) {
      break;
    }

    qint64 chunkOffset = 0;
    while (chunkOffset < readRc) {
      if (isCancelled(context)) {
        return smb::core::Result<bool>::failure(cancelledError());
      }

      const auto remaining = readRc - chunkOffset;
      const auto writeRc = smb2_pwrite(
          targetSession.context(), targetFile.get(),
          reinterpret_cast<const uint8_t *>(buffer.constData() + chunkOffset),
          static_cast<uint32_t>(remaining),
          static_cast<uint64_t>(offset + chunkOffset));
      if (writeRc == -EAGAIN) {
        continue;
      }
      if (writeRc <= 0) {
        const auto details = smb2Details(targetSession.context(), writeRc,
                                         QStringLiteral("write"));
        return smb::core::Result<bool>::failure(
            libsmb2Error(writeRc, details, Libsmb2ErrorContext::FileOperation,
                         m_sanitizer, targetSecret));
      }
      chunkOffset += writeRc;
    }

    offset += readRc;
    if (context.progressCallback) {
      context.progressCallback(smb::core::TransferProgress{offset, totalSize});
    }
  }

  const auto fsyncRc = smb2_fsync(targetSession.context(), targetFile.get());
  if (fsyncRc < 0) {
    const auto details =
        smb2Details(targetSession.context(), fsyncRc, QStringLiteral("fsync"));
    return smb::core::Result<bool>::failure(libsmb2Error(fsyncRc, details, Libsmb2ErrorContext::FileOperation, m_sanitizer, targetSecret));
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool>
Libsmb2SmbClient::move(const smb::core::Connection &sourceConnection,
                       const smb::core::CredentialSecret *sourceSecret,
                       const QString &sourceRemotePath,
                       const smb::core::Connection &targetConnection,
                       const smb::core::CredentialSecret *targetSecret,
                       const QString &targetRemotePath,
                       const smb::core::OperationContext &context) {
  if (sameShare(sourceConnection, targetConnection)) {
    return rename(sourceConnection, sourceSecret, sourceRemotePath,
                  targetRemotePath, context);
  }

  auto copied = copy(sourceConnection, sourceSecret, sourceRemotePath,
                     targetConnection, targetSecret, targetRemotePath, context);
  if (!copied.ok()) {
    return copied;
  }

  if (isCancelled(context)) {
    return smb::core::Result<bool>::failure(cancelledError());
  }

  return remove(sourceConnection, sourceSecret, sourceRemotePath, context);
}

} // namespace smb::infrastructure
