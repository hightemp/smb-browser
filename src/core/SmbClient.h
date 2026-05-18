#pragma once

#include "core/Connection.h"
#include "core/CredentialStore.h"
#include "core/Error.h"
#include "core/RemoteFileEntry.h"

#include <QVector>
#include <atomic>
#include <functional>

namespace smb::core {

class CancellationToken {
public:
  void cancel() { m_cancelled.store(true); }

  bool isCancellationRequested() const { return m_cancelled.load(); }

private:
  std::atomic_bool m_cancelled = false;
};

struct TransferProgress {
  qint64 bytesTransferred = 0;
  qint64 totalBytes = 0;
};

using ProgressCallback = std::function<void(const TransferProgress &)>;

struct OperationContext {
  CancellationToken *cancellationToken = nullptr;
  ProgressCallback progressCallback;
};

class SmbClient {
public:
  virtual ~SmbClient() = default;

  virtual Result<bool> checkConnection(const Connection &connection,
                                       const CredentialSecret *secret,
                                       const OperationContext &context) = 0;
  virtual Result<QVector<RemoteFileEntry>>
  listDirectory(const Connection &connection, const CredentialSecret *secret,
                const QString &remotePath, const OperationContext &context) = 0;
  virtual Result<bool> createDirectory(const Connection &connection,
                                       const CredentialSecret *secret,
                                       const QString &remotePath,
                                       const OperationContext &context) = 0;
  virtual Result<bool> remove(const Connection &connection,
                              const CredentialSecret *secret,
                              const QString &remotePath,
                              const OperationContext &context) = 0;
  virtual Result<bool> rename(const Connection &connection,
                              const CredentialSecret *secret,
                              const QString &sourceRemotePath,
                              const QString &targetRemotePath,
                              const OperationContext &context) = 0;
  virtual Result<bool> downloadFile(const Connection &connection,
                                    const CredentialSecret *secret,
                                    const QString &remotePath,
                                    const QString &localPath,
                                    const OperationContext &context) = 0;
  virtual Result<bool> uploadFile(const Connection &connection,
                                  const CredentialSecret *secret,
                                  const QString &localPath,
                                  const QString &remotePath,
                                  const OperationContext &context) = 0;
  virtual Result<bool>
  copy(const Connection &sourceConnection, const CredentialSecret *sourceSecret,
       const QString &sourceRemotePath, const Connection &targetConnection,
       const CredentialSecret *targetSecret, const QString &targetRemotePath,
       const OperationContext &context) = 0;
  virtual Result<bool>
  move(const Connection &sourceConnection, const CredentialSecret *sourceSecret,
       const QString &sourceRemotePath, const Connection &targetConnection,
       const CredentialSecret *targetSecret, const QString &targetRemotePath,
       const OperationContext &context) = 0;
};

} // namespace smb::core
