#pragma once

#include "application/SettingsService.h"
#include "core/Settings.h"

#include <QDialog>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace smb::application {
class TempFileCache;
}

namespace smb::ui {

class LocalizationManager;
class ThemeManager;

class SettingsDialog final : public QDialog {
  Q_OBJECT

public:
  explicit SettingsDialog(
      smb::application::SettingsUseCase *settingsUseCase = nullptr,
      ThemeManager *themeManager = nullptr,
      LocalizationManager *localizationManager = nullptr,
      QWidget *parent = nullptr,
      smb::application::TempFileCache *tempFileCache = nullptr);

  bool loadSettings();
  void setSettings(const smb::core::ApplicationSettings &settings);
  smb::core::ApplicationSettings settings() const;

public slots:
  void accept() override;

private:
  void updateTrayControls();
  void clearCache();
  void setValidationMessage(const QString &message);
  smb::core::ThemeMode selectedThemeMode() const;
  smb::core::LanguageMode selectedLanguageMode() const;
  smb::core::CredentialStoreMode selectedCredentialStoreMode() const;
  void setComboByData(QComboBox *combo, int value);

  smb::application::SettingsUseCase *m_settingsUseCase = nullptr;
  ThemeManager *m_themeManager = nullptr;
  LocalizationManager *m_localizationManager = nullptr;
  smb::application::TempFileCache *m_tempFileCache = nullptr;
  std::unique_ptr<smb::application::TempFileCache> m_ownedTempFileCache;
  smb::core::ApplicationSettings m_settings =
      smb::core::ApplicationSettings::defaults();

  QComboBox *m_themeModeCombo = nullptr;
  QComboBox *m_languageModeCombo = nullptr;
  QCheckBox *m_closeToTrayCheckBox = nullptr;
  QCheckBox *m_trayNotificationsCheckBox = nullptr;
  QComboBox *m_credentialStoreModeCombo = nullptr;
  QComboBox *m_logLevelCombo = nullptr;
  QSpinBox *m_operationTimeoutSpinBox = nullptr;
  QSpinBox *m_cacheRetentionSpinBox = nullptr;
  QSpinBox *m_cacheMaxSizeSpinBox = nullptr;
  QPushButton *m_clearCacheButton = nullptr;
  QLabel *m_validationMessage = nullptr;
};

} // namespace smb::ui
