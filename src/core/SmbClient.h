#pragma once

#include "core/Connection.h"
#include "core/CredentialStore.h"
#include "core/Error.h"
#include "core/RemoteFileEntry.h"

#include <QVector>
#include <atomic>
#include <functional>
#include <utility>

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

struct SmbClientCapabilities {
  bool canListDirectory = true;
  bool canCreateDirectory = true;
  bool canRemove = true;
  bool canRename = true;
  bool canDownload = true;
  bool canUpload = true;
  bool canCopy = true;
  bool canMove = true;
  bool canSetBasicMetadata = false;
  bool canReadExtendedAttributes = false;
  bool canWriteExtendedAttributes = false;
  bool canReadSecurityDescriptor = false;
  bool canWriteSecurityDescriptor = false;
  bool canWatchDirectory = false;
  bool canUsePosixMode = false;
  bool canUsePosixOwner = false;
  bool canBrowseShares = false;
  QString unsupportedAdvancedOperationsReason;
};

struct SmbCapabilityReport {
  bool serverAvailable = false;
  bool shareAvailable = false;
  QString dialect;
  bool signingRequired = false;
  bool encryptionSupported = false;
  bool encryptionRequired = false;
  bool dfsSupported = false;
  bool shareDfs = false;
  bool shareDfsRoot = false;
  SmbClientCapabilities capabilities;
};

struct SmbShareInfo {
  QString name;
  QString type;
  QString comment;
  bool hidden = false;
  bool dfs = false;
};

class SmbClient {
public:
  virtual ~SmbClient() = default;

  virtual SmbClientCapabilities
  capabilities(const Connection &connection) const {
    (void)connection;
    return {};
  }

  virtual Result<SmbCapabilityReport>
  probeCapabilities(const Connection &connection,
                    const CredentialSecret *secret,
                    const OperationContext &context) {
    (void)secret;
    (void)context;
    SmbCapabilityReport report;
    report.capabilities = capabilities(connection);
    return Result<SmbCapabilityReport>::success(std::move(report));
  }

  virtual Result<QVector<SmbShareInfo>>
  listShares(const Connection &connection, const CredentialSecret *secret,
             const OperationContext &context) {
    (void)connection;
    (void)secret;
    (void)context;
    return Result<QVector<SmbShareInfo>>::failure(AppError::fromCode(
        ErrorCode::ProtocolUnsupported, ErrorCategory::Smb,
        QStringLiteral("Share browsing is not supported by this SMB backend."),
        false));
  }

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
