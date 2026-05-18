#include "credentials/QtKeychainCredentialStore.h"

#include <QtTest/QtTest>

class QtKeychainCredentialStoreTest final : public QObject {
  Q_OBJECT

private slots:
  void availabilityCheckIsPassive() {
    smb::infrastructure::QtKeychainCredentialStore store(
        QStringLiteral("SMB Browser Tests"));

    const auto available = store.isAvailable();
    QVERIFY(available.ok());
    QVERIFY(available.value());
  }

  void realKeychainContract() {
    if (!qEnvironmentVariableIsSet("SMB_BROWSER_RUN_KEYCHAIN_TESTS")) {
      QSKIP("Set SMB_BROWSER_RUN_KEYCHAIN_TESTS=1 to run real keychain smoke "
            "tests.");
    }

    smb::infrastructure::QtKeychainCredentialStore store(
        QStringLiteral("SMB Browser Tests"));
    const smb::core::CredentialSecret initial{
        QByteArrayLiteral("synthetic-keychain-secret")};

    const auto saved = store.save(QStringLiteral("connection-1"), initial);
    QVERIFY2(saved.ok(), qPrintable(saved.error().sanitizedTechnicalDetails));
    QVERIFY(
        !saved.value().contains(QStringLiteral("synthetic-keychain-secret")));

    const auto loaded = store.load(saved.value());
    QVERIFY2(loaded.ok(), qPrintable(loaded.error().sanitizedTechnicalDetails));
    QCOMPARE(loaded.value().bytes,
             QByteArrayLiteral("synthetic-keychain-secret"));

    const smb::core::CredentialSecret updatedSecret{
        QByteArrayLiteral("updated-synthetic-keychain-secret")};
    const auto updated = store.update(saved.value(), updatedSecret);
    QVERIFY2(updated.ok(),
             qPrintable(updated.error().sanitizedTechnicalDetails));

    const auto reloaded = store.load(saved.value());
    QVERIFY2(reloaded.ok(),
             qPrintable(reloaded.error().sanitizedTechnicalDetails));
    QCOMPARE(reloaded.value().bytes,
             QByteArrayLiteral("updated-synthetic-keychain-secret"));

    const auto removed = store.remove(saved.value());
    QVERIFY2(removed.ok(),
             qPrintable(removed.error().sanitizedTechnicalDetails));
    QVERIFY(removed.value());
  }
};

QTEST_MAIN(QtKeychainCredentialStoreTest)

#include "test_qtkeychain_credential_store.moc"
