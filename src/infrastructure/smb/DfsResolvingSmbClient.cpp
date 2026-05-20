#include "smb/DfsResolvingSmbClient.h"

#include <QMutexLocker>

#include <algorithm>
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

QDateTime expiresAtForTtl(int ttlSeconds) {
  return QDateTime::currentDateTimeUtc().addSecs(std::max(1, ttlSeconds));
}

bool isCacheFresh(const QDateTime &expiresAtUtc) {
  return !expiresAtUtc.isValid() ||
         QDateTime::currentDateTimeUtc() < expiresAtUtc;
}

bool isTargetFailoverError(const smb::core::AppError &error) {
  switch (error.code) {
  case smb::core::ErrorCode::ServerUnavailable:
  case smb::core::ErrorCode::ShareUnavailable:
  case smb::core::ErrorCode::Timeout:
  case smb::core::ErrorCode::NetworkError:
    return true;
  default:
    return false;
  }
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
  auto cachedTargets = cachedResolvedConnections(connection);
  if (!cachedTargets.isEmpty()) {
    std::optional<smb::core::AppError> lastFailoverError;
    for (const auto &cached : cachedTargets) {
      auto cachedResult = operation(cached);
      if (cachedResult.ok()) {
        return cachedResult;
      }
      if (looksLikeShareDfsReferralFailure(cachedResult.error())) {
        forgetCachedConnection(connection);
        cachedTargets.clear();
        break;
      }
      if (!isTargetFailoverError(cachedResult.error())) {
        return cachedResult;
      }
      lastFailoverError = cachedResult.error();
    }
    if (!cachedTargets.isEmpty() && lastFailoverError.has_value()) {
      return smb::core::Result<T>::failure(std::move(lastFailoverError.value()));
    }
  }

  auto result = operation(connection);
  if (result.ok() || !looksLikeShareDfsReferralFailure(result.error())) {
    return result;
  }

  const auto targets = resolveAndCache(connection, secret, context);
  if (targets.isEmpty()) {
    return result;
  }

  std::optional<smb::core::AppError> lastFailoverError;
  for (const auto &target : targets) {
    auto resolvedResult = operation(target);
    if (resolvedResult.ok()) {
      return resolvedResult;
    }
    if (!isTargetFailoverError(resolvedResult.error())) {
      return resolvedResult;
    }
    lastFailoverError = resolvedResult.error();
  }

  if (lastFailoverError.has_value()) {
    return smb::core::Result<T>::failure(std::move(lastFailoverError.value()));
  }
  return result;
}

template <typename T, typename Operation, typename Rebase>
smb::core::Result<T> DfsResolvingSmbClient::runWithPathDfsFallback(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context, Operation operation,
    Rebase rebase) {
  const auto normalizedPath = normalizeRemotePath(remotePath);
  auto cachedMappings = cachedResolvedPathMappings(connection, normalizedPath);
  if (!cachedMappings.isEmpty()) {
    std::optional<smb::core::AppError> lastFailoverError;
    for (const auto &mapping : cachedMappings) {
      auto cachedResult =
          operation(mapping.targetConnection,
                    targetPathForMapping(mapping, normalizedPath));
      if (cachedResult.ok()) {
        return smb::core::Result<T>::success(
            rebase(std::move(cachedResult.value()), mapping));
      }
      if (looksLikeDfsReferralFailure(cachedResult.error())) {
        forgetCachedPathMapping(connection, mapping.originalPrefix);
        cachedMappings.clear();
        break;
      }
      if (!isTargetFailoverError(cachedResult.error())) {
        return cachedResult;
      }
      lastFailoverError = cachedResult.error();
    }
    if (!cachedMappings.isEmpty() && lastFailoverError.has_value()) {
      return smb::core::Result<T>::failure(std::move(lastFailoverError.value()));
    }
  }

  auto result = runWithDfsFallback<T>(
      connection, secret, context,
      [&operation, &normalizedPath](const smb::core::Connection &candidate) {
        return operation(candidate, normalizedPath);
      });
  if (result.ok() || !looksLikePathDfsReferralFailure(result.error())) {
    return result;
  }

  const auto mappings =
      resolvePathAndCache(connection, secret, normalizedPath, context);
  if (mappings.isEmpty()) {
    return result;
  }

  std::optional<smb::core::AppError> lastFailoverError;
  for (const auto &mapping : mappings) {
    auto resolvedResult =
        operation(mapping.targetConnection,
                  targetPathForMapping(mapping, normalizedPath));
    if (resolvedResult.ok()) {
      return smb::core::Result<T>::success(
          rebase(std::move(resolvedResult.value()), mapping));
    }
    if (!isTargetFailoverError(resolvedResult.error())) {
      return resolvedResult;
    }
    lastFailoverError = resolvedResult.error();
  }

  if (lastFailoverError.has_value()) {
    return smb::core::Result<T>::failure(std::move(lastFailoverError.value()));
  }
  return result;
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

  const auto mappings = resolvePathAndCache(connection, secret, sourcePath,
                                            context);
  if (mappings.isEmpty()) {
    return result;
  }
  const auto mapping = mappings.first();

  const auto mappedTargetPath = isPathPrefix(mapping.originalPrefix, targetPath)
                                    ? targetPathForMapping(mapping, targetPath)
                                    : targetPath;
  return m_delegate.rename(mapping.targetConnection, secret,
                           targetPathForMapping(mapping, sourcePath),
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
    const auto sourceMappings = resolvePathAndCache(
        sourceConnection, sourceSecret, originalSourcePath, context);
    if (!sourceMappings.isEmpty()) {
      const auto mapping = sourceMappings.first();
      source = mapping.targetConnection;
      sourcePath = targetPathForMapping(mapping, originalSourcePath);
    }
    const auto targetMappings = resolvePathAndCache(
        targetConnection, targetSecret, parentRemotePath(originalTargetPath),
        context);
    if (!targetMappings.isEmpty()) {
      const auto mapping = targetMappings.first();
      target = mapping.targetConnection;
      targetPath = targetPathForMapping(mapping, originalTargetPath);
    }
    return m_delegate.copy(source, sourceSecret, sourcePath, target,
                           targetSecret, targetPath, context);
  }

  if (!looksLikeShareDfsReferralFailure(result.error())) {
    return result;
  }

  const auto resolvedSources =
      resolveAndCache(sourceConnection, sourceSecret, context);
  const auto resolvedTargets =
      resolveAndCache(targetConnection, targetSecret, context);
  source = resolvedSources.isEmpty() ? sourceConnection : resolvedSources.first();
  target = resolvedTargets.isEmpty() ? targetConnection : resolvedTargets.first();
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
    const auto sourceMappings = resolvePathAndCache(
        sourceConnection, sourceSecret, originalSourcePath, context);
    if (!sourceMappings.isEmpty()) {
      const auto mapping = sourceMappings.first();
      source = mapping.targetConnection;
      sourcePath = targetPathForMapping(mapping, originalSourcePath);
    }
    const auto targetMappings = resolvePathAndCache(
        targetConnection, targetSecret, parentRemotePath(originalTargetPath),
        context);
    if (!targetMappings.isEmpty()) {
      const auto mapping = targetMappings.first();
      target = mapping.targetConnection;
      targetPath = targetPathForMapping(mapping, originalTargetPath);
    }
    return m_delegate.move(source, sourceSecret, sourcePath, target,
                           targetSecret, targetPath, context);
  }

  if (!looksLikeShareDfsReferralFailure(result.error())) {
    return result;
  }

  const auto resolvedSources =
      resolveAndCache(sourceConnection, sourceSecret, context);
  const auto resolvedTargets =
      resolveAndCache(targetConnection, targetSecret, context);
  source = resolvedSources.isEmpty() ? sourceConnection : resolvedSources.first();
  target = resolvedTargets.isEmpty() ? targetConnection : resolvedTargets.first();
  return m_delegate.move(source, sourceSecret, originalSourcePath, target,
                         targetSecret, originalTargetPath, context);
}

QVector<smb::core::Connection>
DfsResolvingSmbClient::cachedResolvedConnections(
    const smb::core::Connection &connection) const {
  const QMutexLocker locker(&m_cacheMutex);
  const auto it = m_resolvedConnections.constFind(cacheKey(connection));
  if (it == m_resolvedConnections.cend() ||
      !isCacheFresh(it.value().expiresAtUtc)) {
    return {};
  }
  return it.value().targets;
}

QVector<smb::core::Connection> DfsResolvingSmbClient::resolveAndCache(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context) {
  const auto resolved = m_resolver.resolveTargets(connection, secret, context);
  if (!resolved.ok() || resolved.value().isEmpty()) {
    return {};
  }

  QVector<smb::core::Connection> targets;
  auto expiresAtUtc = QDateTime();
  for (const auto &target : resolved.value()) {
    targets.push_back(target.connection);
    const auto targetExpiresAt = expiresAtForTtl(target.ttlSeconds);
    if (!expiresAtUtc.isValid() || targetExpiresAt < expiresAtUtc) {
      expiresAtUtc = targetExpiresAt;
    }
  }
  {
    const QMutexLocker locker(&m_cacheMutex);
    m_resolvedConnections.insert(cacheKey(connection),
                                 DfsConnectionCacheEntry{targets, expiresAtUtc});
  }
  return targets;
}

void DfsResolvingSmbClient::forgetCachedConnection(
    const smb::core::Connection &connection) {
  const QMutexLocker locker(&m_cacheMutex);
  m_resolvedConnections.remove(cacheKey(connection));
}

std::optional<smb::core::Connection>
DfsResolvingSmbClient::cachedResolvedConnection(
    const smb::core::Connection &connection) const {
  const auto targets = cachedResolvedConnections(connection);
  if (targets.isEmpty()) {
    return std::nullopt;
  }
  return targets.first();
}

std::optional<DfsPathMapping> DfsResolvingSmbClient::cachedResolvedPathMapping(
    const smb::core::Connection &connection, const QString &remotePath) const {
  const auto mappings = cachedResolvedPathMappings(connection, remotePath);
  if (mappings.isEmpty()) {
    return std::nullopt;
  }
  return mappings.first();
}

QVector<DfsPathMapping> DfsResolvingSmbClient::cachedResolvedPathMappings(
    const smb::core::Connection &connection, const QString &remotePath) const {
  const auto sourceKey = cacheKey(connection);
  const auto normalizedPath = normalizeRemotePath(remotePath);
  const QMutexLocker locker(&m_cacheMutex);

  std::optional<DfsPathMappingCacheEntry> best;
  for (auto it = m_resolvedPathMappings.cbegin();
       it != m_resolvedPathMappings.cend(); ++it) {
    const auto &entry = it.value();
    if (!isCacheFresh(entry.expiresAtUtc) || entry.mappings.isEmpty()) {
      continue;
    }
    const auto &firstMapping = entry.mappings.first();
    if (firstMapping.connectionKey != sourceKey ||
        !isPathPrefix(firstMapping.originalPrefix, normalizedPath)) {
      continue;
    }
    if (!best.has_value() ||
        firstMapping.originalPrefix.size() >
            best->mappings.first().originalPrefix.size()) {
      best = entry;
    }
  }
  return best.has_value() ? best->mappings : QVector<DfsPathMapping>{};
}

QVector<DfsPathMapping> DfsResolvingSmbClient::resolvePathAndCache(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context) {
  const auto resolved =
      m_resolver.resolvePathTargets(connection, secret, remotePath, context);
  if (!resolved.ok() || resolved.value().isEmpty()) {
    return {};
  }

  QVector<DfsPathMapping> mappings;
  auto expiresAtUtc = QDateTime();
  for (const auto &resolvedPath : resolved.value()) {
    DfsPathMapping mapping;
    mapping.connectionKey = cacheKey(connection);
    mapping.originalPrefix = normalizeRemotePath(
        resolvedPath.originalPathPrefix.isEmpty()
            ? remotePath
            : resolvedPath.originalPathPrefix);
    mapping.targetConnection = resolvedPath.connection;
    mapping.targetPrefix =
        normalizeRemotePath(resolvedPath.targetPathPrefix.isEmpty()
                                ? QStringLiteral("/")
                                : resolvedPath.targetPathPrefix);
    mapping.expiresAtUtc = expiresAtForTtl(resolvedPath.ttlSeconds);
    if (!expiresAtUtc.isValid() || mapping.expiresAtUtc < expiresAtUtc) {
      expiresAtUtc = mapping.expiresAtUtc;
    }
    mappings.push_back(std::move(mapping));
  }

  {
    const QMutexLocker locker(&m_cacheMutex);
    m_resolvedPathMappings.insert(
        pathCacheKey(connection, mappings.first().originalPrefix),
        DfsPathMappingCacheEntry{mappings, expiresAtUtc});
  }
  return mappings;
}

void DfsResolvingSmbClient::forgetCachedPathMapping(
    const smb::core::Connection &connection, const QString &originalPrefix) {
  const QMutexLocker locker(&m_cacheMutex);
  m_resolvedPathMappings.remove(pathCacheKey(connection, originalPrefix));
}

} // namespace smb::infrastructure
