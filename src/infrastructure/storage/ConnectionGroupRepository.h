#pragma once

#include "core/ConnectionGroup.h"
#include "core/Error.h"

#include <QSqlDatabase>
#include <QVector>

namespace smb::infrastructure {

class ConnectionGroupRepository {
public:
  explicit ConnectionGroupRepository(QSqlDatabase database);

  smb::core::Result<smb::core::ConnectionGroup>
  add(smb::core::ConnectionGroup group);
  smb::core::Result<smb::core::ConnectionGroup>
  getById(const QString &id) const;
  smb::core::Result<QVector<smb::core::ConnectionGroup>> list() const;
  smb::core::Result<smb::core::ConnectionGroup>
  update(smb::core::ConnectionGroup group);
  smb::core::Result<bool> remove(const QString &id);

private:
  QSqlDatabase m_database;
};

} // namespace smb::infrastructure
