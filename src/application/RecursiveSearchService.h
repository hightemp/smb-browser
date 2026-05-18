#pragma once

#include "application/ConnectionOpenService.h"

namespace smb::application {

struct RecursiveSearchOptions {
  QString connectionId;
  QString startRemotePath = QStringLiteral("/");
  QString query;
  int maxDepth = 5;
  int maxResults = 1000;
  bool includeDirectories = true;
};

struct RecursiveSearchResult {
  QVector<smb::core::RemoteFileEntry> entries;
  int scannedDirectories = 0;
  bool limitReached = false;
};

class RecursiveSearchService final {
public:
  explicit RecursiveSearchService(RemoteDirectoryUseCase &directoryUseCase);

  smb::core::Result<RecursiveSearchResult>
  search(const RecursiveSearchOptions &options,
         const smb::core::OperationContext &context = {}) const;

private:
  static QString normalizeRemotePath(QString remotePath);
  static bool matchesQuery(const smb::core::RemoteFileEntry &entry,
                           const QString &query);
  static smb::core::AppError cancelledError();
  static smb::core::AppError invalidOptionsError(const QString &details);

  RemoteDirectoryUseCase &m_directoryUseCase;
};

} // namespace smb::application
