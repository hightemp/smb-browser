#include "application/ImportExportService.h"

#include "core/PathNormalizer.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <sodium.h>

namespace smb::application {

namespace {

constexpr int kExportSchemaVersion = 1;
constexpr int kEncryptedExportSchemaVersion = 1;

QString plainExportSchema() {
  return QStringLiteral("smb-browser.connections.export");
}

QString encryptedExportSchema() {
  return QStringLiteral("smb-browser.connections.encrypted-export");
}

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

smb::core::AppError validationError(const QString &details) {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::InvalidPath,
                                       smb::core::ErrorCategory::Validation,
                                       details, false);
}

smb::core::AppError credentialError(smb::core::ErrorCode code,
                                    const QString &details) {
  return smb::core::AppError::fromCode(code,
                                       smb::core::ErrorCategory::Credentials,
                                       details, false);
}

bool ensureSodiumInitialized() {
  static const bool initialized = sodium_init() >= 0;
  return initialized;
}

QByteArray randomBytes(int size) {
  QByteArray bytes(size, Qt::Uninitialized);
  randombytes_buf(bytes.data(), static_cast<size_t>(bytes.size()));
  return bytes;
}

QByteArray deriveExportKey(const QByteArray &passphrase,
                           const QByteArray &salt,
                           smb::core::AppError *error) {
  QByteArray key(crypto_secretbox_KEYBYTES, Qt::Uninitialized);
  const auto result = crypto_pwhash(
      reinterpret_cast<unsigned char *>(key.data()),
      static_cast<unsigned long long>(key.size()), passphrase.constData(),
      static_cast<unsigned long long>(passphrase.size()),
      reinterpret_cast<const unsigned char *>(salt.constData()),
      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
      crypto_pwhash_ALG_DEFAULT);
  if (result != 0) {
    *error = credentialError(smb::core::ErrorCode::CredentialStoreUnavailable,
                             QStringLiteral("Unable to derive export key."));
    sodium_memzero(key.data(), static_cast<size_t>(key.size()));
    return {};
  }

  return key;
}

smb::core::Result<QByteArray>
encryptExportPayload(QByteArray plaintext, const QByteArray &passphrase) {
  if (passphrase.isEmpty()) {
    return smb::core::Result<QByteArray>::failure(
        validationError(QStringLiteral("Encrypted export requires passphrase.")));
  }
  if (!ensureSodiumInitialized()) {
    return smb::core::Result<QByteArray>::failure(credentialError(
        smb::core::ErrorCode::CredentialStoreUnavailable,
        QStringLiteral("libsodium initialization failed.")));
  }

  const auto salt = randomBytes(crypto_pwhash_SALTBYTES);
  const auto nonce = randomBytes(crypto_secretbox_NONCEBYTES);

  smb::core::AppError keyError;
  auto key = deriveExportKey(passphrase, salt, &keyError);
  if (keyError.hasError()) {
    sodium_memzero(plaintext.data(), static_cast<size_t>(plaintext.size()));
    return smb::core::Result<QByteArray>::failure(keyError);
  }

  QByteArray ciphertext(plaintext.size() + crypto_secretbox_MACBYTES,
                        Qt::Uninitialized);
  crypto_secretbox_easy(
      reinterpret_cast<unsigned char *>(ciphertext.data()),
      reinterpret_cast<const unsigned char *>(plaintext.constData()),
      static_cast<unsigned long long>(plaintext.size()),
      reinterpret_cast<const unsigned char *>(nonce.constData()),
      reinterpret_cast<const unsigned char *>(key.constData()));
  sodium_memzero(key.data(), static_cast<size_t>(key.size()));
  sodium_memzero(plaintext.data(), static_cast<size_t>(plaintext.size()));

  QJsonObject root;
  root.insert(QStringLiteral("schema"), encryptedExportSchema());
  root.insert(QStringLiteral("version"), kEncryptedExportSchemaVersion);
  root.insert(QStringLiteral("kdf"), QStringLiteral("libsodium-crypto_pwhash"));
  root.insert(QStringLiteral("cipher"), QStringLiteral("crypto_secretbox"));
  root.insert(QStringLiteral("salt"), QString::fromLatin1(salt.toBase64()));
  root.insert(QStringLiteral("nonce"), QString::fromLatin1(nonce.toBase64()));
  root.insert(QStringLiteral("ciphertext"),
              QString::fromLatin1(ciphertext.toBase64()));

  return smb::core::Result<QByteArray>::success(
      QJsonDocument(root).toJson(QJsonDocument::Indented));
}

smb::core::Result<QByteArray>
decryptExportPayload(const QJsonObject &root, const QByteArray &passphrase) {
  if (passphrase.isEmpty()) {
    return smb::core::Result<QByteArray>::failure(
        validationError(QStringLiteral("Encrypted import requires passphrase.")));
  }
  if (!ensureSodiumInitialized()) {
    return smb::core::Result<QByteArray>::failure(credentialError(
        smb::core::ErrorCode::CredentialStoreUnavailable,
        QStringLiteral("libsodium initialization failed.")));
  }
  if (root.value(QStringLiteral("version")).toInt() !=
      kEncryptedExportSchemaVersion) {
    return smb::core::Result<QByteArray>::failure(
        validationError(QStringLiteral("Unsupported encrypted export version.")));
  }

  const auto salt = QByteArray::fromBase64(
      root.value(QStringLiteral("salt")).toString().toLatin1());
  const auto nonce = QByteArray::fromBase64(
      root.value(QStringLiteral("nonce")).toString().toLatin1());
  const auto ciphertext = QByteArray::fromBase64(
      root.value(QStringLiteral("ciphertext")).toString().toLatin1());
  if (salt.size() != crypto_pwhash_SALTBYTES ||
      nonce.size() != crypto_secretbox_NONCEBYTES ||
      ciphertext.size() < crypto_secretbox_MACBYTES) {
    return smb::core::Result<QByteArray>::failure(validationError(
        QStringLiteral("Encrypted export structure is invalid.")));
  }

  smb::core::AppError keyError;
  auto key = deriveExportKey(passphrase, salt, &keyError);
  if (keyError.hasError()) {
    return smb::core::Result<QByteArray>::failure(keyError);
  }

  QByteArray plaintext(ciphertext.size() - crypto_secretbox_MACBYTES,
                       Qt::Uninitialized);
  const auto result = crypto_secretbox_open_easy(
      reinterpret_cast<unsigned char *>(plaintext.data()),
      reinterpret_cast<const unsigned char *>(ciphertext.constData()),
      static_cast<unsigned long long>(ciphertext.size()),
      reinterpret_cast<const unsigned char *>(nonce.constData()),
      reinterpret_cast<const unsigned char *>(key.constData()));
  sodium_memzero(key.data(), static_cast<size_t>(key.size()));
  if (result != 0) {
    return smb::core::Result<QByteArray>::failure(
        credentialError(smb::core::ErrorCode::PermissionDenied,
                        QStringLiteral("Encrypted export decryption failed.")));
  }

  return smb::core::Result<QByteArray>::success(std::move(plaintext));
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
  root.insert(QStringLiteral("schema"), plainExportSchema());
  root.insert(QStringLiteral("version"), kExportSchemaVersion);
  root.insert(QStringLiteral("exportedAt"),
              QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  root.insert(QStringLiteral("connections"), connections);
  root.insert(QStringLiteral("groups"), groups);

  if (options.encryptExport) {
    return encryptExportPayload(
        QJsonDocument(root).toJson(QJsonDocument::Compact),
        options.encryptionPassphrase);
  }

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
  const auto schema = root.value(QStringLiteral("schema")).toString();
  if (schema == encryptedExportSchema()) {
    auto decrypted = decryptExportPayload(root, options.encryptionPassphrase);
    if (!decrypted.ok()) {
      return smb::core::Result<ImportResult>::failure(decrypted.error());
    }

    auto nestedOptions = options;
    nestedOptions.encryptionPassphrase.clear();
    auto imported = importConnections(decrypted.value(), nestedOptions);
    sodium_memzero(decrypted.value().data(),
                   static_cast<size_t>(decrypted.value().size()));
    return imported;
  }

  if (schema != plainExportSchema() ||
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
