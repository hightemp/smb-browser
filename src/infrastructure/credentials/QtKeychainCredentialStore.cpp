#include "credentials/QtKeychainCredentialStore.h"

#include <QEventLoop>
#include <QUuid>
#include <qt5keychain/keychain.h>
#include <utility>

namespace smb::infrastructure {

namespace {

smb::core::AppError keychainError(QKeychain::Error error,
                                  const QString &details) {
  using smb::core::ErrorCategory;
  using smb::core::ErrorCode;

  switch (error) {
  case QKeychain::NoError:
    return smb::core::AppError::none();
  case QKeychain::EntryNotFound:
    return smb::core::AppError::fromCode(ErrorCode::CredentialNotFound,
                                         ErrorCategory::Credentials, details,
                                         false);
  case QKeychain::NoBackendAvailable:
  case QKeychain::NotImplemented:
    return smb::core::AppError::fromCode(ErrorCode::CredentialStoreUnavailable,
                                         ErrorCategory::Credentials, details,
                                         false);
  case QKeychain::AccessDenied:
  case QKeychain::AccessDeniedByUser:
  case QKeychain::CouldNotDeleteEntry:
    return smb::core::AppError::fromCode(ErrorCode::PermissionDenied,
                                         ErrorCategory::Credentials, details,
                                         false);
  case QKeychain::OtherError:
    return smb::core::AppError::fromCode(
        ErrorCode::Unknown, ErrorCategory::Credentials, details, false);
  }

  return smb::core::AppError::fromCode(
      ErrorCode::Unknown, ErrorCategory::Credentials, details, false);
}

template <typename Job> smb::core::AppError runJob(Job &job) {
  QEventLoop loop;
  job.setAutoDelete(false);
  job.setInsecureFallback(false);
  QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
  job.start();
  loop.exec();
  return keychainError(job.error(), job.errorString());
}

QString makeCredentialRef(const QString &ownerId) {
  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (ownerId.isEmpty()) {
    return QStringLiteral("qtkeychain:%1").arg(id);
  }

  return QStringLiteral("qtkeychain:%1:%2").arg(ownerId, id);
}

} // namespace

QtKeychainCredentialStore::QtKeychainCredentialStore(QString serviceName)
    : m_serviceName(std::move(serviceName)) {}

smb::core::Result<QString>
QtKeychainCredentialStore::save(const QString &ownerId,
                                const smb::core::CredentialSecret &secret) {
  const auto credentialRef = makeCredentialRef(ownerId);
  auto updated = update(credentialRef, secret);
  if (!updated.ok()) {
    return smb::core::Result<QString>::failure(updated.error());
  }

  return smb::core::Result<QString>::success(credentialRef);
}

smb::core::Result<smb::core::CredentialSecret>
QtKeychainCredentialStore::load(const QString &credentialRef) const {
  QKeychain::ReadPasswordJob job(m_serviceName);
  job.setKey(credentialRef);

  const auto error = runJob(job);
  if (error.hasError()) {
    return smb::core::Result<smb::core::CredentialSecret>::failure(error);
  }

  smb::core::CredentialSecret secret;
  secret.bytes = job.binaryData();
  return smb::core::Result<smb::core::CredentialSecret>::success(secret);
}

smb::core::Result<bool>
QtKeychainCredentialStore::update(const QString &credentialRef,
                                  const smb::core::CredentialSecret &secret) {
  QKeychain::WritePasswordJob job(m_serviceName);
  job.setKey(credentialRef);
  job.setBinaryData(secret.bytes);

  const auto error = runJob(job);
  if (error.hasError()) {
    return smb::core::Result<bool>::failure(error);
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool>
QtKeychainCredentialStore::remove(const QString &credentialRef) {
  QKeychain::DeletePasswordJob job(m_serviceName);
  job.setKey(credentialRef);

  const auto error = runJob(job);
  if (error.hasError()) {
    if (error.code == smb::core::ErrorCode::CredentialNotFound) {
      return smb::core::Result<bool>::success(false);
    }
    return smb::core::Result<bool>::failure(error);
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> QtKeychainCredentialStore::isAvailable() const {
  return smb::core::Result<bool>::success(true);
}

} // namespace smb::infrastructure
