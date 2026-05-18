#include "ui/SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>
#include <QtTest/QtTest>

class FakeSettingsUseCase final : public smb::application::SettingsUseCase {
public:
  smb::core::ApplicationSettings loaded =
      smb::core::ApplicationSettings::defaults();
  smb::core::ApplicationSettings saved;
  bool saveCalled = false;
  bool failSave = false;

  smb::core::Result<smb::core::ApplicationSettings>
  loadSettings() const override {
    return smb::core::Result<smb::core::ApplicationSettings>::success(loaded);
  }

  smb::core::Result<bool>
  saveSettings(const smb::core::ApplicationSettings &settings) override {
    if (failSave) {
      return smb::core::Result<bool>::failure(
          smb::core::AppError::fromCode(smb::core::ErrorCode::StorageError,
                                        smb::core::ErrorCategory::Storage,
                                        QStringLiteral("save failed"), false));
    }

    saved = settings;
    saveCalled = true;
    return smb::core::Result<bool>::success(true);
  }
};

class SettingsDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void exposesExpectedControls() {
    smb::ui::SettingsDialog dialog;

    QVERIFY(dialog.findChild<QComboBox *>(QStringLiteral("themeModeCombo")) !=
            nullptr);
    QVERIFY(dialog.findChild<QComboBox *>(QStringLiteral("languageModeCombo")) !=
            nullptr);
    QVERIFY(dialog.findChild<QCheckBox *>(
                QStringLiteral("closeToTrayCheckBox")) != nullptr);
    QVERIFY(dialog.findChild<QCheckBox *>(
                QStringLiteral("trayNotificationsCheckBox")) != nullptr);
    QVERIFY(dialog.findChild<QComboBox *>(
                QStringLiteral("credentialStoreModeCombo")) != nullptr);
    QVERIFY(dialog.findChild<QComboBox *>(QStringLiteral("logLevelCombo")) !=
            nullptr);
    QVERIFY(dialog.findChild<QSpinBox *>(
                QStringLiteral("operationTimeoutSpinBox")) != nullptr);
    QVERIFY(dialog.findChild<QSpinBox *>(
                QStringLiteral("cacheRetentionSpinBox")) != nullptr);
  }

  void loadSettingsPopulatesControls() {
    FakeSettingsUseCase useCase;
    useCase.loaded.themeMode = smb::core::ThemeMode::Dark;
    useCase.loaded.languageMode = smb::core::LanguageMode::Russian;
    useCase.loaded.closeToTray = false;
    useCase.loaded.showTrayNotifications = true;
    useCase.loaded.logLevel = QStringLiteral("warning");
    useCase.loaded.operationTimeoutMs = 45000;
    useCase.loaded.cacheRetentionDays = 14;

    smb::ui::SettingsDialog dialog(&useCase);
    QVERIFY(dialog.loadSettings());

    const auto settings = dialog.settings();
    QVERIFY(settings.themeMode == smb::core::ThemeMode::Dark);
    QVERIFY(settings.languageMode == smb::core::LanguageMode::Russian);
    QVERIFY(!settings.closeToTray);
    QCOMPARE(settings.logLevel, QStringLiteral("warning"));
    QCOMPARE(settings.operationTimeoutMs, 45000);
    QCOMPARE(settings.cacheRetentionDays, 14);

    auto *trayNotifications = dialog.findChild<QCheckBox *>(
        QStringLiteral("trayNotificationsCheckBox"));
    QVERIFY(trayNotifications != nullptr);
    QVERIFY(!trayNotifications->isEnabled());
  }

  void acceptSavesUpdatedSettingsThroughUseCase() {
    FakeSettingsUseCase useCase;
    smb::ui::SettingsDialog dialog(&useCase);
    QVERIFY(dialog.loadSettings());

    auto *theme = dialog.findChild<QComboBox *>(QStringLiteral("themeModeCombo"));
    auto *language =
        dialog.findChild<QComboBox *>(QStringLiteral("languageModeCombo"));
    auto *timeout =
        dialog.findChild<QSpinBox *>(QStringLiteral("operationTimeoutSpinBox"));
    QVERIFY(theme != nullptr);
    QVERIFY(language != nullptr);
    QVERIFY(timeout != nullptr);

    theme->setCurrentText(QStringLiteral("Dark"));
    language->setCurrentText(QStringLiteral("Russian"));
    timeout->setValue(120000);

    dialog.accept();

    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(useCase.saveCalled);
    QVERIFY(useCase.saved.themeMode == smb::core::ThemeMode::Dark);
    QVERIFY(useCase.saved.languageMode == smb::core::LanguageMode::Russian);
    QCOMPARE(useCase.saved.operationTimeoutMs, 120000);
  }

  void saveFailureKeepsDialogOpenAndShowsError() {
    FakeSettingsUseCase useCase;
    useCase.failSave = true;

    smb::ui::SettingsDialog dialog(&useCase);
    QVERIFY(dialog.loadSettings());
    dialog.accept();

    QVERIFY(dialog.result() != int(QDialog::Accepted));
    QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("settingsValidationLabel"))
                 ->text()
                 .isEmpty());
  }
};

QTEST_MAIN(SettingsDialogTest)

#include "test_settings_dialog.moc"
