#include "storage/ConnectionRepository.h"

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
      QStringLiteral("Connection was not found: %1").arg(id), false);
}

QString nowIsoUtc() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
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

smb::core::AuthType authTypeFromString(const QString &value) {
  if (value == QStringLiteral("guest")) {
    return smb::core::AuthType::Guest;
  }
  if (value == QStringLiteral("anonymous")) {
    return smb::core::AuthType::Anonymous;
  }
  if (value == QStringLiteral("current_user")) {
    return smb::core::AuthType::CurrentUser;
  }
  return smb::core::AuthType::Password;
}

smb::core::ErrorCode errorCodeFromString(const QString &value) {
  if (value == QStringLiteral("invalid_path")) {
    return smb::core::ErrorCode::InvalidPath;
  }
  if (value == QStringLiteral("dns_error")) {
    return smb::core::ErrorCode::DnsError;
  }
  if (value == QStringLiteral("server_unavailable")) {
    return smb::core::ErrorCode::ServerUnavailable;
  }
  if (value == QStringLiteral("share_unavailable")) {
    return smb::core::ErrorCode::ShareUnavailable;
  }
  if (value == QStringLiteral("authentication_failed")) {
    return smb::core::ErrorCode::AuthenticationFailed;
  }
  if (value == QStringLiteral("permission_denied")) {
    return smb::core::ErrorCode::PermissionDenied;
  }
  if (value == QStringLiteral("timeout")) {
    return smb::core::ErrorCode::Timeout;
  }
  if (value == QStringLiteral("protocol_unsupported")) {
    return smb::core::ErrorCode::ProtocolUnsupported;
  }
  if (value == QStringLiteral("network_error")) {
    return smb::core::ErrorCode::NetworkError;
  }
  if (value == QStringLiteral("file_not_found")) {
    return smb::core::ErrorCode::FileNotFound;
  }
  if (value == QStringLiteral("already_exists")) {
    return smb::core::ErrorCode::AlreadyExists;
  }
  if (value == QStringLiteral("directory_not_empty")) {
    return smb::core::ErrorCode::DirectoryNotEmpty;
  }
  if (value == QStringLiteral("operation_cancelled")) {
    return smb::core::ErrorCode::OperationCancelled;
  }
  if (value == QStringLiteral("local_io_error")) {
    return smb::core::ErrorCode::LocalIoError;
  }
  if (value == QStringLiteral("storage_error")) {
    return smb::core::ErrorCode::StorageError;
  }
  if (value == QStringLiteral("credential_store_unavailable")) {
    return smb::core::ErrorCode::CredentialStoreUnavailable;
  }
  if (value == QStringLiteral("credential_not_found")) {
    return smb::core::ErrorCode::CredentialNotFound;
  }
  if (value == QStringLiteral("unknown")) {
    return smb::core::ErrorCode::Unknown;
  }
  return smb::core::ErrorCode::None;
}

QString nonNullString(const QString &value) {
  return value.isNull() ? QStringLiteral("") : value;
}

smb::core::Connection connectionFromQuery(const QSqlQuery &query) {
  const auto value = [&query](const QString &fieldName) {
    return query.value(query.record().indexOf(fieldName));
  };

  smb::core::Connection connection;
  connection.id = value(QStringLiteral("id")).toString();
  connection.name = value(QStringLiteral("name")).toString();
  connection.inputPath = value(QStringLiteral("input_path")).toString();
  connection.normalizedUri = value(QStringLiteral("normalized_uri")).toString();
  connection.server = value(QStringLiteral("server")).toString();
  connection.share = value(QStringLiteral("share")).toString();
  connection.initialRemotePath =
      value(QStringLiteral("initial_remote_path")).toString();
  connection.domain = value(QStringLiteral("domain")).toString();
  connection.username = value(QStringLiteral("username")).toString();
  connection.authType =
      authTypeFromString(value(QStringLiteral("auth_type")).toString());
  connection.credentialRef = value(QStringLiteral("credential_ref")).toString();
  connection.comment = value(QStringLiteral("comment")).toString();
  connection.groupId = value(QStringLiteral("group_id")).toString();
  connection.isFavorite = value(QStringLiteral("is_favorite")).toInt() != 0;
  connection.lastOpenedAt =
      dateFromValue(value(QStringLiteral("last_opened_at")));
  connection.createdAt = dateFromValue(value(QStringLiteral("created_at")));
  connection.updatedAt = dateFromValue(value(QStringLiteral("updated_at")));
  connection.lastErrorCode =
      errorCodeFromString(value(QStringLiteral("last_error_code")).toString());
  connection.lastErrorMessage =
      value(QStringLiteral("last_error_message")).toString();
  connection.lastSuccessfulCheckAt =
      dateFromValue(value(QStringLiteral("last_successful_check_at")));
  return connection;
}

void bindConnection(QSqlQuery &query, const smb::core::Connection &connection) {
  query.bindValue(QStringLiteral(":id"), connection.id);
  query.bindValue(QStringLiteral(":name"), nonNullString(connection.name));
  query.bindValue(QStringLiteral(":input_path"),
                  nonNullString(connection.inputPath));
  query.bindValue(QStringLiteral(":normalized_uri"),
                  nonNullString(connection.normalizedUri));
  query.bindValue(QStringLiteral(":server"), nonNullString(connection.server));
  query.bindValue(QStringLiteral(":share"), nonNullString(connection.share));
  query.bindValue(QStringLiteral(":initial_remote_path"),
                  nonNullString(connection.initialRemotePath));
  query.bindValue(QStringLiteral(":domain"), nonNullString(connection.domain));
  query.bindValue(QStringLiteral(":username"),
                  nonNullString(connection.username));
  query.bindValue(QStringLiteral(":auth_type"),
                  smb::core::toString(connection.authType));
  query.bindValue(QStringLiteral(":credential_ref"),
                  nonNullString(connection.credentialRef));
  query.bindValue(QStringLiteral(":comment"),
                  nonNullString(connection.comment));
  query.bindValue(QStringLiteral(":group_id"), connection.groupId.isEmpty()
                                                   ? QVariant(QVariant::String)
                                                   : connection.groupId);
  query.bindValue(QStringLiteral(":is_favorite"),
                  connection.isFavorite ? 1 : 0);
  query.bindValue(QStringLiteral(":last_opened_at"),
                  dateToValue(connection.lastOpenedAt));
  query.bindValue(QStringLiteral(":created_at"),
                  dateToValue(connection.createdAt));
  query.bindValue(QStringLiteral(":updated_at"),
                  dateToValue(connection.updatedAt));
  query.bindValue(QStringLiteral(":last_error_code"),
                  smb::core::toString(connection.lastErrorCode));
  query.bindValue(QStringLiteral(":last_error_message"),
                  nonNullString(connection.lastErrorMessage));
  query.bindValue(QStringLiteral(":last_successful_check_at"),
                  dateToValue(connection.lastSuccessfulCheckAt));
}

} // namespace

ConnectionRepository::ConnectionRepository(QSqlDatabase database)
    : m_database(std::move(database)) {}

smb::core::Result<smb::core::Connection>
ConnectionRepository::add(smb::core::Connection connection) {
  if (connection.id.isEmpty()) {
    connection.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  }
  const auto now = QDateTime::currentDateTimeUtc();
  if (!connection.createdAt.isValid()) {
    connection.createdAt = now;
  }
  connection.updatedAt = now;

  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "INSERT INTO connections("
      "id, name, input_path, normalized_uri, server, share, "
      "initial_remote_path, "
      "domain, username, auth_type, credential_ref, comment, group_id, "
      "is_favorite, "
      "last_opened_at, created_at, updated_at, last_error_code, "
      "last_error_message, "
      "last_successful_check_at) "
      "VALUES(:id, :name, :input_path, :normalized_uri, :server, :share, "
      ":initial_remote_path, :domain, :username, :auth_type, :credential_ref, "
      ":comment, :group_id, :is_favorite, :last_opened_at, :created_at, "
      ":updated_at, "
      ":last_error_code, :last_error_message, :last_successful_check_at)"));
  bindConnection(query, connection);

  if (!query.exec()) {
    return smb::core::Result<smb::core::Connection>::failure(
        storageError(query.lastError().text()));
  }

  return smb::core::Result<smb::core::Connection>::success(
      std::move(connection));
}

smb::core::Result<smb::core::Connection>
ConnectionRepository::getById(const QString &id) const {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("SELECT * FROM connections WHERE id = :id"));
  query.bindValue(QStringLiteral(":id"), id);

  if (!query.exec()) {
    return smb::core::Result<smb::core::Connection>::failure(
        storageError(query.lastError().text()));
  }
  if (!query.next()) {
    return smb::core::Result<smb::core::Connection>::failure(notFoundError(id));
  }

  return smb::core::Result<smb::core::Connection>::success(
      connectionFromQuery(query));
}

smb::core::Result<QVector<smb::core::Connection>>
ConnectionRepository::list() const {
  QSqlQuery query(m_database);
  if (!query.exec(
          QStringLiteral("SELECT * FROM connections ORDER BY is_favorite DESC, "
                         "name COLLATE NOCASE ASC"))) {
    return smb::core::Result<QVector<smb::core::Connection>>::failure(
        storageError(query.lastError().text()));
  }

  QVector<smb::core::Connection> connections;
  while (query.next()) {
    connections.push_back(connectionFromQuery(query));
  }

  return smb::core::Result<QVector<smb::core::Connection>>::success(
      std::move(connections));
}

smb::core::Result<smb::core::Connection>
ConnectionRepository::update(smb::core::Connection connection) {
  if (connection.id.isEmpty()) {
    return smb::core::Result<smb::core::Connection>::failure(
        notFoundError(connection.id));
  }
  connection.updatedAt = QDateTime::currentDateTimeUtc();

  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "UPDATE connections SET "
      "name = :name, input_path = :input_path, normalized_uri = "
      ":normalized_uri, "
      "server = :server, share = :share, initial_remote_path = "
      ":initial_remote_path, "
      "domain = :domain, username = :username, auth_type = :auth_type, "
      "credential_ref = :credential_ref, comment = :comment, group_id = "
      ":group_id, "
      "is_favorite = :is_favorite, last_opened_at = :last_opened_at, "
      "created_at = :created_at, updated_at = :updated_at, "
      "last_error_code = :last_error_code, last_error_message = "
      ":last_error_message, "
      "last_successful_check_at = :last_successful_check_at "
      "WHERE id = :id"));
  bindConnection(query, connection);

  if (!query.exec()) {
    return smb::core::Result<smb::core::Connection>::failure(
        storageError(query.lastError().text()));
  }
  if (query.numRowsAffected() == 0) {
    return smb::core::Result<smb::core::Connection>::failure(
        notFoundError(connection.id));
  }

  return smb::core::Result<smb::core::Connection>::success(
      std::move(connection));
}

smb::core::Result<bool> ConnectionRepository::remove(const QString &id) {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("DELETE FROM connections WHERE id = :id"));
  query.bindValue(QStringLiteral(":id"), id);
  if (!query.exec()) {
    return smb::core::Result<bool>::failure(
        storageError(query.lastError().text()));
  }

  return smb::core::Result<bool>::success(query.numRowsAffected() > 0);
}

smb::core::Result<bool>
ConnectionRepository::updateLastOpened(const QString &id,
                                       const QDateTime &openedAt) {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral("UPDATE connections SET last_opened_at = "
                               ":last_opened_at, updated_at = :updated_at "
                               "WHERE id = :id"));
  query.bindValue(QStringLiteral(":last_opened_at"), dateToValue(openedAt));
  query.bindValue(QStringLiteral(":updated_at"), nowIsoUtc());
  query.bindValue(QStringLiteral(":id"), id);
  if (!query.exec()) {
    return smb::core::Result<bool>::failure(
        storageError(query.lastError().text()));
  }

  return smb::core::Result<bool>::success(query.numRowsAffected() > 0);
}

smb::core::Result<bool>
ConnectionRepository::updateLastError(const QString &id,
                                      smb::core::ErrorCode errorCode,
                                      const QString &sanitizedMessage) {
  QSqlQuery query(m_database);
  query.prepare(QStringLiteral(
      "UPDATE connections SET last_error_code = :last_error_code, "
      "last_error_message = :last_error_message, updated_at = :updated_at "
      "WHERE id = :id"));
  query.bindValue(QStringLiteral(":last_error_code"),
                  smb::core::toString(errorCode));
  query.bindValue(QStringLiteral(":last_error_message"), sanitizedMessage);
  query.bindValue(QStringLiteral(":updated_at"), nowIsoUtc());
  query.bindValue(QStringLiteral(":id"), id);
  if (!query.exec()) {
    return smb::core::Result<bool>::failure(
        storageError(query.lastError().text()));
  }

  return smb::core::Result<bool>::success(query.numRowsAffected() > 0);
}

smb::core::Result<bool>
ConnectionRepository::updateLastSuccessfulCheck(const QString &id,
                                                const QDateTime &checkedAt) {
  QSqlQuery query(m_database);
  query.prepare(
      QStringLiteral("UPDATE connections SET last_successful_check_at = "
                     ":last_successful_check_at, "
                     "last_error_code = 'none', last_error_message = '', "
                     "updated_at = :updated_at "
                     "WHERE id = :id"));
  query.bindValue(QStringLiteral(":last_successful_check_at"),
                  dateToValue(checkedAt));
  query.bindValue(QStringLiteral(":updated_at"), nowIsoUtc());
  query.bindValue(QStringLiteral(":id"), id);
  if (!query.exec()) {
    return smb::core::Result<bool>::failure(
        storageError(query.lastError().text()));
  }

  return smb::core::Result<bool>::success(query.numRowsAffected() > 0);
}

} // namespace smb::infrastructure
