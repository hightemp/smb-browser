#include "credentials/EncryptedVaultCredentialStore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class EncryptedVaultCredentialStoreTest final : public QObject {
  Q_OBJECT

private slots:
  void saveLoadUpdateDelete() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto vaultPath = tempDir.filePath(QStringLiteral("vault.json"));
    smb::infrastructure::EncryptedVaultCredentialStore store(
        vaultPath, QByteArrayLiteral("synthetic-master-password"));

    QVERIFY(store.isAvailable().ok());

    const smb::core::CredentialSecret initial{
        QByteArrayLiteral("synthetic-vault-secret")};
    const auto saved = store.save(QStringLiteral("connection-1"), initial);
    QVERIFY2(saved.ok(), qPrintable(saved.error().sanitizedTechnicalDetails));
    QVERIFY(!saved.value().contains(QStringLiteral("synthetic-vault-secret")));

    const auto loaded = store.load(saved.value());
    QVERIFY2(loaded.ok(), qPrintable(loaded.error().sanitizedTechnicalDetails));
    QCOMPARE(loaded.value().bytes, QByteArrayLiteral("synthetic-vault-secret"));

    const smb::core::CredentialSecret updatedSecret{
        QByteArrayLiteral("updated-synthetic-vault-secret")};
    const auto updated = store.update(saved.value(), updatedSecret);
    QVERIFY2(updated.ok(),
             qPrintable(updated.error().sanitizedTechnicalDetails));

    const auto reloaded = store.load(saved.value());
    QVERIFY2(reloaded.ok(),
             qPrintable(reloaded.error().sanitizedTechnicalDetails));
    QCOMPARE(reloaded.value().bytes,
             QByteArrayLiteral("updated-synthetic-vault-secret"));

    const auto removed = store.remove(saved.value());
    QVERIFY2(removed.ok(),
             qPrintable(removed.error().sanitizedTechnicalDetails));
    QVERIFY(removed.value());

    const auto missing = store.load(saved.value());
    QVERIFY(!missing.ok());
    QVERIFY(missing.error().code == smb::core::ErrorCode::CredentialNotFound);
  }

  void vaultFileDoesNotContainPlainTextSecrets() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto vaultPath = tempDir.filePath(QStringLiteral("vault.json"));
    smb::infrastructure::EncryptedVaultCredentialStore store(
        vaultPath, QByteArrayLiteral("synthetic-master-password"));

    const smb::core::CredentialSecret initial{
        QByteArrayLiteral("do-not-store-this-plain-text")};
    const auto saved = store.save(QStringLiteral("connection-1"), initial);
    QVERIFY2(saved.ok(), qPrintable(saved.error().sanitizedTechnicalDetails));

    QFile file(vaultPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    QVERIFY(!bytes.contains("do-not-store-this-plain-text"));
    QVERIFY(!bytes.contains("synthetic-master-password"));
    QVERIFY(bytes.contains("ciphertext"));
  }

  void wrongMasterPasswordFails() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto vaultPath = tempDir.filePath(QStringLiteral("vault.json"));
    {
      smb::infrastructure::EncryptedVaultCredentialStore store(
          vaultPath, QByteArrayLiteral("correct-master-password"));
      const smb::core::CredentialSecret secret{
          QByteArrayLiteral("synthetic-vault-secret")};
      const auto saved = store.save(QStringLiteral("connection-1"), secret);
      QVERIFY2(saved.ok(), qPrintable(saved.error().sanitizedTechnicalDetails));
    }

    smb::infrastructure::EncryptedVaultCredentialStore wrongStore(
        vaultPath, QByteArrayLiteral("wrong-master-password"));
    const auto loaded = wrongStore.load(QStringLiteral("vault:missing"));
    QVERIFY(!loaded.ok());
    QVERIFY(loaded.error().code == smb::core::ErrorCode::PermissionDenied);
    QVERIFY(!loaded.error().sanitizedTechnicalDetails.contains(
        QStringLiteral("correct-master-password")));
    QVERIFY(!loaded.error().sanitizedTechnicalDetails.contains(
        QStringLiteral("wrong-master-password")));
  }
};

QTEST_MAIN(EncryptedVaultCredentialStoreTest)

#include "test_encrypted_vault_credential_store.moc"
