#include "application/SettingsService.h"
#include "storage/SettingsRepository.h"
#include "storage/SqliteStorage.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

class SettingsServiceTest final : public QObject {
  Q_OBJECT

private slots:
  void savesAndLoadsThroughRepository() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    smb::infrastructure::SettingsRepository repository(storage.database());
    smb::application::SettingsService service(repository);

    auto settings = smb::core::ApplicationSettings::defaults();
    settings.themeMode = smb::core::ThemeMode::Dark;
    settings.languageMode = smb::core::LanguageMode::Russian;
    settings.logLevel = QStringLiteral("debug");
    settings.closeToTray = false;
    settings.operationTimeoutMs = 45000;

    QVERIFY(service.saveSettings(settings).ok());
    const auto loaded = service.loadSettings();
    QVERIFY(loaded.ok());
    QVERIFY(loaded.value().themeMode == smb::core::ThemeMode::Dark);
    QVERIFY(loaded.value().languageMode == smb::core::LanguageMode::Russian);
    QCOMPARE(loaded.value().logLevel, QStringLiteral("debug"));
    QVERIFY(!loaded.value().closeToTray);
    QCOMPARE(loaded.value().operationTimeoutMs, 45000);
  }
};

QTEST_MAIN(SettingsServiceTest)

#include "test_settings_service.moc"
