#include "application/RecursiveSearchService.h"

#include <QQueue>

namespace smb::application {

namespace {

struct PendingDirectory {
  QString remotePath;
  int depth = 0;
};

bool isCancellationRequested(const smb::core::OperationContext &context) {
  return context.cancellationToken != nullptr &&
         context.cancellationToken->isCancellationRequested();
}

} // namespace

RecursiveSearchService::RecursiveSearchService(
    RemoteDirectoryUseCase &directoryUseCase)
    : m_directoryUseCase(directoryUseCase) {}

smb::core::Result<RecursiveSearchResult> RecursiveSearchService::search(
    const RecursiveSearchOptions &options,
    const smb::core::OperationContext &context) const {
  if (options.connectionId.trimmed().isEmpty()) {
    return smb::core::Result<RecursiveSearchResult>::failure(
        invalidOptionsError(QStringLiteral("Connection ID is required.")));
  }
  if (options.maxDepth < 0 || options.maxResults <= 0) {
    return smb::core::Result<RecursiveSearchResult>::failure(
        invalidOptionsError(
            QStringLiteral("Depth and result limits must be positive.")));
  }

  RecursiveSearchResult result;
  QQueue<PendingDirectory> pending;
  pending.enqueue(
      PendingDirectory{normalizeRemotePath(options.startRemotePath), 0});

  const auto query = options.query.trimmed();
  while (!pending.isEmpty()) {
    if (isCancellationRequested(context)) {
      return smb::core::Result<RecursiveSearchResult>::failure(
          cancelledError());
    }

    const auto current = pending.dequeue();
    const auto listed = m_directoryUseCase.listDirectory(
        options.connectionId, current.remotePath, context);
    if (!listed.ok()) {
      return smb::core::Result<RecursiveSearchResult>::failure(listed.error());
    }

    ++result.scannedDirectories;
    if (context.progressCallback) {
      context.progressCallback(smb::core::TransferProgress{
          result.scannedDirectories, options.maxResults});
    }

    for (const auto &entry : listed.value().entries) {
      if (isCancellationRequested(context)) {
        return smb::core::Result<RecursiveSearchResult>::failure(
            cancelledError());
      }

      const auto shouldInclude =
          (options.includeDirectories || entry.isFile()) &&
          matchesQuery(entry, query);
      if (shouldInclude) {
        result.entries.push_back(entry);
        if (result.entries.size() >= options.maxResults) {
          result.limitReached = true;
          return smb::core::Result<RecursiveSearchResult>::success(
              std::move(result));
        }
      }

      if (entry.isDirectory() && current.depth < options.maxDepth) {
        pending.enqueue(PendingDirectory{normalizeRemotePath(entry.remotePath),
                                         current.depth + 1});
      }
    }
  }

  return smb::core::Result<RecursiveSearchResult>::success(std::move(result));
}

QString RecursiveSearchService::normalizeRemotePath(QString remotePath) {
  remotePath =
      remotePath.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (remotePath.isEmpty()) {
    return QStringLiteral("/");
  }
  if (!remotePath.startsWith(QLatin1Char('/'))) {
    remotePath.prepend(QLatin1Char('/'));
  }
  while (remotePath.size() > 1 && remotePath.endsWith(QLatin1Char('/'))) {
    remotePath.chop(1);
  }
  return remotePath;
}

bool RecursiveSearchService::matchesQuery(
    const smb::core::RemoteFileEntry &entry, const QString &query) {
  return query.isEmpty() || entry.name.contains(query, Qt::CaseInsensitive) ||
         entry.remotePath.contains(query, Qt::CaseInsensitive);
}

smb::core::AppError RecursiveSearchService::cancelledError() {
  return smb::core::AppError::fromCode(
      smb::core::ErrorCode::OperationCancelled,
      smb::core::ErrorCategory::General,
      QStringLiteral("Recursive search cancelled."), false);
}

smb::core::AppError
RecursiveSearchService::invalidOptionsError(const QString &details) {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::InvalidPath,
                                       smb::core::ErrorCategory::Validation,
                                       details, false);
}

} // namespace smb::application
