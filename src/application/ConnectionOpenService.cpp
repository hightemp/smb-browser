#include "application/ConnectionOpenService.h"

#include <QDateTime>
#include <utility>

namespace smb::application {

namespace {

QString normalizedStartPath(const QString &path) {
  auto value = path.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
  while (value.startsWith(QLatin1Char('/'))) {
    value.remove(0, 1);
  }
  while (value.endsWith(QLatin1Char('/'))) {
    value.chop(1);
  }
  return value.isEmpty() ? QStringLiteral("/") : QStringLiteral("/") + value;
}

QString errorMessageForStorage(const smb::core::AppError &error) {
  if (!error.sanitizedTechnicalDetails.isEmpty()) {
    return error.sanitizedTechnicalDetails;
  }
  return error.userMessage;
}

} // namespace

ConnectionOpenService::ConnectionOpenService(
    smb::infrastructure::ConnectionRepository &repository,
    smb::core::CredentialStore &credentialStore,
    smb::core::SmbClient &smbClient)
    : m_repository(repository), m_credentialStore(credentialStore),
      m_smbClient(smbClient) {}

smb::core::Result<OpenConnectionResult>
ConnectionOpenService::open(const QString &connectionId,
                            const smb::core::OperationContext &context) {
  const auto loadedConnection = m_repository.getById(connectionId);
  if (!loadedConnection.ok()) {
    return smb::core::Result<OpenConnectionResult>::failure(
        loadedConnection.error());
  }

  return openAtPath(connectionId, loadedConnection.value().initialRemotePath,
                    true, context);
}

smb::core::Result<OpenConnectionResult> ConnectionOpenService::listDirectory(
    const QString &connectionId, const QString &remotePath,
    const smb::core::OperationContext &context) {
  return openAtPath(connectionId, remotePath, false, context);
}

smb::core::Result<bool> ConnectionOpenService::createDirectory(
    const QString &connectionId, const QString &remotePath,
    const smb::core::OperationContext &context) {
  const auto path = normalizedStartPath(remotePath);
  return runFileOperation(
      connectionId,
      [this, path](const smb::core::Connection &connection,
                   const smb::core::CredentialSecret *secret,
                   const smb::core::OperationContext &operationContext) {
        return m_smbClient.createDirectory(connection, secret, path,
                                           operationContext);
      },
      context);
}

smb::core::Result<bool>
ConnectionOpenService::remove(const QString &connectionId,
                              const QString &remotePath,
                              const smb::core::OperationContext &context) {
  const auto path = normalizedStartPath(remotePath);
  return runFileOperation(
      connectionId,
      [this, path](const smb::core::Connection &connection,
                   const smb::core::CredentialSecret *secret,
                   const smb::core::OperationContext &operationContext) {
        return m_smbClient.remove(connection, secret, path, operationContext);
      },
      context);
}

smb::core::Result<bool>
ConnectionOpenService::rename(const QString &connectionId,
                              const QString &sourceRemotePath,
                              const QString &targetRemotePath,
                              const smb::core::OperationContext &context) {
  const auto source = normalizedStartPath(sourceRemotePath);
  const auto target = normalizedStartPath(targetRemotePath);
  return runFileOperation(
      connectionId,
      [this, source,
       target](const smb::core::Connection &connection,
               const smb::core::CredentialSecret *secret,
               const smb::core::OperationContext &operationContext) {
        return m_smbClient.rename(connection, secret, source, target,
                                  operationContext);
      },
      context);
}

smb::core::Result<bool> ConnectionOpenService::downloadFile(
    const QString &connectionId, const QString &remotePath,
    const QString &localPath, const smb::core::OperationContext &context) {
  const auto path = normalizedStartPath(remotePath);
  return runFileOperation(
      connectionId,
      [this, path,
       localPath](const smb::core::Connection &connection,
                  const smb::core::CredentialSecret *secret,
                  const smb::core::OperationContext &operationContext) {
        return m_smbClient.downloadFile(connection, secret, path, localPath,
                                        operationContext);
      },
      context);
}

smb::core::Result<bool> ConnectionOpenService::uploadFile(
    const QString &connectionId, const QString &localPath,
    const QString &remotePath, const smb::core::OperationContext &context) {
  const auto path = normalizedStartPath(remotePath);
  return runFileOperation(
      connectionId,
      [this, localPath,
       path](const smb::core::Connection &connection,
             const smb::core::CredentialSecret *secret,
             const smb::core::OperationContext &operationContext) {
        return m_smbClient.uploadFile(connection, secret, localPath, path,
                                      operationContext);
      },
      context);
}

smb::core::Result<bool> ConnectionOpenService::copy(
    const QString &sourceConnectionId, const QString &sourceRemotePath,
    const QString &targetConnectionId, const QString &targetRemotePath,
    const smb::core::OperationContext &context) {
  auto source = loadConnectionWithSecret(sourceConnectionId);
  if (!source.ok()) {
    rememberError(sourceConnectionId, source.error());
    return smb::core::Result<bool>::failure(source.error());
  }

  auto target = loadConnectionWithSecret(targetConnectionId);
  if (!target.ok()) {
    rememberError(source.value().connection.id, target.error());
    return smb::core::Result<bool>::failure(target.error());
  }

  const auto sourcePath = normalizedStartPath(sourceRemotePath);
  const auto targetPath = normalizedStartPath(targetRemotePath);
  const auto *sourceSecret = source.value().secret.has_value()
                                 ? &source.value().secret.value()
                                 : nullptr;
  const auto *targetSecret = target.value().secret.has_value()
                                 ? &target.value().secret.value()
                                 : nullptr;
  auto copied = m_smbClient.copy(source.value().connection, sourceSecret,
                                 sourcePath, target.value().connection,
                                 targetSecret, targetPath, context);
  if (!copied.ok()) {
    rememberError(source.value().connection.id, copied.error());
    if (source.value().connection.id != target.value().connection.id) {
      rememberError(target.value().connection.id, copied.error());
    }
  }
  return copied;
}

smb::core::Result<bool> ConnectionOpenService::move(
    const QString &sourceConnectionId, const QString &sourceRemotePath,
    const QString &targetConnectionId, const QString &targetRemotePath,
    const smb::core::OperationContext &context) {
  auto source = loadConnectionWithSecret(sourceConnectionId);
  if (!source.ok()) {
    rememberError(sourceConnectionId, source.error());
    return smb::core::Result<bool>::failure(source.error());
  }

  auto target = loadConnectionWithSecret(targetConnectionId);
  if (!target.ok()) {
    rememberError(source.value().connection.id, target.error());
    return smb::core::Result<bool>::failure(target.error());
  }

  const auto sourcePath = normalizedStartPath(sourceRemotePath);
  const auto targetPath = normalizedStartPath(targetRemotePath);
  const auto *sourceSecret = source.value().secret.has_value()
                                 ? &source.value().secret.value()
                                 : nullptr;
  const auto *targetSecret = target.value().secret.has_value()
                                 ? &target.value().secret.value()
                                 : nullptr;
  auto moved = m_smbClient.move(source.value().connection, sourceSecret,
                                sourcePath, target.value().connection,
                                targetSecret, targetPath, context);
  if (!moved.ok()) {
    rememberError(source.value().connection.id, moved.error());
    if (source.value().connection.id != target.value().connection.id) {
      rememberError(target.value().connection.id, moved.error());
    }
  }
  return moved;
}

smb::core::Result<OpenConnectionResult> ConnectionOpenService::openAtPath(
    const QString &connectionId, const QString &remotePath,
    bool updateLastOpened, const smb::core::OperationContext &context) {
  const auto loadedConnection = m_repository.getById(connectionId);
  if (!loadedConnection.ok()) {
    return smb::core::Result<OpenConnectionResult>::failure(
        loadedConnection.error());
  }

  const auto &connection = loadedConnection.value();
  auto loadedSecret = loadSecret(connection);
  if (!loadedSecret.ok()) {
    rememberError(connection.id, loadedSecret.error());
    return smb::core::Result<OpenConnectionResult>::failure(
        loadedSecret.error());
  }

  auto secret = std::move(loadedSecret.value());
  const auto *secretPtr = secret.has_value() ? &secret.value() : nullptr;
  const auto currentPath = normalizedStartPath(remotePath);
  auto listed =
      m_smbClient.listDirectory(connection, secretPtr, currentPath, context);
  if (!listed.ok()) {
    rememberError(connection.id, listed.error());
    return smb::core::Result<OpenConnectionResult>::failure(listed.error());
  }

  if (updateLastOpened) {
    const auto openedAt = QDateTime::currentDateTimeUtc();
    auto updated = m_repository.updateLastOpened(connection.id, openedAt);
    if (!updated.ok()) {
      return smb::core::Result<OpenConnectionResult>::failure(updated.error());
    }
  }

  auto refreshedConnection = m_repository.getById(connection.id);
  if (!refreshedConnection.ok()) {
    return smb::core::Result<OpenConnectionResult>::failure(
        refreshedConnection.error());
  }

  OpenConnectionResult result;
  result.connection = refreshedConnection.value();
  result.currentRemotePath = currentPath;
  result.entries = std::move(listed.value());
  return smb::core::Result<OpenConnectionResult>::success(std::move(result));
}

smb::core::Result<bool> ConnectionOpenService::runFileOperation(
    const QString &connectionId, FileOperation operation,
    const smb::core::OperationContext &context) {
  const auto loadedConnection = m_repository.getById(connectionId);
  if (!loadedConnection.ok()) {
    return smb::core::Result<bool>::failure(loadedConnection.error());
  }

  const auto &connection = loadedConnection.value();
  auto loadedSecret = loadSecret(connection);
  if (!loadedSecret.ok()) {
    rememberError(connection.id, loadedSecret.error());
    return smb::core::Result<bool>::failure(loadedSecret.error());
  }

  auto secret = std::move(loadedSecret.value());
  const auto *secretPtr = secret.has_value() ? &secret.value() : nullptr;
  auto result = operation(connection, secretPtr, context);
  if (!result.ok()) {
    rememberError(connection.id, result.error());
  }
  return result;
}

smb::core::Result<ConnectionOpenService::ConnectionWithSecret>
ConnectionOpenService::loadConnectionWithSecret(
    const QString &connectionId) const {
  auto loadedConnection = m_repository.getById(connectionId);
  if (!loadedConnection.ok()) {
    return smb::core::Result<ConnectionWithSecret>::failure(
        loadedConnection.error());
  }

  auto loadedSecret = loadSecret(loadedConnection.value());
  if (!loadedSecret.ok()) {
    return smb::core::Result<ConnectionWithSecret>::failure(
        loadedSecret.error());
  }

  ConnectionWithSecret result;
  result.connection = loadedConnection.value();
  result.secret = std::move(loadedSecret.value());
  return smb::core::Result<ConnectionWithSecret>::success(std::move(result));
}

smb::core::Result<std::optional<smb::core::CredentialSecret>>
ConnectionOpenService::loadSecret(
    const smb::core::Connection &connection) const {
  if (!connection.usesStoredCredential()) {
    return smb::core::Result<
        std::optional<smb::core::CredentialSecret>>::success(std::nullopt);
  }

  auto secret = m_credentialStore.load(connection.credentialRef);
  if (!secret.ok()) {
    return smb::core::Result<
        std::optional<smb::core::CredentialSecret>>::failure(secret.error());
  }

  return smb::core::Result<std::optional<smb::core::CredentialSecret>>::success(
      std::move(secret.value()));
}

void ConnectionOpenService::rememberError(const QString &connectionId,
                                          const smb::core::AppError &error) {
  m_repository.updateLastError(connectionId, error.code,
                               errorMessageForStorage(error));
}

} // namespace smb::application
