#pragma once

#include <QDateTime>
#include <QString>

namespace smb::core {

struct ConnectionGroup {
  QString id;
  QString name;
  int sortOrder = 0;
  QDateTime createdAt;
  QDateTime updatedAt;
};

} // namespace smb::core
