#include "application/ImportExportService.h"

#include "core/PathNormalizer.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace smb::application {

namespace {

constexpr int kExportSchemaVersion = 1;

void addDateTime(QJsonObject &object, const QString &key,
                 const QDateTime &dateTime) {
  if (dateTime.isValid()) {
    object.insert(key, dateTime.toUTC().toString(Qt::ISODateWithMs));
  }
}

QDateTime dateTimeFromJson(const QJsonObject &object, const QString &key) {
  const auto value = object.value(key);
  if (!value.isString()) {
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
  const QVector<smb::core::ErrorCode> knownCodes{
      smb::core::ErrorCode::None,
      smb::core::ErrorCode::InvalidPath,
      smb::core::ErrorCode::DnsError,
      smb::core::ErrorCode::ServerUnavailable,
      smb::core::ErrorCode::ShareUnavailable,
      smb::core::ErrorCode::AuthenticationFailed,
      smb::core::ErrorCode::PermissionDenied,
      smb::core::ErrorCode::Timeout,
      smb::core::ErrorCode::ProtocolUnsupported,
      smb::core::ErrorCode::NetworkError,
      smb::core::ErrorCode::FileNotFound,
      smb::core::ErrorCode::AlreadyExists,
      smb::core::ErrorCode::DirectoryNotEmpty,
      smb::core::ErrorCode::OperationCancelled,
      smb::core::ErrorCode::LocalIoError,
      smb::core::ErrorCode::StorageError,
      smb::core::ErrorCode::CredentialStoreUnavailable,
      smb::core::ErrorCode::CredentialNotFound,
      smb::core::ErrorCode::Unknown,
  };
  for (const auto code : knownCodes) {
    if (smb::core::toString(code) == value) {
      return code;
    }
  }
  return smb::core::ErrorCode::None;
}

} // namespace

smb::core::Result<QByteArray> ImportExportService::exportConnections(
    const ExportPayload &payload, const ExportOptions &options) const {
  if (options.includePlainTextPasswords &&
      !options.confirmPlainTextPasswordExport) {
    return smb::core::Result<QByteArray>::failure(
        smb::core::AppError::fromCode(
            smb::core::ErrorCode::Unknown, smb::core::ErrorCategory::Validation,
            QStringLiteral("Plain-text password export requires explicit "
                           "confirmation.")));
  }
  if (options.includePlainTextPasswords && options.credentialStore == nullptr) {
    return smb::core::Result<QByteArray>::failure(
        smb::core::AppError::fromCode(
            smb::core::ErrorCode::CredentialStoreUnavailable,
            smb::core::ErrorCategory::Credentials,
            QStringLiteral("Credential store is required for plain-text "
                           "password export.")));
  }

  QJsonArray connections;
  for (const auto &connection : payload.connections) {
    auto item = connectionToJson(connection, options);
    if (!item.ok()) {
      return smb::core::Result<QByteArray>::failure(item.error());
    }
    connections.push_back(item.value());
  }

  QJsonArray groups;
  for (const auto &group : payload.groups) {
    groups.push_back(groupToJson(group));
  }

  QJsonObject root;
  root.insert(QStringLiteral("schema"),
              QStringLiteral("smb-browser.connections.export"));
  root.insert(QStringLiteral("version"), kExportSchemaVersion);
  root.insert(QStringLiteral("exportedAt"),
              QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  root.insert(QStringLiteral("connections"), connections);
  root.insert(QStringLiteral("groups"), groups);

  return smb::core::Result<QByteArray>::success(
      QJsonDocument(root).toJson(QJsonDocument::Indented));
}

smb::core::Result<ImportResult> ImportExportService::importConnections(
    const QByteArray &bytes, const ImportOptions &options) const {
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(bytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    return smb::core::Result<ImportResult>::failure(
        smb::core::AppError::fromCode(smb::core::ErrorCode::InvalidPath,
                                      smb::core::ErrorCategory::Validation,
                                      parseError.errorString()));
  }

  const auto root = document.object();
  if (root.value(QStringLiteral("schema")).toString() !=
          QStringLiteral("smb-browser.connections.export") ||
      root.value(QStringLiteral("version")).toInt() != kExportSchemaVersion) {
    return smb::core::Result<ImportResult>::failure(
        smb::core::AppError::fromCode(
            smb::core::ErrorCode::InvalidPath,
            smb::core::ErrorCategory::Validation,
            QStringLiteral("Unsupported import schema or version.")));
  }

  ImportResult result;
  const auto groupValues = root.value(QStringLiteral("groups")).toArray();
  result.groups.reserve(groupValues.size());
  for (const auto &value : groupValues) {
    if (value.isObject()) {
      result.groups.push_back(groupFromJson(value.toObject()));
    }
  }

  auto existingIds = options.existingConnectionIds;
  const auto connectionValues =
      root.value(QStringLiteral("connections")).toArray();
  result.connections.reserve(connectionValues.size());
  for (int index = 0; index < connectionValues.size(); ++index) {
    const auto value = connectionValues.at(index);
    if (!value.isObject()) {
      result.errors.push_back(ImportRecordError{
          index, QString(),
          smb::core::AppError::fromCode(
              smb::core::ErrorCode::InvalidPath,
              smb::core::ErrorCategory::Validation,
              QStringLiteral("Connection entry is not an object."))});
      continue;
    }

    const auto object = value.toObject();
    auto parsed = connectionFromJson(object, index);
    if (!parsed.ok()) {
      result.errors.push_back(ImportRecordError{
          index, object.value(QStringLiteral("name")).toString(),
          parsed.error()});
      continue;
    }

    auto connection = parsed.value();
    if (!connection.id.isEmpty() && existingIds.contains(connection.id)) {
      if (options.duplicatePolicy == DuplicatePolicy::Skip) {
        ++result.skippedDuplicates;
        continue;
      }
      if (options.duplicatePolicy == DuplicatePolicy::CreateCopy) {
        connection.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        connection.name = connection.name.isEmpty()
                              ? QStringLiteral("Imported copy")
                              : QStringLiteral("%1 Copy").arg(connection.name);
      }
    }

    if (!connection.id.isEmpty()) {
      existingIds.insert(connection.id);
    }
    result.connections.push_back(std::move(connection));
  }

  return smb::core::Result<ImportResult>::success(std::move(result));
}

smb::core::Result<QJsonObject> ImportExportService::connectionToJson(
    const smb::core::Connection &connection,
    const ExportOptions &options) const {
  QJsonObject object;
  object.insert(QStringLiteral("id"), connection.id);
  object.insert(QStringLiteral("name"), connection.name);
  object.insert(QStringLiteral("inputPath"), connection.inputPath);
  object.insert(QStringLiteral("normalizedUri"), connection.normalizedUri);
  object.insert(QStringLiteral("server"), connection.server);
  object.insert(QStringLiteral("share"), connection.share);
  object.insert(QStringLiteral("initialRemotePath"),
                connection.initialRemotePath);
  object.insert(QStringLiteral("domain"), connection.domain);
  object.insert(QStringLiteral("username"), connection.username);
  object.insert(QStringLiteral("authType"), smb::core::toString(connection.authType));
  object.insert(QStringLiteral("comment"), connection.comment);
  object.insert(QStringLiteral("groupId"), connection.groupId);
  object.insert(QStringLiteral("favorite"), connection.isFavorite);
  object.insert(QStringLiteral("lastErrorCode"),
                smb::core::toString(connection.lastErrorCode));
  object.insert(QStringLiteral("lastErrorMessage"),
                m_sanitizer.sanitize(connection.lastErrorMessage));

  addDateTime(object, QStringLiteral("lastOpenedAt"), connection.lastOpenedAt);
  addDateTime(object, QStringLiteral("createdAt"), connection.createdAt);
  addDateTime(object, QStringLiteral("updatedAt"), connection.updatedAt);
  addDateTime(object, QStringLiteral("lastSuccessfulCheckAt"),
              connection.lastSuccessfulCheckAt);

  if (options.includePlainTextPasswords &&
      connection.authType == smb::core::AuthType::Password) {
    if (connection.credentialRef.isEmpty()) {
      return smb::core::Result<QJsonObject>::failure(
          smb::core::AppError::fromCode(
              smb::core::ErrorCode::CredentialNotFound,
              smb::core::ErrorCategory::Credentials,
              QStringLiteral("Connection has no credential reference.")));
    }

    const auto secret = options.credentialStore->load(connection.credentialRef);
    if (!secret.ok()) {
      return smb::core::Result<QJsonObject>::failure(secret.error());
    }

    object.insert(QStringLiteral("plainTextPassword"),
                  QString::fromUtf8(secret.value().bytes));
  }

  return smb::core::Result<QJsonObject>::success(std::move(object));
}

QJsonObject
ImportExportService::groupToJson(const smb::core::ConnectionGroup &group) const {
  QJsonObject object;
  object.insert(QStringLiteral("id"), group.id);
  object.insert(QStringLiteral("name"), group.name);
  object.insert(QStringLiteral("sortOrder"), group.sortOrder);
  addDateTime(object, QStringLiteral("createdAt"), group.createdAt);
  addDateTime(object, QStringLiteral("updatedAt"), group.updatedAt);
  return object;
}

smb::core::Result<smb::core::Connection>
ImportExportService::connectionFromJson(const QJsonObject &object,
                                        int /*index*/) const {
  const auto path = object.value(QStringLiteral("normalizedUri")).toString(
      object.value(QStringLiteral("inputPath")).toString());
  const auto normalized = smb::core::PathNormalizer::normalizeSmbPath(path);
  if (!normalized.ok()) {
    return smb::core::Result<smb::core::Connection>::failure(
        normalized.error());
  }

  auto connection = smb::core::Connection::createEmpty();
  connection.id = object.value(QStringLiteral("id")).toString();
  connection.name = object.value(QStringLiteral("name")).toString();
  connection.inputPath = object.value(QStringLiteral("inputPath")).toString(
      normalized.value().inputPath);
  connection.normalizedUri = normalized.value().normalizedUri;
  connection.server = normalized.value().server;
  connection.share = normalized.value().share;
  connection.initialRemotePath = normalized.value().initialRemotePath;
  connection.domain = object.value(QStringLiteral("domain")).toString();
  connection.username = object.value(QStringLiteral("username")).toString();
  connection.authType = authTypeFromString(
      object.value(QStringLiteral("authType")).toString());
  connection.credentialRef.clear();
  connection.comment = object.value(QStringLiteral("comment")).toString();
  connection.groupId = object.value(QStringLiteral("groupId")).toString();
  connection.isFavorite = object.value(QStringLiteral("favorite")).toBool();
  connection.lastErrorCode = errorCodeFromString(
      object.value(QStringLiteral("lastErrorCode")).toString());
  connection.lastErrorMessage = m_sanitizer.sanitize(
      object.value(QStringLiteral("lastErrorMessage")).toString());
  connection.lastOpenedAt =
      dateTimeFromJson(object, QStringLiteral("lastOpenedAt"));
  connection.createdAt = dateTimeFromJson(object, QStringLiteral("createdAt"));
  connection.updatedAt = dateTimeFromJson(object, QStringLiteral("updatedAt"));
  connection.lastSuccessfulCheckAt =
      dateTimeFromJson(object, QStringLiteral("lastSuccessfulCheckAt"));

  return smb::core::Result<smb::core::Connection>::success(
      std::move(connection));
}

smb::core::ConnectionGroup
ImportExportService::groupFromJson(const QJsonObject &object) const {
  smb::core::ConnectionGroup group;
  group.id = object.value(QStringLiteral("id")).toString();
  group.name = object.value(QStringLiteral("name")).toString();
  group.sortOrder = object.value(QStringLiteral("sortOrder")).toInt();
  group.createdAt = dateTimeFromJson(object, QStringLiteral("createdAt"));
  group.updatedAt = dateTimeFromJson(object, QStringLiteral("updatedAt"));
  return group;
}

QString ImportExportService::dateTimeToString(const QDateTime &dateTime) {
  return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODateWithMs)
                            : QString();
}

} // namespace smb::application
