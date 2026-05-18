#pragma once

#include "core/Connection.h"
#include "core/Error.h"

#include <QSqlDatabase>
#include <QVector>

namespace smb::infrastructure {

class ConnectionRepository {
public:
  explicit ConnectionRepository(QSqlDatabase database);

  smb::core::Result<smb::core::Connection>
  add(smb::core::Connection connection);
  smb::core::Result<smb::core::Connection> getById(const QString &id) const;
  smb::core::Result<QVector<smb::core::Connection>> list() const;
  smb::core::Result<smb::core::Connection>
  update(smb::core::Connection connection);
  smb::core::Result<bool> remove(const QString &id);

  smb::core::Result<bool> updateLastOpened(const QString &id,
                                           const QDateTime &openedAt);
  smb::core::Result<bool> updateLastError(const QString &id,
                                          smb::core::ErrorCode errorCode,
                                          const QString &sanitizedMessage);
  smb::core::Result<bool> updateLastSuccessfulCheck(const QString &id,
                                                    const QDateTime &checkedAt);

private:
  QSqlDatabase m_database;
};

} // namespace smb::infrastructure
