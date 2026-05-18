#include "storage/ConnectionGroupRepository.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QVariant>
#include <utility>

namespace smb::infrastructure {

namespace {

smb::core::AppError storageError(const QString &details) {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::StorageError,
                                       smb::core::ErrorCategory::Storage,
                                       details, false);
}

smb::core::AppError notFoundError(const QString &id) {
  return smb::core::AppError::fromCode(
      smb::core::ErrorCode::FileNotFound, smb::core::ErrorCategory::Storage,
      QStringLiteral("Connection group was not found: %1").arg(id), false);
}

QVariant dateToValue(const QDateTime &dateTime) {
  if (!dateTime.isValid()) {
    return QVariant(QVariant::String);
  }

  return dateTime.toUTC().toString(Qt::ISODateWithMs);
}

QDateTime dateFromValue(const QVariant &value) {
  if (value.isNull() || value.toString().isEmpty()) {
    return {};
  }

  return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}

QString nonNullString(const QString &value) {
  return value.isNull() ? QStringLiteral("") : value;
}

smb::core::ConnectionGroup groupFromQuery(const QSqlQuery &query) {
  const auto value = [&query](const QString &fieldName) {
    return query.value(query.record().indexOf(fieldName));
  };

  smb::core::ConnectionGroup group;
  group.id = value(QStringLiteral("id")).toString();
  group.name = value(QStringLiteral("name")).toString();
  group.sortOrder = value(QStringLiteral("sort_order")).toInt();
  group.createdAt = dateFromValue(value(QStringLiteral("created_at")));
  group.updatedAt = dateFromValue(value(QStringLiteral("updated_at")));
  return group;
}

void bindGroup(QSqlQuery &query, const smb::core::ConnectionGroup &group) {
  query.bindValue(QStringLiteral(":id"), group.id);
  query.bindValue(QStringLiteral(":name"), nonNullString(group.name));
  query.bindValue(QStringLiteral(":sort_order"), group.sortOrder);
  query.bindValue(QStringLiteral(":created_at"), dateToValue(group.createdAt));
  query.bindValue(QStringLiteral(":updated_at"), dateToValue(group.updatedAt));
}

} // namespace

ConnectionGroupRepository::ConnectionGroupRepository(QSqlDatabase database)
    : m_database(std::move(database)) {}

smb::core::Result<smb::core::ConnectionGroup>
ConnectionGroupRepository::add(smb::core::ConnectionGroup group) {
  if (group.id.isEmpty()) {
    group.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  const auto now = QDateTime::currentDateTimeUtc();
  if (!group.createdAt.isValid()) {
    group.createdAt = now;
  }
  group.updatedAt = now;

  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO connection_groups(id, name, sort_order, created_at, "
      "updated_at) "
      "VALUES(:id, :name, :sort_order, :created_at, :updated_at)"));
  bindGroup(query, group);
  if (!query.exec()) {
    return smb::core::Result<smb::core::ConnectionGroup>::failure(
        storageError(query.lastError().text()));
  }

  return smb::core::Result<smb::core::ConnectionGroup>::success(
      std::move(group));
}

smb::core::Result<smb::core::ConnectionGroup>
ConnectionGroupRepository::getById(const QString &id) const {
  QSqlQuery query(m_database);
  query.prepare(
      QStringLiteral("SELECT * FROM connection_groups WHERE id = :id"));
  query.bindValue(QStringLiteral(":id"), id);
  if (!query.exec()) {
    return smb::core::Result<smb::core::ConnectionGroup>::failure(
        storageError(query.lastError().text()));
  }
  if (!query.next()) {
    return smb::core::Result<smb::core::ConnectionGroup>::failure(
        notFoundError(id));
  }

  return smb::core::Result<smb::core::ConnectionGroup>::success(
      groupFromQuery(query));
}

smb::core::Result<QVector<smb::core::ConnectionGroup>>
ConnectionGroupRepository::list() const {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral("SELECT * FROM connection_groups ORDER BY "
                                 "sort_order ASC, name COLLATE NOCASE ASC"))) {
    return smb::core::Result<QVector<smb::core::ConnectionGroup>>::failure(
        storageError(query.lastError().text()));
  }

  QVector<smb::core::ConnectionGroup> groups;
  while (query.next()) {
    groups.push_back(groupFromQuery(query));
  }

  return smb::core::Result<QVector<smb::core::ConnectionGroup>>::success(
      std::move(groups));
}

smb::core::Result<smb::core::ConnectionGroup>
ConnectionGroupRepository::update(smb::core::ConnectionGroup group) {
  if (group.id.isEmpty()) {
    return smb::core::Result<smb::core::ConnectionGroup>::failure(
        notFoundError(group.id));
  }
  group.updatedAt = QDateTime::currentDateTimeUtc();

  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "UPDATE connection_groups SET name = :name, sort_order = :sort_order, "
      "created_at = :created_at, updated_at = :updated_at WHERE id = :id"));
  bindGroup(query, group);
  if (!query.exec()) {
    return smb::core::Result<smb::core::ConnectionGroup>::failure(
        storageError(query.lastError().text()));
  }
  if (query.numRowsAffected() == 0) {
    return smb::core::Result<smb::core::ConnectionGroup>::failure(
        notFoundError(group.id));
  }

  return smb::core::Result<smb::core::ConnectionGroup>::success(
      std::move(group));
}

smb::core::Result<bool> ConnectionGroupRepository::remove(const QString &id) {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("DELETE FROM connection_groups WHERE id = :id"));
  query.bindValue(QStringLiteral(":id"), id);
  if (!query.exec()) {
    return smb::core::Result<bool>::failure(
        storageError(query.lastError().text()));
  }

  return smb::core::Result<bool>::success(query.numRowsAffected() > 0);
}

} // namespace smb::infrastructure
