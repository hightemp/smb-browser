#include "core/Settings.h"

#include <QtTest/QtTest>

class SettingsModelTest final : public QObject {
  Q_OBJECT

private slots:
  void defaultsMatchProductDecisions() {
    const auto settings = smb::core::ApplicationSettings::defaults();

    QVERIFY(settings.themeMode == smb::core::ThemeMode::System);
    QVERIFY(settings.languageMode == smb::core::LanguageMode::English);
    QVERIFY(settings.credentialStoreMode ==
            smb::core::CredentialStoreMode::SystemKeychainWithVaultFallback);
    QVERIFY(settings.closeToTray);
    QCOMPARE(settings.operationTimeoutMs, 30000);
    QCOMPARE(settings.cacheRetentionDays, 7);
    QCOMPARE(settings.cacheMaxSizeMb, 512);
  }

  void enumValuesHaveStableStorageNames() {
    QCOMPARE(smb::core::toString(smb::core::ThemeMode::Dark),
             QStringLiteral("dark"));
    QCOMPARE(smb::core::toString(smb::core::LanguageMode::Russian),
             QStringLiteral("russian"));
    QCOMPARE(
        smb::core::toString(smb::core::CredentialStoreMode::EncryptedVault),
        QStringLiteral("encrypted_vault"));
  }
};

QTEST_MAIN(SettingsModelTest)

#include "test_settings_model.moc"
