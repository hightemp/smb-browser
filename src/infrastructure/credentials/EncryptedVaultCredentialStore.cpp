#include "credentials/EncryptedVaultCredentialStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <sodium.h>
#include <utility>

namespace smb::infrastructure {

namespace {

constexpr int VaultVersion = 1;

smb::core::AppError credentialError(smb::core::ErrorCode code,
                                    const QString &details) {
  return smb::core::AppError::fromCode(
      code, smb::core::ErrorCategory::Credentials, details, false);
}

smb::core::AppError localIoError(const QString &details) {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::LocalIoError,
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

QString makeCredentialRef(const QString &ownerId) {
  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (ownerId.isEmpty()) {
    return QStringLiteral("vault:%1").arg(id);
  }

  return QStringLiteral("vault:%1:%2").arg(ownerId, id);
}

QJsonObject emptyPayload() {
  QJsonObject payload;
  payload.insert(QStringLiteral("credentials"), QJsonObject{});
  return payload;
}

QJsonObject credentialsObject(const QJsonObject &payload) {
  return payload.value(QStringLiteral("credentials")).toObject();
}

QByteArray deriveKey(const QByteArray &masterPassword, const QByteArray &salt,
                     smb::core::AppError *error) {
  QByteArray key(crypto_secretbox_KEYBYTES, Qt::Uninitialized);
  const auto result = crypto_pwhash(
      reinterpret_cast<unsigned char *>(key.data()),
      static_cast<unsigned long long>(key.size()), masterPassword.constData(),
      static_cast<unsigned long long>(masterPassword.size()),
      reinterpret_cast<const unsigned char *>(salt.constData()),
      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
      crypto_pwhash_ALG_DEFAULT);
  if (result != 0) {
    *error = credentialError(smb::core::ErrorCode::CredentialStoreUnavailable,
                             QStringLiteral("Unable to derive vault key."));
    sodium_memzero(key.data(), static_cast<size_t>(key.size()));
    return {};
  }

  return key;
}

smb::core::Result<QJsonObject>
decryptPayload(const QString &vaultPath, const QByteArray &masterPassword) {
  if (!ensureSodiumInitialized()) {
    return smb::core::Result<QJsonObject>::failure(
        credentialError(smb::core::ErrorCode::CredentialStoreUnavailable,
                        QStringLiteral("libsodium initialization failed.")));
  }

  if (!QFile::exists(vaultPath)) {
    return smb::core::Result<QJsonObject>::success(emptyPayload());
  }

  QFile file(vaultPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return smb::core::Result<QJsonObject>::failure(
        localIoError(QStringLiteral("Unable to open encrypted vault.")));
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return smb::core::Result<QJsonObject>::failure(credentialError(
        smb::core::ErrorCode::StorageError,
        QStringLiteral("Encrypted vault file is not valid JSON.")));
  }

  const auto object = document.object();
  if (object.value(QStringLiteral("version")).toInt() != VaultVersion) {
    return smb::core::Result<QJsonObject>::failure(credentialError(
        smb::core::ErrorCode::StorageError,
        QStringLiteral("Encrypted vault version is not supported.")));
  }

  const auto salt = QByteArray::fromBase64(
      object.value(QStringLiteral("salt")).toString().toLatin1());
  const auto nonce = QByteArray::fromBase64(
      object.value(QStringLiteral("nonce")).toString().toLatin1());
  const auto ciphertext = QByteArray::fromBase64(
      object.value(QStringLiteral("ciphertext")).toString().toLatin1());
  if (salt.size() != crypto_pwhash_SALTBYTES ||
      nonce.size() != crypto_secretbox_NONCEBYTES ||
      ciphertext.size() < crypto_secretbox_MACBYTES) {
    return smb::core::Result<QJsonObject>::failure(credentialError(
        smb::core::ErrorCode::StorageError,
        QStringLiteral("Encrypted vault structure is invalid.")));
  }

  smb::core::AppError keyError;
  auto key = deriveKey(masterPassword, salt, &keyError);
  if (keyError.hasError()) {
    return smb::core::Result<QJsonObject>::failure(keyError);
  }

  QByteArray plaintext(ciphertext.size() - crypto_secretbox_MACBYTES,
                       Qt::Uninitialized);
  const auto openResult = crypto_secretbox_open_easy(
      reinterpret_cast<unsigned char *>(plaintext.data()),
      reinterpret_cast<const unsigned char *>(ciphertext.constData()),
      static_cast<unsigned long long>(ciphertext.size()),
      reinterpret_cast<const unsigned char *>(nonce.constData()),
      reinterpret_cast<const unsigned char *>(key.constData()));
  sodium_memzero(key.data(), static_cast<size_t>(key.size()));
  if (openResult != 0) {
    return smb::core::Result<QJsonObject>::failure(
        credentialError(smb::core::ErrorCode::PermissionDenied,
                        QStringLiteral("Encrypted vault decryption failed.")));
  }

  const auto payloadDocument = QJsonDocument::fromJson(plaintext);
  sodium_memzero(plaintext.data(), static_cast<size_t>(plaintext.size()));
  if (!payloadDocument.isObject()) {
    return smb::core::Result<QJsonObject>::failure(
        credentialError(smb::core::ErrorCode::StorageError,
                        QStringLiteral("Encrypted vault payload is invalid.")));
  }

  return smb::core::Result<QJsonObject>::success(payloadDocument.object());
}

smb::core::AppError writePayload(const QString &vaultPath,
                                 const QByteArray &masterPassword,
                                 const QJsonObject &payload) {
  if (!ensureSodiumInitialized()) {
    return credentialError(smb::core::ErrorCode::CredentialStoreUnavailable,
                           QStringLiteral("libsodium initialization failed."));
  }

  const auto salt = randomBytes(crypto_pwhash_SALTBYTES);
  const auto nonce = randomBytes(crypto_secretbox_NONCEBYTES);

  smb::core::AppError keyError;
  auto key = deriveKey(masterPassword, salt, &keyError);
  if (keyError.hasError()) {
    return keyError;
  }

  auto plaintext = QJsonDocument(payload).toJson(QJsonDocument::Compact);
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

  QJsonObject vault;
  vault.insert(QStringLiteral("version"), VaultVersion);
  vault.insert(QStringLiteral("kdf"),
               QStringLiteral("libsodium-crypto_pwhash"));
  vault.insert(QStringLiteral("cipher"), QStringLiteral("crypto_secretbox"));
  vault.insert(QStringLiteral("salt"), QString::fromLatin1(salt.toBase64()));
  vault.insert(QStringLiteral("nonce"), QString::fromLatin1(nonce.toBase64()));
  vault.insert(QStringLiteral("ciphertext"),
               QString::fromLatin1(ciphertext.toBase64()));

  const QFileInfo fileInfo(vaultPath);
  if (!fileInfo.dir().exists() &&
      !QDir().mkpath(fileInfo.dir().absolutePath())) {
    return localIoError(
        QStringLiteral("Unable to create encrypted vault directory."));
  }

  QFile file(vaultPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return localIoError(QStringLiteral("Unable to write encrypted vault."));
  }

  file.write(QJsonDocument(vault).toJson(QJsonDocument::Compact));
  if (!file.flush()) {
    return localIoError(QStringLiteral("Unable to flush encrypted vault."));
  }

  return smb::core::AppError::none();
}

} // namespace

EncryptedVaultCredentialStore::EncryptedVaultCredentialStore(
    QString vaultPath, QByteArray masterPassword)
    : m_vaultPath(std::move(vaultPath)),
      m_masterPassword(std::move(masterPassword)) {}

EncryptedVaultCredentialStore::~EncryptedVaultCredentialStore() {
  if (!m_masterPassword.isEmpty()) {
    sodium_memzero(m_masterPassword.data(),
                   static_cast<size_t>(m_masterPassword.size()));
  }
}

smb::core::Result<QString>
EncryptedVaultCredentialStore::save(const QString &ownerId,
                                    const smb::core::CredentialSecret &secret) {
  const auto credentialRef = makeCredentialRef(ownerId);
  auto updated = update(credentialRef, secret);
  if (!updated.ok()) {
    return smb::core::Result<QString>::failure(updated.error());
  }

  return smb::core::Result<QString>::success(credentialRef);
}

smb::core::Result<smb::core::CredentialSecret>
EncryptedVaultCredentialStore::load(const QString &credentialRef) const {
  auto payload = decryptPayload(m_vaultPath, m_masterPassword);
  if (!payload.ok()) {
    return smb::core::Result<smb::core::CredentialSecret>::failure(
        payload.error());
  }

  const auto credentials = credentialsObject(payload.value());
  if (!credentials.contains(credentialRef)) {
    return smb::core::Result<smb::core::CredentialSecret>::failure(
        credentialError(smb::core::ErrorCode::CredentialNotFound,
                        QStringLiteral("Credential was not found.")));
  }

  smb::core::CredentialSecret secret;
  secret.bytes = QByteArray::fromBase64(
      credentials.value(credentialRef).toString().toLatin1());
  return smb::core::Result<smb::core::CredentialSecret>::success(secret);
}

smb::core::Result<bool> EncryptedVaultCredentialStore::update(
    const QString &credentialRef, const smb::core::CredentialSecret &secret) {
  auto payload = decryptPayload(m_vaultPath, m_masterPassword);
  if (!payload.ok()) {
    return smb::core::Result<bool>::failure(payload.error());
  }

  auto object = payload.value();
  auto credentials = credentialsObject(object);
  credentials.insert(credentialRef,
                     QString::fromLatin1(secret.bytes.toBase64()));
  object.insert(QStringLiteral("credentials"), credentials);

  const auto error = writePayload(m_vaultPath, m_masterPassword, object);
  if (error.hasError()) {
    return smb::core::Result<bool>::failure(error);
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool>
EncryptedVaultCredentialStore::remove(const QString &credentialRef) {
  auto payload = decryptPayload(m_vaultPath, m_masterPassword);
  if (!payload.ok()) {
    return smb::core::Result<bool>::failure(payload.error());
  }

  auto object = payload.value();
  auto credentials = credentialsObject(object);
  const auto removed = credentials.contains(credentialRef);
  credentials.remove(credentialRef);
  object.insert(QStringLiteral("credentials"), credentials);

  const auto error = writePayload(m_vaultPath, m_masterPassword, object);
  if (error.hasError()) {
    return smb::core::Result<bool>::failure(error);
  }

  return smb::core::Result<bool>::success(removed);
}

smb::core::Result<bool> EncryptedVaultCredentialStore::isAvailable() const {
  if (!ensureSodiumInitialized()) {
    return smb::core::Result<bool>::failure(
        credentialError(smb::core::ErrorCode::CredentialStoreUnavailable,
                        QStringLiteral("libsodium initialization failed.")));
  }

  return smb::core::Result<bool>::success(true);
}

} // namespace smb::infrastructure
