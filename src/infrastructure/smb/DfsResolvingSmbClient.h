#pragma once

#include "core/DfsReferralResolver.h"
#include "core/SmbClient.h"

#include <QHash>
#include <QMutex>
#include <optional>

namespace smb::infrastructure {

class DfsResolvingSmbClient final : public smb::core::SmbClient {
public:
  DfsResolvingSmbClient(smb::core::SmbClient &delegate,
                        smb::core::DfsReferralResolver &resolver);

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
  template <typename T, typename Operation>
  smb::core::Result<T>
  runWithDfsFallback(const smb::core::Connection &connection,
                     const smb::core::CredentialSecret *secret,
                     const smb::core::OperationContext &context,
                     Operation operation);

  std::optional<smb::core::Connection>
  cachedResolvedConnection(const smb::core::Connection &connection) const;
  std::optional<smb::core::Connection>
  resolveAndCache(const smb::core::Connection &connection,
                  const smb::core::CredentialSecret *secret,
                  const smb::core::OperationContext &context);
  void forgetCachedConnection(const smb::core::Connection &connection);

  smb::core::SmbClient &m_delegate;
  smb::core::DfsReferralResolver &m_resolver;
  mutable QMutex m_cacheMutex;
  QHash<QString, smb::core::Connection> m_resolvedConnections;
};

} // namespace smb::infrastructure
