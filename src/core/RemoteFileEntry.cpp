#include "core/RemoteFileEntry.h"

namespace smb::core {

QString toString(RemoteFileType type) {
  switch (type) {
  case RemoteFileType::File:
    return QStringLiteral("file");
  case RemoteFileType::Directory:
    return QStringLiteral("directory");
  case RemoteFileType::Symlink:
    return QStringLiteral("symlink");
  case RemoteFileType::Unknown:
    return QStringLiteral("unknown");
  }

  return QStringLiteral("unknown");
}

bool RemoteFileEntry::isDirectory() const {
  return type == RemoteFileType::Directory;
}

bool RemoteFileEntry::isFile() const { return type == RemoteFileType::File; }

} // namespace smb::core
