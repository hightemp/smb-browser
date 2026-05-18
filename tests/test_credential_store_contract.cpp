#include "core/CredentialStore.h"

#include <QHash>
#include <QUuid>
#include <QtTest/QtTest>

namespace {

class InMemoryCredentialStore final : public smb::core::CredentialStore {
public:
  smb::core::Result<QString>
  save(const QString &ownerId,
       const smb::core::CredentialSecret &secret) override {
    const auto ref =
        QStringLiteral("credential:%1:%2")
            .arg(ownerId, QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_values.insert(ref, secret);
    return smb::core::Result<QString>::success(ref);
  }

  smb::core::Result<smb::core::CredentialSecret>
  load(const QString &credentialRef) const override {
    if (!m_values.contains(credentialRef)) {
      return smb::core::Result<smb::core::CredentialSecret>::failure(
          smb::core::AppError::fromCode(
              smb::core::ErrorCode::CredentialNotFound,
              smb::core::ErrorCategory::Credentials,
              QStringLiteral("Credential was not found."), false));
    }

    return smb::core::Result<smb::core::CredentialSecret>::success(
        m_values.value(credentialRef));
  }

  smb::core::Result<bool>
  update(const QString &credentialRef,
         const smb::core::CredentialSecret &secret) override {
    if (!m_values.contains(credentialRef)) {
      return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
          smb::core::ErrorCode::CredentialNotFound,
          smb::core::ErrorCategory::Credentials,
          QStringLiteral("Credential was not found."), false));
    }

    m_values.insert(credentialRef, secret);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> remove(const QString &credentialRef) override {
    return smb::core::Result<bool>::success(m_values.remove(credentialRef) > 0);
  }

  smb::core::Result<bool> isAvailable() const override {
    return smb::core::Result<bool>::success(true);
  }

private:
  QHash<QString, smb::core::CredentialSecret> m_values;
};

} // namespace

class CredentialStoreContractTest final : public QObject {
  Q_OBJECT

private slots:
  void saveLoadUpdateDelete() {
    InMemoryCredentialStore store;
    QVERIFY(store.isAvailable().ok());
    QVERIFY(store.isAvailable().value());

    const smb::core::CredentialSecret initial{
        QByteArrayLiteral("synthetic-secret")};
    const auto saved = store.save(QStringLiteral("connection-1"), initial);
    QVERIFY(saved.ok());
    QVERIFY(!saved.value().contains(QStringLiteral("synthetic-secret")));

    const auto loaded = store.load(saved.value());
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.value().bytes, QByteArrayLiteral("synthetic-secret"));

    const smb::core::CredentialSecret updatedSecret{
        QByteArrayLiteral("updated-synthetic-secret")};
    const auto updated = store.update(saved.value(), updatedSecret);
    QVERIFY(updated.ok());
    QVERIFY(updated.value());

    const auto reloaded = store.load(saved.value());
    QVERIFY(reloaded.ok());
    QCOMPARE(reloaded.value().bytes,
             QByteArrayLiteral("updated-synthetic-secret"));

    const auto removed = store.remove(saved.value());
    QVERIFY(removed.ok());
    QVERIFY(removed.value());

    const auto missing = store.load(saved.value());
    QVERIFY(!missing.ok());
    QVERIFY(missing.error().code == smb::core::ErrorCode::CredentialNotFound);
  }

  void missingCredentialDoesNotExposeSecret() {
    InMemoryCredentialStore store;
    const auto missing = store.load(QStringLiteral("credential:missing"));

    QVERIFY(!missing.ok());
    QVERIFY(!missing.error().sanitizedTechnicalDetails.contains(
        QStringLiteral("synthetic-secret")));
    QVERIFY(missing.error().category == smb::core::ErrorCategory::Credentials);
  }
};

QTEST_MAIN(CredentialStoreContractTest)

#include "test_credential_store_contract.moc"
