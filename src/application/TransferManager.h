#pragma once

#include "application/OperationQueue.h"
#include "core/SmbClient.h"

#include <optional>

namespace smb::application {

class TransferManager {
public:
  TransferManager(OperationQueue &operationQueue,
                  smb::core::SmbClient &smbClient);

  QString downloadFile(smb::core::Connection connection,
                       std::optional<smb::core::CredentialSecret> secret,
                       QString remotePath, QString localPath);
  QString uploadFile(smb::core::Connection connection,
                     std::optional<smb::core::CredentialSecret> secret,
                     QString localPath, QString remotePath);
  QString copy(smb::core::Connection sourceConnection,
               std::optional<smb::core::CredentialSecret> sourceSecret,
               QString sourceRemotePath, smb::core::Connection targetConnection,
               std::optional<smb::core::CredentialSecret> targetSecret,
               QString targetRemotePath);
  QString move(smb::core::Connection sourceConnection,
               std::optional<smb::core::CredentialSecret> sourceSecret,
               QString sourceRemotePath, smb::core::Connection targetConnection,
               std::optional<smb::core::CredentialSecret> targetSecret,
               QString targetRemotePath);

private:
  OperationQueue &m_operationQueue;
  smb::core::SmbClient &m_smbClient;
};

} // namespace smb::application
