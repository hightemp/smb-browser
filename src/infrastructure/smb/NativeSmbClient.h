#pragma once

#include "core/LogSanitizer.h"
#include "core/SmbClient.h"

namespace smb::infrastructure {

class NativeSmbClient final : public smb::core::SmbClient {
public:
  explicit NativeSmbClient(int timeoutSeconds = 15,
                           smb::core::LogSanitizer sanitizer = {});

  smb::core::Result<bool>
  checkConnection(const smb::core::Connection &connection,
                  const smb::core::CredentialSecret *secret,
                  const smb::core::OperationContext &context) override;

  smb::core::Result<QVector<smb::core::RemoteFileEntry>>
  listDirectory(const smb::core::Connection &connection,
                const smb::core::CredentialSecret *secret,
                const QString &remotePath,
                const smb::core::OperationContext &context) override;

  smb::core::Result<bool>
  createDirectory(const smb::core::Connection &connection,
                  const smb::core::CredentialSecret *secret,
                  const QString &remotePath,
                  const smb::core::OperationContext &context) override;

  smb::core::Result<bool>
  remove(const smb::core::Connection &connection,
         const smb::core::CredentialSecret *secret, const QString &remotePath,
         const smb::core::OperationContext &context) override;

  smb::core::Result<bool>
  rename(const smb::core::Connection &connection,
         const smb::core::CredentialSecret *secret,
         const QString &sourceRemotePath, const QString &targetRemotePath,
         const smb::core::OperationContext &context) override;

  smb::core::Result<bool>
  downloadFile(const smb::core::Connection &connection,
               const smb::core::CredentialSecret *secret,
               const QString &remotePath, const QString &localPath,
               const smb::core::OperationContext &context) override;

  smb::core::Result<bool>
  uploadFile(const smb::core::Connection &connection,
             const smb::core::CredentialSecret *secret,
             const QString &localPath, const QString &remotePath,
             const smb::core::OperationContext &context) override;

  smb::core::Result<bool>
  copy(const smb::core::Connection &sourceConnection,
       const smb::core::CredentialSecret *sourceSecret,
       const QString &sourceRemotePath,
       const smb::core::Connection &targetConnection,
       const smb::core::CredentialSecret *targetSecret,
       const QString &targetRemotePath,
       const smb::core::OperationContext &context) override;

  smb::core::Result<bool>
  move(const smb::core::Connection &sourceConnection,
       const smb::core::CredentialSecret *sourceSecret,
       const QString &sourceRemotePath,
       const smb::core::Connection &targetConnection,
       const smb::core::CredentialSecret *targetSecret,
       const QString &targetRemotePath,
       const smb::core::OperationContext &context) override;

private:
  int m_timeoutSeconds = 15;
  smb::core::LogSanitizer m_sanitizer;
};

} // namespace smb::infrastructure
