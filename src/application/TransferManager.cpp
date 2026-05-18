#include "application/TransferManager.h"

#include <utility>

namespace smb::application {

namespace {

const smb::core::CredentialSecret *
secretPtr(const std::optional<smb::core::CredentialSecret> &secret) {
  return secret.has_value() ? &secret.value() : nullptr;
}

} // namespace

TransferManager::TransferManager(OperationQueue &operationQueue,
                                 smb::core::SmbClient &smbClient)
    : m_operationQueue(operationQueue), m_smbClient(smbClient) {}

QString
TransferManager::downloadFile(smb::core::Connection connection,
                              std::optional<smb::core::CredentialSecret> secret,
                              QString remotePath, QString localPath) {
  return m_operationQueue.enqueue(
      QStringLiteral("download:%1").arg(remotePath),
      [this, connection = std::move(connection), secret = std::move(secret),
       remotePath = std::move(remotePath), localPath = std::move(localPath)](
          const smb::core::OperationContext &context) {
        return m_smbClient.downloadFile(connection, secretPtr(secret),
                                        remotePath, localPath, context);
      });
}

QString
TransferManager::uploadFile(smb::core::Connection connection,
                            std::optional<smb::core::CredentialSecret> secret,
                            QString localPath, QString remotePath) {
  return m_operationQueue.enqueue(
      QStringLiteral("upload:%1").arg(remotePath),
      [this, connection = std::move(connection), secret = std::move(secret),
       localPath = std::move(localPath), remotePath = std::move(remotePath)](
          const smb::core::OperationContext &context) {
        return m_smbClient.uploadFile(connection, secretPtr(secret), localPath,
                                      remotePath, context);
      });
}

QString
TransferManager::copy(smb::core::Connection sourceConnection,
                      std::optional<smb::core::CredentialSecret> sourceSecret,
                      QString sourceRemotePath,
                      smb::core::Connection targetConnection,
                      std::optional<smb::core::CredentialSecret> targetSecret,
                      QString targetRemotePath) {
  return m_operationQueue.enqueue(
      QStringLiteral("copy:%1").arg(sourceRemotePath),
      [this, sourceConnection = std::move(sourceConnection),
       sourceSecret = std::move(sourceSecret),
       sourceRemotePath = std::move(sourceRemotePath),
       targetConnection = std::move(targetConnection),
       targetSecret = std::move(targetSecret),
       targetRemotePath = std::move(targetRemotePath)](
          const smb::core::OperationContext &context) {
        return m_smbClient.copy(sourceConnection, secretPtr(sourceSecret),
                                sourceRemotePath, targetConnection,
                                secretPtr(targetSecret), targetRemotePath,
                                context);
      });
}

QString
TransferManager::move(smb::core::Connection sourceConnection,
                      std::optional<smb::core::CredentialSecret> sourceSecret,
                      QString sourceRemotePath,
                      smb::core::Connection targetConnection,
                      std::optional<smb::core::CredentialSecret> targetSecret,
                      QString targetRemotePath) {
  return m_operationQueue.enqueue(
      QStringLiteral("move:%1").arg(sourceRemotePath),
      [this, sourceConnection = std::move(sourceConnection),
       sourceSecret = std::move(sourceSecret),
       sourceRemotePath = std::move(sourceRemotePath),
       targetConnection = std::move(targetConnection),
       targetSecret = std::move(targetSecret),
       targetRemotePath = std::move(targetRemotePath)](
          const smb::core::OperationContext &context) {
        return m_smbClient.move(sourceConnection, secretPtr(sourceSecret),
                                sourceRemotePath, targetConnection,
                                secretPtr(targetSecret), targetRemotePath,
                                context);
      });
}

} // namespace smb::application
