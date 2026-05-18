#pragma once

#include "core/Error.h"

#include <QSqlDatabase>
#include <QString>

namespace smb::infrastructure {

class SqliteStorage {
public:
  SqliteStorage();
  ~SqliteStorage();

  SqliteStorage(const SqliteStorage &) = delete;
  SqliteStorage &operator=(const SqliteStorage &) = delete;

  smb::core::AppError open(const QString &databasePath);
  smb::core::AppError migrate();

  QSqlDatabase database() const;
  QString connectionName() const;
  bool isOpen() const;

private:
  smb::core::AppError execute(const QString &sql);

  QString m_connectionName;
  bool m_open = false;
};

} // namespace smb::infrastructure
