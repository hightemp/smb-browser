#include "storage/SqliteStorage.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QUuid>
#include <QVariant>

namespace smb::infrastructure {

namespace {

smb::core::AppError storageError(const QString &details) {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::StorageError,
                                       smb::core::ErrorCategory::Storage,
                                       details, false);
}

QString nowIsoUtc() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

} // namespace

SqliteStorage::SqliteStorage()
    : m_connectionName(
          QStringLiteral("smb_browser_%1")
              .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))) {}

SqliteStorage::~SqliteStorage() {
  if (QSqlDatabase::contains(m_connectionName)) {
    {
      auto db = QSqlDatabase::database(m_connectionName);
      if (db.isOpen()) {
        db.close();
      }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
  }
}

smb::core::AppError SqliteStorage::open(const QString &databasePath) {
  if (m_open) {
    return smb::core::AppError::none();
  }

  auto db =
      QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  db.setDatabaseName(databasePath);
  if (!db.open()) {
    return storageError(db.lastError().text());
  }

  m_open = true;
  const auto foreignKeysError =
      execute(QStringLiteral("PRAGMA foreign_keys = ON"));
  if (foreignKeysError.hasError()) {
    return foreignKeysError;
  }

  return smb::core::AppError::none();
}

smb::core::AppError SqliteStorage::migrate() {
  if (!isOpen()) {
    return storageError(QStringLiteral("SQLite database is not open."));
  }

  const QStringList statements{
      QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations ("
                     "version INTEGER PRIMARY KEY,"
                     "applied_at TEXT NOT NULL)"),
      QStringLiteral("CREATE TABLE IF NOT EXISTS connection_groups ("
                     "id TEXT PRIMARY KEY,"
                     "name TEXT NOT NULL,"
                     "sort_order INTEGER NOT NULL DEFAULT 0,"
                     "created_at TEXT NOT NULL,"
                     "updated_at TEXT NOT NULL)"),
      QStringLiteral("CREATE TABLE IF NOT EXISTS connections ("
                     "id TEXT PRIMARY KEY,"
                     "name TEXT NOT NULL,"
                     "input_path TEXT NOT NULL,"
                     "normalized_uri TEXT NOT NULL,"
                     "server TEXT NOT NULL,"
                     "share TEXT NOT NULL,"
                     "initial_remote_path TEXT NOT NULL DEFAULT '',"
                     "domain TEXT NOT NULL DEFAULT '',"
                     "username TEXT NOT NULL DEFAULT '',"
                     "auth_type TEXT NOT NULL,"
                     "credential_ref TEXT NOT NULL DEFAULT '',"
                     "comment TEXT NOT NULL DEFAULT '',"
                     "group_id TEXT,"
                     "is_favorite INTEGER NOT NULL DEFAULT 0,"
                     "last_opened_at TEXT,"
                     "created_at TEXT NOT NULL,"
                     "updated_at TEXT NOT NULL,"
                     "last_error_code TEXT NOT NULL DEFAULT 'none',"
                     "last_error_message TEXT NOT NULL DEFAULT '',"
                     "last_successful_check_at TEXT,"
                     "FOREIGN KEY(group_id) REFERENCES connection_groups(id) "
                     "ON DELETE SET NULL)"),
      QStringLiteral("CREATE INDEX IF NOT EXISTS idx_connections_group_id ON "
                     "connections(group_id)"),
      QStringLiteral("CREATE INDEX IF NOT EXISTS idx_connections_favorite ON "
                     "connections(is_favorite)"),
      QStringLiteral("CREATE TABLE IF NOT EXISTS settings ("
                     "key TEXT PRIMARY KEY,"
                     "value TEXT NOT NULL,"
                     "updated_at TEXT NOT NULL)")};

  auto db = database();
  if (!db.transaction()) {
    return storageError(db.lastError().text());
  }

  for (const auto &statement : statements) {
    const auto error = execute(statement);
    if (error.hasError()) {
      db.rollback();
      return error;
    }
  }

  QSqlQuery query(db);
  query.prepare(
      QStringLiteral("INSERT OR IGNORE INTO schema_migrations(version, "
                     "applied_at) VALUES(1, :applied_at)"));
  query.bindValue(QStringLiteral(":applied_at"), nowIsoUtc());
  if (!query.exec()) {
    db.rollback();
    return storageError(query.lastError().text());
  }

  if (!db.commit()) {
    return storageError(db.lastError().text());
  }

  return smb::core::AppError::none();
}

QSqlDatabase SqliteStorage::database() const {
  return QSqlDatabase::database(m_connectionName);
}

QString SqliteStorage::connectionName() const { return m_connectionName; }

bool SqliteStorage::isOpen() const {
  return m_open && QSqlDatabase::contains(m_connectionName) &&
         QSqlDatabase::database(m_connectionName).isOpen();
}

smb::core::AppError SqliteStorage::execute(const QString &sql) {
  QSqlQuery query(database());
  if (!query.exec(sql)) {
    return storageError(query.lastError().text());
  }

  return smb::core::AppError::none();
}

} // namespace smb::infrastructure
