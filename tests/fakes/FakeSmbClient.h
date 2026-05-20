#pragma once

#include "core/SmbClient.h"

#include <QByteArray>
#include <QHash>

namespace smb::tests {

enum class FakeSmbOperation {
  CheckConnection,
  ListDirectory,
  CreateDirectory,
  Remove,
  Rename,
  Download,
  Upload,
  Copy,
  Move,
};

class FakeSmbClient final : public smb::core::SmbClient {
public:
  void setRequirePassword(bool requirePassword);
  void setExpectedSecret(QByteArray expectedSecret);
  void failOperation(FakeSmbOperation operation, smb::core::ErrorCode code);
  void clearFailures();

  void addDirectory(const QString &remotePath);
  void addFile(const QString &remotePath, QByteArray content);
  void addSymlink(const QString &remotePath);

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
  struct Node {
    smb::core::RemoteFileType type = smb::core::RemoteFileType::Unknown;
    QByteArray content;
  };

  smb::core::AppError
  preflight(FakeSmbOperation operation,
            const smb::core::CredentialSecret *secret,
            const smb::core::OperationContext &context) const;
  smb::core::AppError operationFailure(FakeSmbOperation operation) const;
  static QString normalizePath(const QString &remotePath);

  QHash<QString, Node> m_nodes;
  QHash<int, smb::core::ErrorCode> m_failures;
  bool m_requirePassword = false;
  QByteArray m_expectedSecret;
};

} // namespace smb::tests
