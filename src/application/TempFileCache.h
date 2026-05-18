#pragma once

#include "core/Error.h"

#include <QDateTime>
#include <QMutex>
#include <QSet>
#include <QString>

namespace smb::application {

struct TempFileCacheCleanupOptions {
  QDateTime removeFilesOlderThanUtc;
  qint64 maxSizeBytes = 0;
};

struct TempFileCacheCleanupResult {
  int filesRemoved = 0;
  qint64 bytesRemoved = 0;
  qint64 bytesRemaining = 0;
};

class TempFileCache {
public:
  explicit TempFileCache(QString rootPath = defaultRootPath());
  ~TempFileCache() = default;

  TempFileCache(const TempFileCache &) = delete;
  TempFileCache &operator=(const TempFileCache &) = delete;

  static QString defaultRootPath();

  QString rootPath() const;
  smb::core::Result<QString> localPathFor(const QString &connectionId,
                                          const QString &remotePath) const;
  smb::core::Result<TempFileCacheCleanupResult>
  cleanup(const TempFileCacheCleanupOptions &options) const;
  smb::core::Result<int> removeOlderThan(const QDateTime &cutoffUtc) const;
  smb::core::Result<bool> clearAll() const;
  void protectPath(const QString &localPath) const;
  void unprotectPath(const QString &localPath) const;
  bool isProtectedPath(const QString &localPath) const;

private:
  QSet<QString> protectedPathsSnapshot() const;

  QString m_rootPath;
  mutable QMutex m_mutex;
  mutable QSet<QString> m_protectedPaths;
};

} // namespace smb::application
