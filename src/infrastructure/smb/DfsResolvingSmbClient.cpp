#include "smb/DfsResolvingSmbClient.h"

#include <QMutexLocker>

#include <utility>

namespace smb::infrastructure {

namespace {

QString cacheKey(const smb::core::Connection &connection) {
  return QStringLiteral("%1\n%2\n%3\n%4\n%5")
      .arg(connection.server.toLower(), connection.share.toLower(),
           connection.domain.toLower(), connection.username.toLower(),
           smb::core::toString(connection.authType));
}

bool looksLikeDfsReferralFailure(const smb::core::AppError &error) {
  if (error.code != smb::core::ErrorCode::ShareUnavailable) {
    return false;
  }

  const auto details = error.sanitizedTechnicalDetails.toLower();
  return details.contains(QStringLiteral("bad network name")) ||
         details.contains(QStringLiteral("bad_network_name")) ||
         details.contains(QStringLiteral("status_bad_network_name")) ||
         details.contains(QStringLiteral("tree connect"));
}

} // namespace

DfsResolvingSmbClient::DfsResolvingSmbClient(
    smb::core::SmbClient &delegate, smb::core::DfsReferralResolver &resolver)
    : m_delegate(delegate), m_resolver(resolver) {}

template <typename T, typename Operation>
smb::core::Result<T> DfsResolvingSmbClient::runWithDfsFallback(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context, Operation operation) {
  if (const auto cached = cachedResolvedConnection(connection);
      cached.has_value()) {
    auto cachedResult = operation(cached.value());
    if (cachedResult.ok() ||
        !looksLikeDfsReferralFailure(cachedResult.error())) {
      return cachedResult;
    }
    forgetCachedConnection(connection);
  }

  auto result = operation(connection);
  if (result.ok() || !looksLikeDfsReferralFailure(result.error())) {
    return result;
  }

  const auto resolved = resolveAndCache(connection, secret, context);
  if (!resolved.has_value()) {
    return result;
  }

  return operation(resolved.value());
}

smb::core::Result<bool> DfsResolvingSmbClient::checkConnection(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context) {
  return runWithDfsFallback<bool>(
      connection, secret, context,
      [this, secret, &context](const smb::core::Connection &candidate) {
        return m_delegate.checkConnection(candidate, secret, context);
      });
}

smb::core::Result<QVector<smb::core::RemoteFileEntry>>
DfsResolvingSmbClient::listDirectory(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context) {
  return runWithDfsFallback<QVector<smb::core::RemoteFileEntry>>(
      connection, secret, context,
      [this, secret, &remotePath,
       &context](const smb::core::Connection &candidate) {
        return m_delegate.listDirectory(candidate, secret, remotePath, context);
      });
}

smb::core::Result<bool> DfsResolvingSmbClient::createDirectory(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context) {
  return runWithDfsFallback<bool>(connection, secret, context,
                                  [this, secret, &remotePath, &context](
                                      const smb::core::Connection &candidate) {
                                    return m_delegate.createDirectory(
                                        candidate, secret, remotePath, context);
                                  });
}

smb::core::Result<bool>
DfsResolvingSmbClient::remove(const smb::core::Connection &connection,
                              const smb::core::CredentialSecret *secret,
                              const QString &remotePath,
                              const smb::core::OperationContext &context) {
  return runWithDfsFallback<bool>(
      connection, secret, context,
      [this, secret, &remotePath,
       &context](const smb::core::Connection &candidate) {
        return m_delegate.remove(candidate, secret, remotePath, context);
      });
}

smb::core::Result<bool>
DfsResolvingSmbClient::rename(const smb::core::Connection &connection,
                              const smb::core::CredentialSecret *secret,
                              const QString &sourceRemotePath,
                              const QString &targetRemotePath,
                              const smb::core::OperationContext &context) {
  return runWithDfsFallback<bool>(
      connection, secret, context,
      [this, secret, &sourceRemotePath, &targetRemotePath,
       &context](const smb::core::Connection &candidate) {
        return m_delegate.rename(candidate, secret, sourceRemotePath,
                                 targetRemotePath, context);
      });
}

smb::core::Result<bool> DfsResolvingSmbClient::downloadFile(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const QString &localPath, const smb::core::OperationContext &context) {
  return runWithDfsFallback<bool>(
      connection, secret, context,
      [this, secret, &remotePath, &localPath,
       &context](const smb::core::Connection &candidate) {
        return m_delegate.downloadFile(candidate, secret, remotePath, localPath,
                                       context);
      });
}

smb::core::Result<bool> DfsResolvingSmbClient::uploadFile(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &localPath,
    const QString &remotePath, const smb::core::OperationContext &context) {
  return runWithDfsFallback<bool>(
      connection, secret, context,
      [this, secret, &localPath, &remotePath,
       &context](const smb::core::Connection &candidate) {
        return m_delegate.uploadFile(candidate, secret, localPath, remotePath,
                                     context);
      });
}

smb::core::Result<bool>
DfsResolvingSmbClient::copy(const smb::core::Connection &sourceConnection,
                            const smb::core::CredentialSecret *sourceSecret,
                            const QString &sourceRemotePath,
                            const smb::core::Connection &targetConnection,
                            const smb::core::CredentialSecret *targetSecret,
                            const QString &targetRemotePath,
                            const smb::core::OperationContext &context) {
  auto source =
      cachedResolvedConnection(sourceConnection).value_or(sourceConnection);
  auto target =
      cachedResolvedConnection(targetConnection).value_or(targetConnection);

  auto result = m_delegate.copy(source, sourceSecret, sourceRemotePath, target,
                                targetSecret, targetRemotePath, context);
  if (result.ok() || !looksLikeDfsReferralFailure(result.error())) {
    return result;
  }

  source = resolveAndCache(sourceConnection, sourceSecret, context)
               .value_or(sourceConnection);
  target = resolveAndCache(targetConnection, targetSecret, context)
               .value_or(targetConnection);
  return m_delegate.copy(source, sourceSecret, sourceRemotePath, target,
                         targetSecret, targetRemotePath, context);
}

smb::core::Result<bool>
DfsResolvingSmbClient::move(const smb::core::Connection &sourceConnection,
                            const smb::core::CredentialSecret *sourceSecret,
                            const QString &sourceRemotePath,
                            const smb::core::Connection &targetConnection,
                            const smb::core::CredentialSecret *targetSecret,
                            const QString &targetRemotePath,
                            const smb::core::OperationContext &context) {
  auto source =
      cachedResolvedConnection(sourceConnection).value_or(sourceConnection);
  auto target =
      cachedResolvedConnection(targetConnection).value_or(targetConnection);

  auto result = m_delegate.move(source, sourceSecret, sourceRemotePath, target,
                                targetSecret, targetRemotePath, context);
  if (result.ok() || !looksLikeDfsReferralFailure(result.error())) {
    return result;
  }

  source = resolveAndCache(sourceConnection, sourceSecret, context)
               .value_or(sourceConnection);
  target = resolveAndCache(targetConnection, targetSecret, context)
               .value_or(targetConnection);
  return m_delegate.move(source, sourceSecret, sourceRemotePath, target,
                         targetSecret, targetRemotePath, context);
}

std::optional<smb::core::Connection>
DfsResolvingSmbClient::cachedResolvedConnection(
    const smb::core::Connection &connection) const {
  const QMutexLocker locker(&m_cacheMutex);
  const auto it = m_resolvedConnections.constFind(cacheKey(connection));
  if (it == m_resolvedConnections.cend()) {
    return std::nullopt;
  }
  return it.value();
}

std::optional<smb::core::Connection> DfsResolvingSmbClient::resolveAndCache(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context) {
  const auto resolved = m_resolver.resolve(connection, secret, context);
  if (!resolved.ok() || !resolved.value().has_value()) {
    return std::nullopt;
  }

  const auto target = resolved.value().value();
  {
    const QMutexLocker locker(&m_cacheMutex);
    m_resolvedConnections.insert(cacheKey(connection), target);
  }
  return target;
}

void DfsResolvingSmbClient::forgetCachedConnection(
    const smb::core::Connection &connection) {
  const QMutexLocker locker(&m_cacheMutex);
  m_resolvedConnections.remove(cacheKey(connection));
}

} // namespace smb::infrastructure
