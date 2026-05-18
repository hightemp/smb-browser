#pragma once

#include <QDateTime>
#include <QString>

namespace smb::core {

enum class RemoteFileType {
  File,
  Directory,
  Symlink,
  Unknown,
};

struct RemoteFileEntry {
  QString name;
  QString remotePath;
  RemoteFileType type = RemoteFileType::Unknown;
  qint64 size = 0;
  QDateTime modifiedAt;
  QString attributes;
  QString permissions;
  bool isHidden = false;

  bool isDirectory() const;
  bool isFile() const;
};

QString toString(RemoteFileType type);

} // namespace smb::core
