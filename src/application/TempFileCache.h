#pragma once

#include "core/Error.h"

#include <QDateTime>
#include <QString>

namespace smb::application {

class TempFileCache {
public:
  explicit TempFileCache(QString rootPath = defaultRootPath());

  static QString defaultRootPath();

  QString rootPath() const;
  smb::core::Result<QString> localPathFor(const QString &connectionId,
                                          const QString &remotePath) const;
  smb::core::Result<int> removeOlderThan(const QDateTime &cutoffUtc) const;
  smb::core::Result<bool> clearAll() const;

private:
  QString m_rootPath;
};

} // namespace smb::application
