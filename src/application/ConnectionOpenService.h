#pragma once

#include "core/CredentialStore.h"
#include "core/RemoteFileEntry.h"
#include "core/SmbClient.h"
#include "storage/ConnectionRepository.h"

#include <functional>
#include <optional>

namespace smb::application {

struct OpenConnectionResult {
  smb::core::Connection connection;
  QString currentRemotePath;
  QVector<smb::core::RemoteFileEntry> entries;
};

class ConnectionOpenUseCase {
public:
  virtual ~ConnectionOpenUseCase() = default;

  virtual smb::core::Result<OpenConnectionResult>
  open(const QString &connectionId,
       const smb::core::OperationContext &context = {}) = 0;
};

class RemoteDirectoryUseCase {
public:
  virtual ~RemoteDirectoryUseCase() = default;

  virtual smb::core::Result<OpenConnectionResult>
  listDirectory(const QString &connectionId, const QString &remotePath,
                const smb::core::OperationContext &context = {}) = 0;
};

class RemoteFileOperationUseCase {
public:
  virtual ~RemoteFileOperationUseCase() = default;

  virtual smb::core::Result<bool>
  createDirectory(const QString &connectionId, const QString &remotePath,
                  const smb::core::OperationContext &context = {}) = 0;
  virtual smb::core::Result<bool>
  remove(const QString &connectionId, const QString &remotePath,
         const smb::core::OperationContext &context = {}) = 0;
  virtual smb::core::Result<bool>
  rename(const QString &connectionId, const QString &sourceRemotePath,
         const QString &targetRemotePath,
         const smb::core::OperationContext &context = {}) = 0;
};

class RemoteFileTransferUseCase {
public:
  virtual ~RemoteFileTransferUseCase() = default;

  virtual smb::core::Result<bool>
  downloadFile(const QString &connectionId, const QString &remotePath,
               const QString &localPath,
               const smb::core::OperationContext &context = {}) = 0;
  virtual smb::core::Result<bool>
  uploadFile(const QString &connectionId, const QString &localPath,
             const QString &remotePath,
             const smb::core::OperationContext &context = {}) = 0;
  virtual smb::core::Result<bool>
  copy(const QString &sourceConnectionId, const QString &sourceRemotePath,
       const QString &targetConnectionId, const QString &targetRemotePath,
       const smb::core::OperationContext &context = {}) = 0;
  virtual smb::core::Result<bool>
  move(const QString &sourceConnectionId, const QString &sourceRemotePath,
       const QString &targetConnectionId, const QString &targetRemotePath,
       const smb::core::OperationContext &context = {}) = 0;
};

class ConnectionOpenService final : public ConnectionOpenUseCase,
                                    public RemoteDirectoryUseCase,
                                    public RemoteFileOperationUseCase,
                                    public RemoteFileTransferUseCase {
public:
  ConnectionOpenService(smb::infrastructure::ConnectionRepository &repository,
                        smb::core::CredentialStore &credentialStore,
                        smb::core::SmbClient &smbClient);

  smb::core::Result<OpenConnectionResult>
  open(const QString &connectionId,
       const smb::core::OperationContext &context = {}) override;
  smb::core::Result<OpenConnectionResult>
  listDirectory(const QString &connectionId, const QString &remotePath,
                const smb::core::OperationContext &context = {}) override;
  smb::core::Result<bool>
  createDirectory(const QString &connectionId, const QString &remotePath,
                  const smb::core::OperationContext &context = {}) override;
  smb::core::Result<bool>
  remove(const QString &connectionId, const QString &remotePath,
         const smb::core::OperationContext &context = {}) override;
  smb::core::Result<bool>
  rename(const QString &connectionId, const QString &sourceRemotePath,
         const QString &targetRemotePath,
         const smb::core::OperationContext &context = {}) override;
  smb::core::Result<bool>
  downloadFile(const QString &connectionId, const QString &remotePath,
               const QString &localPath,
               const smb::core::OperationContext &context = {}) override;
  smb::core::Result<bool>
  uploadFile(const QString &connectionId, const QString &localPath,
             const QString &remotePath,
             const smb::core::OperationContext &context = {}) override;
  smb::core::Result<bool>
  copy(const QString &sourceConnectionId, const QString &sourceRemotePath,
       const QString &targetConnectionId, const QString &targetRemotePath,
       const smb::core::OperationContext &context = {}) override;
  smb::core::Result<bool>
  move(const QString &sourceConnectionId, const QString &sourceRemotePath,
       const QString &targetConnectionId, const QString &targetRemotePath,
       const smb::core::OperationContext &context = {}) override;

private:
  struct ConnectionWithSecret {
    smb::core::Connection connection;
    std::optional<smb::core::CredentialSecret> secret;
  };

  using FileOperation = std::function<smb::core::Result<bool>(
      const smb::core::Connection &, const smb::core::CredentialSecret *,
      const smb::core::OperationContext &)>;

  smb::core::Result<OpenConnectionResult>
  openAtPath(const QString &connectionId, const QString &remotePath,
             bool updateLastOpened, const smb::core::OperationContext &context);
  smb::core::Result<bool>
  runFileOperation(const QString &connectionId, FileOperation operation,
                   const smb::core::OperationContext &context);
  smb::core::Result<ConnectionWithSecret>
  loadConnectionWithSecret(const QString &connectionId) const;
  smb::core::Result<std::optional<smb::core::CredentialSecret>>
  loadSecret(const smb::core::Connection &connection) const;
  void rememberError(const QString &connectionId,
                     const smb::core::AppError &error);

  smb::infrastructure::ConnectionRepository &m_repository;
  smb::core::CredentialStore &m_credentialStore;
  smb::core::SmbClient &m_smbClient;
};

} // namespace smb::application

Q_DECLARE_METATYPE(smb::application::OpenConnectionResult)
