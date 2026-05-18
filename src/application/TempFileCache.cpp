#include "application/TempFileCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>
#include <algorithm>
#include <utility>

namespace smb::application {

namespace {

smb::core::AppError cacheError(const QString &details) {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::LocalIoError,
                                       smb::core::ErrorCategory::Transfer,
                                       details, false);
}

QString safeSuffix(const QString &remotePath) {
  auto suffix = QFileInfo(remotePath).suffix().toLower();
  if (suffix.size() > 16) {
    suffix.truncate(16);
  }

  QString safe;
  for (const auto character : suffix) {
    if (character.isLetterOrNumber()) {
      safe.append(character);
    }
  }
  return safe;
}

QString cacheKey(const QString &connectionId, const QString &remotePath) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(connectionId.toUtf8());
  hash.addData("\n", 1);
  hash.addData(remotePath.toUtf8());
  return QString::fromLatin1(hash.result().toHex());
}

struct CacheFile {
  QString path;
  QDateTime modifiedAtUtc;
  qint64 size = 0;
};

QString normalizedLocalPath(const QString &path) {
  return QFileInfo(path).absoluteFilePath();
}

bool isProtected(const QSet<QString> &protectedPaths, const QString &path) {
  return protectedPaths.contains(normalizedLocalPath(path));
}

void collectFiles(const QDir &dir, QVector<CacheFile> &files) {
  const auto entries =
      dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
  for (const auto &entry : entries) {
    if (entry.isDir()) {
      collectFiles(QDir(entry.absoluteFilePath()), files);
      continue;
    }

    files.push_back(CacheFile{entry.absoluteFilePath(),
                              entry.lastModified().toUTC(), entry.size()});
  }
}

void removeEmptyDirectories(const QDir &dir) {
  const auto entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const auto &entry : entries) {
    QDir child(entry.absoluteFilePath());
    removeEmptyDirectories(child);
    dir.rmdir(entry.fileName());
  }
}

qint64 totalSize(const QVector<CacheFile> &files) {
  qint64 total = 0;
  for (const auto &file : files) {
    total += file.size;
  }
  return total;
}

bool removeCacheFile(const CacheFile &file,
                     const QSet<QString> &protectedPaths,
                     TempFileCacheCleanupResult &result) {
  if (isProtected(protectedPaths, file.path)) {
    return false;
  }
  if (!QFile::remove(file.path)) {
    return false;
  }

  ++result.filesRemoved;
  result.bytesRemoved += file.size;
  return true;
}

} // namespace

TempFileCache::TempFileCache(QString rootPath)
    : m_rootPath(std::move(rootPath)) {}

QString TempFileCache::defaultRootPath() {
  auto base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (base.isEmpty()) {
    base = QDir::tempPath() + QStringLiteral("/smb-browser-cache");
  }
  return QDir(base).filePath(QStringLiteral("remote-files"));
}

QString TempFileCache::rootPath() const { return m_rootPath; }

smb::core::Result<QString>
TempFileCache::localPathFor(const QString &connectionId,
                            const QString &remotePath) const {
  const auto key = cacheKey(connectionId, remotePath);
  const auto shard = key.left(2);
  const auto suffix = safeSuffix(remotePath);

  QDir root(m_rootPath);
  const auto directory = root.filePath(shard);
  if (!QDir().mkpath(directory)) {
    return smb::core::Result<QString>::failure(
        cacheError(QStringLiteral("Unable to create cache directory.")));
  }

  auto fileName = key;
  if (!suffix.isEmpty()) {
    fileName += QStringLiteral(".") + suffix;
  }

  return smb::core::Result<QString>::success(
      QDir(directory).filePath(fileName));
}

smb::core::Result<int>
TempFileCache::removeOlderThan(const QDateTime &cutoffUtc) const {
  TempFileCacheCleanupOptions options;
  options.removeFilesOlderThanUtc = cutoffUtc;
  auto cleaned = cleanup(options);
  if (!cleaned.ok()) {
    return smb::core::Result<int>::failure(cleaned.error());
  }
  return smb::core::Result<int>::success(cleaned.value().filesRemoved);
}

smb::core::Result<TempFileCacheCleanupResult>
TempFileCache::cleanup(const TempFileCacheCleanupOptions &options) const {
  QDir root(m_rootPath);
  if (!root.exists()) {
    return smb::core::Result<TempFileCacheCleanupResult>::success({});
  }

  auto protectedPaths = protectedPathsSnapshot();
  TempFileCacheCleanupResult result;

  QVector<CacheFile> files;
  collectFiles(root, files);

  if (options.removeFilesOlderThanUtc.isValid()) {
    const auto cutoff = options.removeFilesOlderThanUtc.toUTC();
    for (const auto &file : files) {
      if (file.modifiedAtUtc < cutoff) {
        removeCacheFile(file, protectedPaths, result);
      }
    }
  }

  files.clear();
  collectFiles(root, files);
  auto remaining = totalSize(files);
  if (options.maxSizeBytes > 0 && remaining > options.maxSizeBytes) {
    std::sort(files.begin(), files.end(),
              [](const CacheFile &left, const CacheFile &right) {
                return left.modifiedAtUtc < right.modifiedAtUtc;
              });
    for (const auto &file : files) {
      if (remaining <= options.maxSizeBytes) {
        break;
      }
      if (removeCacheFile(file, protectedPaths, result)) {
        remaining -= file.size;
      }
    }
  }

  removeEmptyDirectories(root);
  files.clear();
  collectFiles(root, files);
  result.bytesRemaining = totalSize(files);
  return smb::core::Result<TempFileCacheCleanupResult>::success(result);
}

smb::core::Result<bool> TempFileCache::clearAll() const {
  QDir root(m_rootPath);
  if (!root.exists()) {
    return smb::core::Result<bool>::success(true);
  }

  const auto protectedPaths = protectedPathsSnapshot();
  if (protectedPaths.isEmpty()) {
    if (!root.removeRecursively()) {
      return smb::core::Result<bool>::failure(
          cacheError(QStringLiteral("Unable to clear cache directory.")));
    }
    return smb::core::Result<bool>::success(true);
  }

  QVector<CacheFile> files;
  collectFiles(root, files);
  TempFileCacheCleanupResult result;
  for (const auto &file : files) {
    removeCacheFile(file, protectedPaths, result);
  }
  removeEmptyDirectories(root);
  return smb::core::Result<bool>::success(true);
}

void TempFileCache::protectPath(const QString &localPath) const {
  if (localPath.isEmpty()) {
    return;
  }
  QMutexLocker locker(&m_mutex);
  m_protectedPaths.insert(normalizedLocalPath(localPath));
}

void TempFileCache::unprotectPath(const QString &localPath) const {
  if (localPath.isEmpty()) {
    return;
  }
  QMutexLocker locker(&m_mutex);
  m_protectedPaths.remove(normalizedLocalPath(localPath));
}

bool TempFileCache::isProtectedPath(const QString &localPath) const {
  if (localPath.isEmpty()) {
    return false;
  }
  QMutexLocker locker(&m_mutex);
  return m_protectedPaths.contains(normalizedLocalPath(localPath));
}

QSet<QString> TempFileCache::protectedPathsSnapshot() const {
  QMutexLocker locker(&m_mutex);
  return m_protectedPaths;
}

} // namespace smb::application
