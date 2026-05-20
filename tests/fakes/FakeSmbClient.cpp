#include "fakes/FakeSmbClient.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <utility>

namespace smb::tests {

namespace {

smb::core::AppError smbError(smb::core::ErrorCode code,
                             const QString &details) {
  return smb::core::AppError::fromCode(code, smb::core::ErrorCategory::Smb,
                                       details, false);
}

bool isCancelled(const smb::core::OperationContext &context) {
  return context.cancellationToken != nullptr &&
         context.cancellationToken->isCancellationRequested();
}

QString parentPath(const QString &path) {
  const auto index = path.lastIndexOf(QLatin1Char('/'));
  if (index <= 0) {
    return QStringLiteral("/");
  }
  return path.left(index);
}

QString fileName(const QString &path) {
  const auto index = path.lastIndexOf(QLatin1Char('/'));
  if (index < 0) {
    return path;
  }
  return path.mid(index + 1);
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

} // namespace

void FakeSmbClient::setRequirePassword(bool requirePassword) {
  m_requirePassword = requirePassword;
}

void FakeSmbClient::setExpectedSecret(QByteArray expectedSecret) {
  m_expectedSecret = std::move(expectedSecret);
}

void FakeSmbClient::setCapabilities(
    smb::core::SmbClientCapabilities capabilities) {
  m_capabilities = std::move(capabilities);
}

void FakeSmbClient::addShare(smb::core::SmbShareInfo share) {
  m_capabilities.canBrowseShares = true;
  m_shares.push_back(std::move(share));
}

void FakeSmbClient::failOperation(FakeSmbOperation operation,
                                  smb::core::ErrorCode code) {
  m_failures.insert(static_cast<int>(operation), code);
}

void FakeSmbClient::clearFailures() { m_failures.clear(); }

void FakeSmbClient::addDirectory(const QString &remotePath) {
  const auto path = normalizePath(remotePath);
  m_nodes.insert(path, Node{smb::core::RemoteFileType::Directory, {}});
  if (path != QStringLiteral("/")) {
    addDirectory(parentPath(path));
  }
}

void FakeSmbClient::addFile(const QString &remotePath, QByteArray content) {
  const auto path = normalizePath(remotePath);
  addDirectory(parentPath(path));
  m_nodes.insert(path,
                 Node{smb::core::RemoteFileType::File, std::move(content)});
}

void FakeSmbClient::addSymlink(const QString &remotePath) {
  const auto path = normalizePath(remotePath);
  addDirectory(parentPath(path));
  m_nodes.insert(path, Node{smb::core::RemoteFileType::Symlink, {}});
}

smb::core::Result<bool>
FakeSmbClient::checkConnection(const smb::core::Connection &,
                               const smb::core::CredentialSecret *secret,
                               const smb::core::OperationContext &context) {
  const auto error =
      preflight(FakeSmbOperation::CheckConnection, secret, context);
  if (error.hasError()) {
    return smb::core::Result<bool>::failure(error);
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::SmbClientCapabilities
FakeSmbClient::capabilities(const smb::core::Connection &connection) const {
  (void)connection;
  return m_capabilities;
}

smb::core::Result<QVector<smb::core::SmbShareInfo>>
FakeSmbClient::listShares(const smb::core::Connection &,
                          const smb::core::CredentialSecret *secret,
                          const smb::core::OperationContext &context) {
  const auto error = preflight(FakeSmbOperation::ListShares, secret, context);
  if (error.hasError()) {
    return smb::core::Result<QVector<smb::core::SmbShareInfo>>::failure(error);
  }

  return smb::core::Result<QVector<smb::core::SmbShareInfo>>::success(m_shares);
}

smb::core::Result<QVector<smb::core::RemoteFileEntry>>
FakeSmbClient::listDirectory(const smb::core::Connection &,
                             const smb::core::CredentialSecret *secret,
                             const QString &remotePath,
                             const smb::core::OperationContext &context) {
  const auto error =
      preflight(FakeSmbOperation::ListDirectory, secret, context);
  if (error.hasError()) {
    return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::failure(
        error);
  }

  const auto path = normalizePath(remotePath);
  if (!m_nodes.contains(path) ||
      m_nodes.value(path).type != smb::core::RemoteFileType::Directory) {
    return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::failure(
        smbError(smb::core::ErrorCode::FileNotFound,
                 QStringLiteral("Remote directory was not found.")));
  }

  QVector<smb::core::RemoteFileEntry> entries;
  QSet<QString> seen;
  for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
    if (it.key() == path || parentPath(it.key()) != path ||
        seen.contains(it.key())) {
      continue;
    }
    seen.insert(it.key());

    smb::core::RemoteFileEntry entry;
    entry.name = fileName(it.key());
    entry.remotePath = it.key();
    entry.type = it.value().type;
    entry.size = it.value().content.size();
    entries.push_back(entry);
  }

  std::sort(entries.begin(), entries.end(),
            [](const auto &left, const auto &right) {
              if (left.type != right.type) {
                return left.type == smb::core::RemoteFileType::Directory;
              }
              return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
            });

  return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::success(
      std::move(entries));
}

smb::core::Result<bool> FakeSmbClient::createDirectory(
    const smb::core::Connection &, const smb::core::CredentialSecret *secret,
    const QString &remotePath, const smb::core::OperationContext &context) {
  const auto error =
      preflight(FakeSmbOperation::CreateDirectory, secret, context);
  if (error.hasError()) {
    return smb::core::Result<bool>::failure(error);
  }

  const auto path = normalizePath(remotePath);
  if (m_nodes.contains(path)) {
    return smb::core::Result<bool>::failure(
        smbError(smb::core::ErrorCode::AlreadyExists,
                 QStringLiteral("Remote path already exists.")));
  }
  if (!m_nodes.contains(parentPath(path))) {
    return smb::core::Result<bool>::failure(
        smbError(smb::core::ErrorCode::FileNotFound,
                 QStringLiteral("Parent directory was not found.")));
  }

  m_nodes.insert(path, Node{smb::core::RemoteFileType::Directory, {}});
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> FakeSmbClient::remove(
    const smb::core::Connection &, const smb::core::CredentialSecret *secret,
    const QString &remotePath, const smb::core::OperationContext &context) {
  const auto error = preflight(FakeSmbOperation::Remove, secret, context);
  if (error.hasError()) {
    return smb::core::Result<bool>::failure(error);
  }

  const auto path = normalizePath(remotePath);
  if (!m_nodes.contains(path)) {
    return smb::core::Result<bool>::failure(
        smbError(smb::core::ErrorCode::FileNotFound,
                 QStringLiteral("Remote path was not found.")));
  }
  for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it) {
    if (it.key() != path && parentPath(it.key()) == path) {
      return smb::core::Result<bool>::failure(
          smbError(smb::core::ErrorCode::DirectoryNotEmpty,
                   QStringLiteral("Remote directory is not empty.")));
    }
  }

  m_nodes.remove(path);
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> FakeSmbClient::rename(
    const smb::core::Connection &, const smb::core::CredentialSecret *secret,
    const QString &sourceRemotePath, const QString &targetRemotePath,
    const smb::core::OperationContext &context) {
  const auto error = preflight(FakeSmbOperation::Rename, secret, context);
  if (error.hasError()) {
    return smb::core::Result<bool>::failure(error);
  }

  const auto source = normalizePath(sourceRemotePath);
  const auto target = normalizePath(targetRemotePath);
  if (!m_nodes.contains(source)) {
    return smb::core::Result<bool>::failure(
        smbError(smb::core::ErrorCode::FileNotFound,
                 QStringLiteral("Source path was not found.")));
  }
  if (m_nodes.contains(target)) {
    return smb::core::Result<bool>::failure(
        smbError(smb::core::ErrorCode::AlreadyExists,
                 QStringLiteral("Target path already exists.")));
  }

  m_nodes.insert(target, m_nodes.take(source));
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool>
FakeSmbClient::downloadFile(const smb::core::Connection &,
                            const smb::core::CredentialSecret *secret,
                            const QString &remotePath, const QString &localPath,
                            const smb::core::OperationContext &context) {
  const auto error = preflight(FakeSmbOperation::Download, secret, context);
  if (error.hasError()) {
    return smb::core::Result<bool>::failure(error);
  }

  const auto path = normalizePath(remotePath);
  if (!m_nodes.contains(path) ||
      m_nodes.value(path).type != smb::core::RemoteFileType::File) {
    return smb::core::Result<bool>::failure(
        smbError(smb::core::ErrorCode::FileNotFound,
                 QStringLiteral("Remote file was not found.")));
  }

  QFile file(localPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::LocalIoError, smb::core::ErrorCategory::Transfer,
        QStringLiteral("Unable to write local file.")));
  }

  const auto content = m_nodes.value(path).content;
  file.write(content);
  if (context.progressCallback) {
    context.progressCallback(
        smb::core::TransferProgress{content.size(), content.size()});
  }
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool>
FakeSmbClient::uploadFile(const smb::core::Connection &,
                          const smb::core::CredentialSecret *secret,
                          const QString &localPath, const QString &remotePath,
                          const smb::core::OperationContext &context) {
  const auto error = preflight(FakeSmbOperation::Upload, secret, context);
  if (error.hasError()) {
    return smb::core::Result<bool>::failure(error);
  }

  QFile file(localPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::LocalIoError, smb::core::ErrorCategory::Transfer,
        QStringLiteral("Unable to read local file.")));
  }

  const auto content = file.readAll();
  addFile(remotePath, content);
  if (context.progressCallback) {
    context.progressCallback(
        smb::core::TransferProgress{content.size(), content.size()});
  }
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool>
FakeSmbClient::copy(const smb::core::Connection &,
                    const smb::core::CredentialSecret *sourceSecret,
                    const QString &sourceRemotePath,
                    const smb::core::Connection &,
                    const smb::core::CredentialSecret *targetSecret,
                    const QString &targetRemotePath,
                    const smb::core::OperationContext &context) {
  const auto error = preflight(FakeSmbOperation::Copy, sourceSecret, context);
  if (error.hasError()) {
    return smb::core::Result<bool>::failure(error);
  }
  const auto targetError =
      preflight(FakeSmbOperation::Copy, targetSecret, context);
  if (targetError.hasError()) {
    return smb::core::Result<bool>::failure(targetError);
  }

  const auto source = normalizePath(sourceRemotePath);
  if (!m_nodes.contains(source) ||
      m_nodes.value(source).type != smb::core::RemoteFileType::File) {
    return smb::core::Result<bool>::failure(
        smbError(smb::core::ErrorCode::FileNotFound,
                 QStringLiteral("Source file was not found.")));
  }

  addFile(targetRemotePath, m_nodes.value(source).content);
  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool>
FakeSmbClient::move(const smb::core::Connection &sourceConnection,
                    const smb::core::CredentialSecret *sourceSecret,
                    const QString &sourceRemotePath,
                    const smb::core::Connection &targetConnection,
                    const smb::core::CredentialSecret *targetSecret,
                    const QString &targetRemotePath,
                    const smb::core::OperationContext &context) {
  const auto error = preflight(FakeSmbOperation::Move, sourceSecret, context);
  if (error.hasError()) {
    return smb::core::Result<bool>::failure(error);
  }

  if (sameRemoteTree(sourceConnection, targetConnection)) {
    return rename(sourceConnection, sourceSecret, sourceRemotePath,
                  targetRemotePath, context);
  }

  const auto targetError =
      preflight(FakeSmbOperation::Move, targetSecret, context);
  if (targetError.hasError()) {
    return smb::core::Result<bool>::failure(targetError);
  }

  auto copied = copy(sourceConnection, sourceSecret, sourceRemotePath,
                     targetConnection, targetSecret, targetRemotePath, context);
  if (!copied.ok()) {
    return copied;
  }

  return remove(sourceConnection, sourceSecret, sourceRemotePath, context);
}

smb::core::AppError
FakeSmbClient::preflight(FakeSmbOperation operation,
                         const smb::core::CredentialSecret *secret,
                         const smb::core::OperationContext &context) const {
  if (isCancelled(context)) {
    return smbError(smb::core::ErrorCode::OperationCancelled,
                    QStringLiteral("Operation was cancelled."));
  }

  const auto failure = operationFailure(operation);
  if (failure.hasError()) {
    return failure;
  }

  if (m_requirePassword &&
      (secret == nullptr || secret->bytes != m_expectedSecret)) {
    return smbError(smb::core::ErrorCode::AuthenticationFailed,
                    QStringLiteral("Authentication failed."));
  }

  return smb::core::AppError::none();
}

smb::core::AppError
FakeSmbClient::operationFailure(FakeSmbOperation operation) const {
  const auto key = static_cast<int>(operation);
  if (!m_failures.contains(key)) {
    return smb::core::AppError::none();
  }

  return smbError(m_failures.value(key),
                  QStringLiteral("Fake SMB operation failed."));
}

QString FakeSmbClient::normalizePath(const QString &remotePath) {
  auto path = remotePath.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (path.isEmpty()) {
    return QStringLiteral("/");
  }
  if (!path.startsWith(QLatin1Char('/'))) {
    path.prepend(QLatin1Char('/'));
  }
  while (path.size() > 1 && path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  return path;
}

} // namespace smb::tests
