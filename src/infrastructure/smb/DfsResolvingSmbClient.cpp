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

QString normalizeRemotePath(QString remotePath) {
  remotePath = remotePath.trimmed();
  remotePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
  while (remotePath.contains(QStringLiteral("//"))) {
    remotePath.replace(QStringLiteral("//"), QStringLiteral("/"));
  }
  if (remotePath.isEmpty()) {
    remotePath = QStringLiteral("/");
  }
  if (!remotePath.startsWith(QLatin1Char('/'))) {
    remotePath.prepend(QLatin1Char('/'));
  }
  while (remotePath.size() > 1 && remotePath.endsWith(QLatin1Char('/'))) {
    remotePath.chop(1);
  }
  return remotePath;
}

QString pathCacheKey(const smb::core::Connection &connection,
                     const QString &originalPrefix) {
  return QStringLiteral("%1\n%2")
      .arg(cacheKey(connection), normalizeRemotePath(originalPrefix));
}

bool isPathPrefix(const QString &prefix, const QString &remotePath) {
  const auto normalizedPrefix = normalizeRemotePath(prefix);
  const auto normalizedPath = normalizeRemotePath(remotePath);
  return normalizedPrefix == QStringLiteral("/") ||
         normalizedPath == normalizedPrefix ||
         normalizedPath.startsWith(normalizedPrefix + QLatin1Char('/'));
}

QString suffixAfterPrefix(const QString &remotePath, const QString &prefix) {
  const auto normalizedPath = normalizeRemotePath(remotePath);
  const auto normalizedPrefix = normalizeRemotePath(prefix);
  if (normalizedPrefix == QStringLiteral("/")) {
    return normalizedPath == QStringLiteral("/") ? QString() : normalizedPath;
  }
  if (normalizedPath == normalizedPrefix) {
    return {};
  }
  if (!normalizedPath.startsWith(normalizedPrefix + QLatin1Char('/'))) {
    return {};
  }
  return normalizedPath.mid(normalizedPrefix.size());
}

QString joinRemotePath(const QString &prefix, const QString &suffix) {
  const auto normalizedPrefix = normalizeRemotePath(prefix);
  if (suffix.isEmpty()) {
    return normalizedPrefix;
  }

  auto normalizedSuffix = suffix;
  normalizedSuffix.replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (!normalizedSuffix.startsWith(QLatin1Char('/'))) {
    normalizedSuffix.prepend(QLatin1Char('/'));
  }
  while (normalizedSuffix.size() > 1 &&
         normalizedSuffix.endsWith(QLatin1Char('/'))) {
    normalizedSuffix.chop(1);
  }

  if (normalizedPrefix == QStringLiteral("/")) {
    return normalizedSuffix;
  }
  return normalizedPrefix + normalizedSuffix;
}

QString parentRemotePath(const QString &remotePath) {
  const auto normalized = normalizeRemotePath(remotePath);
  const auto slash = normalized.lastIndexOf(QLatin1Char('/'));
  if (slash <= 0) {
    return QStringLiteral("/");
  }
  return normalized.left(slash);
}

QString targetPathForMapping(const DfsPathMapping &mapping,
                             const QString &remotePath) {
  return joinRemotePath(mapping.targetPrefix,
                        suffixAfterPrefix(remotePath, mapping.originalPrefix));
}

QString originalPathForMapping(const DfsPathMapping &mapping,
                               const QString &targetPath) {
  return joinRemotePath(mapping.originalPrefix,
                        suffixAfterPrefix(targetPath, mapping.targetPrefix));
}

QVector<smb::core::RemoteFileEntry>
rebaseEntriesToOriginalNamespace(QVector<smb::core::RemoteFileEntry> entries,
                                 const DfsPathMapping &mapping) {
  for (auto &entry : entries) {
    entry.remotePath = originalPathForMapping(mapping, entry.remotePath);
  }
  return entries;
}

bool looksLikeShareDfsReferralFailure(const smb::core::AppError &error) {
  if (error.code != smb::core::ErrorCode::ShareUnavailable) {
    return false;
  }

  const auto details = error.sanitizedTechnicalDetails.toLower();
  return details.contains(QStringLiteral("bad network name")) ||
         details.contains(QStringLiteral("bad_network_name")) ||
         details.contains(QStringLiteral("status_bad_network_name")) ||
         details.contains(QStringLiteral("tree connect"));
}

bool looksLikePathDfsReferralFailure(const smb::core::AppError &error) {
  if (error.code != smb::core::ErrorCode::ShareUnavailable &&
      error.code != smb::core::ErrorCode::NetworkError) {
    return false;
  }

  const auto details = error.sanitizedTechnicalDetails.toLower();
  return details.contains(QStringLiteral("path not covered")) ||
         details.contains(QStringLiteral("path_not_covered")) ||
         details.contains(QStringLiteral("status_path_not_covered")) ||
         details.contains(QStringLiteral("0xc0000257"));
}

bool looksLikeDfsReferralFailure(const smb::core::AppError &error) {
  return looksLikeShareDfsReferralFailure(error) ||
         looksLikePathDfsReferralFailure(error);
}

bool sameResolvedTarget(const DfsPathMapping &left,
                        const DfsPathMapping &right) {
  return left.targetConnection.server.compare(right.targetConnection.server,
                                              Qt::CaseInsensitive) == 0 &&
         left.targetConnection.share.compare(right.targetConnection.share,
                                             Qt::CaseInsensitive) == 0 &&
         left.targetConnection.domain.compare(right.targetConnection.domain,
                                             Qt::CaseInsensitive) == 0 &&
         left.targetConnection.username.compare(right.targetConnection.username,
                                               Qt::CaseInsensitive) == 0 &&
         left.targetConnection.authType == right.targetConnection.authType;
}

} // namespace

DfsResolvingSmbClient::DfsResolvingSmbClient(
    smb::core::SmbClient &delegate, smb::core::DfsReferralResolver &resolver)
    : m_delegate(delegate), m_resolver(resolver) {}

smb::core::SmbClientCapabilities
DfsResolvingSmbClient::capabilities(
    const smb::core::Connection &connection) const {
  return m_delegate.capabilities(connection);
}

smb::core::Result<smb::core::SmbCapabilityReport>
DfsResolvingSmbClient::probeCapabilities(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context) {
  return m_delegate.probeCapabilities(connection, secret, context);
}

smb::core::Result<QVector<smb::core::SmbShareInfo>>
DfsResolvingSmbClient::listShares(const smb::core::Connection &connection,
                                  const smb::core::CredentialSecret *secret,
                                  const smb::core::OperationContext &context) {
  return m_delegate.listShares(connection, secret, context);
}

template <typename T, typename Operation>
smb::core::Result<T> DfsResolvingSmbClient::runWithDfsFallback(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context, Operation operation) {
  if (const auto cached = cachedResolvedConnection(connection);
      cached.has_value()) {
    auto cachedResult = operation(cached.value());
    if (cachedResult.ok() ||
        !looksLikeShareDfsReferralFailure(cachedResult.error())) {
      return cachedResult;
    }
    forgetCachedConnection(connection);
  }

  auto result = operation(connection);
  if (result.ok() || !looksLikeShareDfsReferralFailure(result.error())) {
    return result;
  }

  const auto resolved = resolveAndCache(connection, secret, context);
  if (!resolved.has_value()) {
    return result;
  }

  return operation(resolved.value());
}

template <typename T, typename Operation, typename Rebase>
smb::core::Result<T> DfsResolvingSmbClient::runWithPathDfsFallback(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context, Operation operation,
    Rebase rebase) {
  const auto normalizedPath = normalizeRemotePath(remotePath);
  if (const auto mapping = cachedResolvedPathMapping(connection, normalizedPath);
      mapping.has_value()) {
    auto cachedResult =
        operation(mapping->targetConnection,
                  targetPathForMapping(mapping.value(), normalizedPath));
    if (cachedResult.ok()) {
      return smb::core::Result<T>::success(
          rebase(std::move(cachedResult.value()), mapping.value()));
    }
    if (!looksLikeDfsReferralFailure(cachedResult.error())) {
      return cachedResult;
    }
    forgetCachedPathMapping(connection, mapping->originalPrefix);
  }

  auto result = runWithDfsFallback<T>(
      connection, secret, context,
      [&operation, &normalizedPath](const smb::core::Connection &candidate) {
        return operation(candidate, normalizedPath);
      });
  if (result.ok() || !looksLikePathDfsReferralFailure(result.error())) {
    return result;
  }

  const auto mapping =
      resolvePathAndCache(connection, secret, normalizedPath, context);
  if (!mapping.has_value()) {
    return result;
  }

  auto resolvedResult =
      operation(mapping->targetConnection,
                targetPathForMapping(mapping.value(), normalizedPath));
  if (!resolvedResult.ok()) {
    return resolvedResult;
  }
  return smb::core::Result<T>::success(
      rebase(std::move(resolvedResult.value()), mapping.value()));
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
  return runWithPathDfsFallback<QVector<smb::core::RemoteFileEntry>>(
      connection, secret, remotePath, context,
      [this, secret,
       &context](const smb::core::Connection &candidate,
                 const QString &candidatePath) {
        return m_delegate.listDirectory(candidate, secret, candidatePath,
                                        context);
      },
      [](QVector<smb::core::RemoteFileEntry> entries,
         const DfsPathMapping &mapping) {
        return rebaseEntriesToOriginalNamespace(std::move(entries), mapping);
      });
}

smb::core::Result<bool> DfsResolvingSmbClient::createDirectory(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context) {
  return runWithPathDfsFallback<bool>(
      connection, secret, remotePath, context,
      [this, secret,
       &context](const smb::core::Connection &candidate,
                 const QString &candidatePath) {
        return m_delegate.createDirectory(candidate, secret, candidatePath,
                                          context);
      },
      [](bool value, const DfsPathMapping &) { return value; });
}

smb::core::Result<bool>
DfsResolvingSmbClient::remove(const smb::core::Connection &connection,
                              const smb::core::CredentialSecret *secret,
                              const QString &remotePath,
                              const smb::core::OperationContext &context) {
  return runWithPathDfsFallback<bool>(
      connection, secret, remotePath, context,
      [this, secret,
       &context](const smb::core::Connection &candidate,
                 const QString &candidatePath) {
        return m_delegate.remove(candidate, secret, candidatePath, context);
      },
      [](bool value, const DfsPathMapping &) { return value; });
}

smb::core::Result<bool>
DfsResolvingSmbClient::rename(const smb::core::Connection &connection,
                              const smb::core::CredentialSecret *secret,
                              const QString &sourceRemotePath,
                              const QString &targetRemotePath,
                              const smb::core::OperationContext &context) {
  const auto sourcePath = normalizeRemotePath(sourceRemotePath);
  const auto targetPath = normalizeRemotePath(targetRemotePath);

  const auto cachedSource = cachedResolvedPathMapping(connection, sourcePath);
  const auto cachedTarget = cachedResolvedPathMapping(connection, targetPath);
  if (cachedSource.has_value() && cachedTarget.has_value() &&
      sameResolvedTarget(cachedSource.value(), cachedTarget.value())) {
    auto cachedResult = m_delegate.rename(
        cachedSource->targetConnection, secret,
        targetPathForMapping(cachedSource.value(), sourcePath),
        targetPathForMapping(cachedTarget.value(), targetPath), context);
    if (cachedResult.ok() ||
        !looksLikeDfsReferralFailure(cachedResult.error())) {
      return cachedResult;
    }
    forgetCachedPathMapping(connection, cachedSource->originalPrefix);
    forgetCachedPathMapping(connection, cachedTarget->originalPrefix);
  }

  auto result = runWithDfsFallback<bool>(
      connection, secret, context,
      [this, secret, &sourcePath, &targetPath,
       &context](const smb::core::Connection &candidate) {
        return m_delegate.rename(candidate, secret, sourcePath, targetPath,
                                 context);
      });
  if (result.ok() || !looksLikePathDfsReferralFailure(result.error())) {
    return result;
  }

  const auto mapping = resolvePathAndCache(connection, secret, sourcePath,
                                           context);
  if (!mapping.has_value()) {
    return result;
  }

  const auto mappedTargetPath = isPathPrefix(mapping->originalPrefix, targetPath)
                                    ? targetPathForMapping(mapping.value(),
                                                           targetPath)
                                    : targetPath;
  return m_delegate.rename(mapping->targetConnection, secret,
                           targetPathForMapping(mapping.value(), sourcePath),
                           mappedTargetPath, context);
}

smb::core::Result<bool> DfsResolvingSmbClient::downloadFile(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const QString &localPath, const smb::core::OperationContext &context) {
  return runWithPathDfsFallback<bool>(
      connection, secret, remotePath, context,
      [this, secret, &localPath,
       &context](const smb::core::Connection &candidate,
                 const QString &candidatePath) {
        return m_delegate.downloadFile(candidate, secret, candidatePath,
                                       localPath, context);
      },
      [](bool value, const DfsPathMapping &) { return value; });
}

smb::core::Result<bool> DfsResolvingSmbClient::uploadFile(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &localPath,
    const QString &remotePath, const smb::core::OperationContext &context) {
  return runWithPathDfsFallback<bool>(
      connection, secret, remotePath, context,
      [this, secret, &localPath,
       &context](const smb::core::Connection &candidate,
                 const QString &candidatePath) {
        return m_delegate.uploadFile(candidate, secret, localPath,
                                     candidatePath, context);
      },
      [](bool value, const DfsPathMapping &) { return value; });
}

smb::core::Result<bool>
DfsResolvingSmbClient::copy(const smb::core::Connection &sourceConnection,
                            const smb::core::CredentialSecret *sourceSecret,
                            const QString &sourceRemotePath,
                            const smb::core::Connection &targetConnection,
                            const smb::core::CredentialSecret *targetSecret,
                            const QString &targetRemotePath,
                            const smb::core::OperationContext &context) {
  const auto originalSourcePath = normalizeRemotePath(sourceRemotePath);
  const auto originalTargetPath = normalizeRemotePath(targetRemotePath);
  auto source =
      cachedResolvedConnection(sourceConnection).value_or(sourceConnection);
  auto target =
      cachedResolvedConnection(targetConnection).value_or(targetConnection);
  auto sourcePath = originalSourcePath;
  auto targetPath = originalTargetPath;

  if (const auto mapping =
          cachedResolvedPathMapping(sourceConnection, originalSourcePath);
      mapping.has_value()) {
    source = mapping->targetConnection;
    sourcePath = targetPathForMapping(mapping.value(), originalSourcePath);
  }
  if (const auto mapping =
          cachedResolvedPathMapping(targetConnection, originalTargetPath);
      mapping.has_value()) {
    target = mapping->targetConnection;
    targetPath = targetPathForMapping(mapping.value(), originalTargetPath);
  }

  auto result = m_delegate.copy(source, sourceSecret, sourcePath, target,
                                targetSecret, targetPath, context);
  if (result.ok()) {
    return result;
  }

  if (looksLikePathDfsReferralFailure(result.error())) {
    if (const auto mapping = resolvePathAndCache(
            sourceConnection, sourceSecret, originalSourcePath, context);
        mapping.has_value()) {
      source = mapping->targetConnection;
      sourcePath = targetPathForMapping(mapping.value(), originalSourcePath);
    }
    if (const auto mapping = resolvePathAndCache(
            targetConnection, targetSecret, parentRemotePath(originalTargetPath),
            context);
        mapping.has_value()) {
      target = mapping->targetConnection;
      targetPath = targetPathForMapping(mapping.value(), originalTargetPath);
    }
    return m_delegate.copy(source, sourceSecret, sourcePath, target,
                           targetSecret, targetPath, context);
  }

  if (!looksLikeShareDfsReferralFailure(result.error())) {
    return result;
  }

  source = resolveAndCache(sourceConnection, sourceSecret, context)
               .value_or(sourceConnection);
  target = resolveAndCache(targetConnection, targetSecret, context)
               .value_or(targetConnection);
  return m_delegate.copy(source, sourceSecret, originalSourcePath, target,
                         targetSecret, originalTargetPath, context);
}

smb::core::Result<bool>
DfsResolvingSmbClient::move(const smb::core::Connection &sourceConnection,
                            const smb::core::CredentialSecret *sourceSecret,
                            const QString &sourceRemotePath,
                            const smb::core::Connection &targetConnection,
                            const smb::core::CredentialSecret *targetSecret,
                            const QString &targetRemotePath,
                            const smb::core::OperationContext &context) {
  const auto originalSourcePath = normalizeRemotePath(sourceRemotePath);
  const auto originalTargetPath = normalizeRemotePath(targetRemotePath);
  auto source =
      cachedResolvedConnection(sourceConnection).value_or(sourceConnection);
  auto target =
      cachedResolvedConnection(targetConnection).value_or(targetConnection);
  auto sourcePath = originalSourcePath;
  auto targetPath = originalTargetPath;

  if (const auto mapping =
          cachedResolvedPathMapping(sourceConnection, originalSourcePath);
      mapping.has_value()) {
    source = mapping->targetConnection;
    sourcePath = targetPathForMapping(mapping.value(), originalSourcePath);
  }
  if (const auto mapping =
          cachedResolvedPathMapping(targetConnection, originalTargetPath);
      mapping.has_value()) {
    target = mapping->targetConnection;
    targetPath = targetPathForMapping(mapping.value(), originalTargetPath);
  }

  auto result = m_delegate.move(source, sourceSecret, sourcePath, target,
                                targetSecret, targetPath, context);
  if (result.ok()) {
    return result;
  }

  if (looksLikePathDfsReferralFailure(result.error())) {
    if (const auto mapping = resolvePathAndCache(
            sourceConnection, sourceSecret, originalSourcePath, context);
        mapping.has_value()) {
      source = mapping->targetConnection;
      sourcePath = targetPathForMapping(mapping.value(), originalSourcePath);
    }
    if (const auto mapping = resolvePathAndCache(
            targetConnection, targetSecret, parentRemotePath(originalTargetPath),
            context);
        mapping.has_value()) {
      target = mapping->targetConnection;
      targetPath = targetPathForMapping(mapping.value(), originalTargetPath);
    }
    return m_delegate.move(source, sourceSecret, sourcePath, target,
                           targetSecret, targetPath, context);
  }

  if (!looksLikeShareDfsReferralFailure(result.error())) {
    return result;
  }

  source = resolveAndCache(sourceConnection, sourceSecret, context)
               .value_or(sourceConnection);
  target = resolveAndCache(targetConnection, targetSecret, context)
               .value_or(targetConnection);
  return m_delegate.move(source, sourceSecret, originalSourcePath, target,
                         targetSecret, originalTargetPath, context);
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

std::optional<DfsPathMapping> DfsResolvingSmbClient::cachedResolvedPathMapping(
    const smb::core::Connection &connection, const QString &remotePath) const {
  const auto sourceKey = cacheKey(connection);
  const auto normalizedPath = normalizeRemotePath(remotePath);
  const QMutexLocker locker(&m_cacheMutex);

  std::optional<DfsPathMapping> best;
  for (auto it = m_resolvedPathMappings.cbegin();
       it != m_resolvedPathMappings.cend(); ++it) {
    const auto &mapping = it.value();
    if (mapping.connectionKey != sourceKey ||
        !isPathPrefix(mapping.originalPrefix, normalizedPath)) {
      continue;
    }
    if (!best.has_value() ||
        mapping.originalPrefix.size() > best->originalPrefix.size()) {
      best = mapping;
    }
  }
  return best;
}

std::optional<DfsPathMapping> DfsResolvingSmbClient::resolvePathAndCache(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context) {
  const auto resolved =
      m_resolver.resolvePath(connection, secret, remotePath, context);
  if (!resolved.ok() || !resolved.value().has_value()) {
    return std::nullopt;
  }

  const auto &resolvedPath = resolved.value().value();
  DfsPathMapping mapping;
  mapping.connectionKey = cacheKey(connection);
  mapping.originalPrefix = normalizeRemotePath(
      resolvedPath.originalPathPrefix.isEmpty() ? remotePath
                                                : resolvedPath.originalPathPrefix);
  mapping.targetConnection = resolvedPath.connection;
  mapping.targetPrefix = normalizeRemotePath(
      resolvedPath.targetPathPrefix.isEmpty() ? QStringLiteral("/")
                                              : resolvedPath.targetPathPrefix);

  {
    const QMutexLocker locker(&m_cacheMutex);
    m_resolvedPathMappings.insert(pathCacheKey(connection, mapping.originalPrefix),
                                  mapping);
  }
  return mapping;
}

void DfsResolvingSmbClient::forgetCachedPathMapping(
    const smb::core::Connection &connection, const QString &originalPrefix) {
  const QMutexLocker locker(&m_cacheMutex);
  m_resolvedPathMappings.remove(pathCacheKey(connection, originalPrefix));
}

} // namespace smb::infrastructure
