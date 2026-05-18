#include "application/TempFileCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
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

int removeOldFiles(const QDir &dir, const QDateTime &cutoffUtc) {
  int removed = 0;
  const auto entries =
      dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
  for (const auto &entry : entries) {
    if (entry.isDir()) {
      removed += removeOldFiles(QDir(entry.absoluteFilePath()), cutoffUtc);
      dir.rmdir(entry.fileName());
      continue;
    }

    if (entry.lastModified().toUTC() < cutoffUtc) {
      if (QFile::remove(entry.absoluteFilePath())) {
        ++removed;
      }
    }
  }
  return removed;
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
  QDir root(m_rootPath);
  if (!root.exists()) {
    return smb::core::Result<int>::success(0);
  }

  return smb::core::Result<int>::success(
      removeOldFiles(root, cutoffUtc.toUTC()));
}

smb::core::Result<bool> TempFileCache::clearAll() const {
  QDir root(m_rootPath);
  if (!root.exists()) {
    return smb::core::Result<bool>::success(true);
  }
  if (!root.removeRecursively()) {
    return smb::core::Result<bool>::failure(
        cacheError(QStringLiteral("Unable to clear cache directory.")));
  }
  return smb::core::Result<bool>::success(true);
}

} // namespace smb::application
