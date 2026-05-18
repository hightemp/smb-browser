#include "storage/SettingsRepository.h"
#include "storage/SqliteStorage.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class SettingsRepositoryTest final : public QObject {
  Q_OBJECT

private slots:
  void loadReturnsDefaultsForEmptyDatabase() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    smb::infrastructure::SettingsRepository repository(storage.database());
    const auto loaded = repository.load();
    QVERIFY(loaded.ok());
    QVERIFY(loaded.value().themeMode == smb::core::ThemeMode::System);
    QVERIFY(loaded.value().languageMode == smb::core::LanguageMode::English);
    QVERIFY(loaded.value().credentialStoreMode ==
            smb::core::CredentialStoreMode::SystemKeychainWithVaultFallback);
  }

  void saveAndLoadRoundTrip() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    auto settings = smb::core::ApplicationSettings::defaults();
    settings.themeMode = smb::core::ThemeMode::Dark;
    settings.languageMode = smb::core::LanguageMode::Russian;
    settings.credentialStoreMode =
        smb::core::CredentialStoreMode::EncryptedVault;
    settings.closeToTray = false;
    settings.operationTimeoutMs = 12000;

    smb::infrastructure::SettingsRepository repository(storage.database());
    QVERIFY(repository.save(settings).ok());

    const auto loaded = repository.load();
    QVERIFY(loaded.ok());
    QVERIFY(loaded.value().themeMode == smb::core::ThemeMode::Dark);
    QVERIFY(loaded.value().languageMode == smb::core::LanguageMode::Russian);
    QVERIFY(loaded.value().credentialStoreMode ==
            smb::core::CredentialStoreMode::EncryptedVault);
    QVERIFY(!loaded.value().closeToTray);
    QCOMPARE(loaded.value().operationTimeoutMs, 12000);
  }

  void unknownEnumValuesFallBackToSafeDefaults() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    QSqlQuery query(storage.database());
    QVERIFY(query.exec(
        QStringLiteral("INSERT INTO settings(key, value, updated_at) "
                       "VALUES('theme_mode', 'broken', 'now')")));
    QVERIFY(query.exec(
        QStringLiteral("INSERT INTO settings(key, value, updated_at) "
                       "VALUES('language_mode', 'broken', 'now')")));
    QVERIFY(query.exec(
        QStringLiteral("INSERT INTO settings(key, value, updated_at) "
                       "VALUES('credential_store_mode', 'broken', 'now')")));

    smb::infrastructure::SettingsRepository repository(storage.database());
    const auto loaded = repository.load();
    QVERIFY(loaded.ok());
    QVERIFY(loaded.value().themeMode == smb::core::ThemeMode::System);
    QVERIFY(loaded.value().languageMode == smb::core::LanguageMode::English);
    QVERIFY(loaded.value().credentialStoreMode ==
            smb::core::CredentialStoreMode::SystemKeychainWithVaultFallback);
  }
};

QTEST_MAIN(SettingsRepositoryTest)

#include "test_settings_repository.moc"
