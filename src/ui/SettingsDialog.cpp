#include "ui/SettingsDialog.h"

#include "application/TempFileCache.h"
#include "ui/LocalizationManager.h"
#include "ui/ThemeManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace smb::ui {

namespace {

int value(smb::core::ThemeMode mode) { return static_cast<int>(mode); }
int value(smb::core::LanguageMode mode) { return static_cast<int>(mode); }
int value(smb::core::CredentialStoreMode mode) {
  return static_cast<int>(mode);
}

smb::core::ThemeMode themeModeFromValue(int raw) {
  switch (static_cast<smb::core::ThemeMode>(raw)) {
  case smb::core::ThemeMode::Light:
    return smb::core::ThemeMode::Light;
  case smb::core::ThemeMode::Dark:
    return smb::core::ThemeMode::Dark;
  case smb::core::ThemeMode::System:
  default:
    return smb::core::ThemeMode::System;
  }
}

smb::core::LanguageMode languageModeFromValue(int raw) {
  switch (static_cast<smb::core::LanguageMode>(raw)) {
  case smb::core::LanguageMode::System:
    return smb::core::LanguageMode::System;
  case smb::core::LanguageMode::Russian:
    return smb::core::LanguageMode::Russian;
  case smb::core::LanguageMode::English:
  default:
    return smb::core::LanguageMode::English;
  }
}

smb::core::CredentialStoreMode credentialStoreModeFromValue(int raw) {
  switch (static_cast<smb::core::CredentialStoreMode>(raw)) {
  case smb::core::CredentialStoreMode::SystemKeychainOnly:
    return smb::core::CredentialStoreMode::SystemKeychainOnly;
  case smb::core::CredentialStoreMode::EncryptedVault:
    return smb::core::CredentialStoreMode::EncryptedVault;
  case smb::core::CredentialStoreMode::SystemKeychainWithVaultFallback:
  default:
    return smb::core::CredentialStoreMode::SystemKeychainWithVaultFallback;
  }
}

} // namespace

SettingsDialog::SettingsDialog(
    smb::application::SettingsUseCase *settingsUseCase,
    ThemeManager *themeManager, LocalizationManager *localizationManager,
    QWidget *parent, smb::application::TempFileCache *tempFileCache)
    : QDialog(parent), m_settingsUseCase(settingsUseCase),
      m_themeManager(themeManager), m_localizationManager(localizationManager) {
  if (tempFileCache != nullptr) {
    m_tempFileCache = tempFileCache;
  } else {
    m_ownedTempFileCache = std::make_unique<smb::application::TempFileCache>();
    m_tempFileCache = m_ownedTempFileCache.get();
  }

  setObjectName(QStringLiteral("settingsDialog"));
  setWindowTitle(tr("Settings"));
  setModal(true);

  auto *layout = new QVBoxLayout(this);

  auto *appearanceGroup = new QGroupBox(tr("Appearance"), this);
  appearanceGroup->setObjectName(QStringLiteral("appearanceSettingsGroup"));
  auto *appearanceForm = new QFormLayout(appearanceGroup);

  m_themeModeCombo = new QComboBox(appearanceGroup);
  m_themeModeCombo->setObjectName(QStringLiteral("themeModeCombo"));
  m_themeModeCombo->addItem(tr("System"), value(smb::core::ThemeMode::System));
  m_themeModeCombo->addItem(tr("Light"), value(smb::core::ThemeMode::Light));
  m_themeModeCombo->addItem(tr("Dark"), value(smb::core::ThemeMode::Dark));
  appearanceForm->addRow(tr("Theme"), m_themeModeCombo);

  m_languageModeCombo = new QComboBox(appearanceGroup);
  m_languageModeCombo->setObjectName(QStringLiteral("languageModeCombo"));
  m_languageModeCombo->addItem(tr("System"),
                               value(smb::core::LanguageMode::System));
  m_languageModeCombo->addItem(tr("English"),
                               value(smb::core::LanguageMode::English));
  m_languageModeCombo->addItem(tr("Russian"),
                               value(smb::core::LanguageMode::Russian));
  appearanceForm->addRow(tr("Language"), m_languageModeCombo);
  layout->addWidget(appearanceGroup);

  auto *behaviorGroup = new QGroupBox(tr("Behavior"), this);
  behaviorGroup->setObjectName(QStringLiteral("behaviorSettingsGroup"));
  auto *behaviorForm = new QFormLayout(behaviorGroup);
  m_closeToTrayCheckBox = new QCheckBox(tr("Close window to tray"), behaviorGroup);
  m_closeToTrayCheckBox->setObjectName(QStringLiteral("closeToTrayCheckBox"));
  behaviorForm->addRow(QString(), m_closeToTrayCheckBox);
  m_trayNotificationsCheckBox =
      new QCheckBox(tr("Show tray notifications"), behaviorGroup);
  m_trayNotificationsCheckBox->setObjectName(
      QStringLiteral("trayNotificationsCheckBox"));
  behaviorForm->addRow(QString(), m_trayNotificationsCheckBox);
  layout->addWidget(behaviorGroup);

  auto *securityGroup = new QGroupBox(tr("Security"), this);
  securityGroup->setObjectName(QStringLiteral("securitySettingsGroup"));
  auto *securityForm = new QFormLayout(securityGroup);
  m_credentialStoreModeCombo = new QComboBox(securityGroup);
  m_credentialStoreModeCombo->setObjectName(
      QStringLiteral("credentialStoreModeCombo"));
  m_credentialStoreModeCombo->addItem(
      tr("System keychain with encrypted vault fallback"),
      value(smb::core::CredentialStoreMode::SystemKeychainWithVaultFallback));
  m_credentialStoreModeCombo->addItem(
      tr("System keychain only"),
      value(smb::core::CredentialStoreMode::SystemKeychainOnly));
  m_credentialStoreModeCombo->addItem(
      tr("Encrypted local vault"),
      value(smb::core::CredentialStoreMode::EncryptedVault));
  securityForm->addRow(tr("Credential store"), m_credentialStoreModeCombo);
  layout->addWidget(securityGroup);

  auto *operationsGroup = new QGroupBox(tr("Operations"), this);
  operationsGroup->setObjectName(QStringLiteral("operationSettingsGroup"));
  auto *operationsForm = new QFormLayout(operationsGroup);

  m_logLevelCombo = new QComboBox(operationsGroup);
  m_logLevelCombo->setObjectName(QStringLiteral("logLevelCombo"));
  m_logLevelCombo->addItem(tr("Debug"), QStringLiteral("debug"));
  m_logLevelCombo->addItem(tr("Info"), QStringLiteral("info"));
  m_logLevelCombo->addItem(tr("Warning"), QStringLiteral("warning"));
  m_logLevelCombo->addItem(tr("Error"), QStringLiteral("error"));
  operationsForm->addRow(tr("Log level"), m_logLevelCombo);

  m_operationTimeoutSpinBox = new QSpinBox(operationsGroup);
  m_operationTimeoutSpinBox->setObjectName(
      QStringLiteral("operationTimeoutSpinBox"));
  m_operationTimeoutSpinBox->setRange(1000, 600000);
  m_operationTimeoutSpinBox->setSingleStep(1000);
  m_operationTimeoutSpinBox->setSuffix(tr(" ms"));
  operationsForm->addRow(tr("Operation timeout"), m_operationTimeoutSpinBox);

  m_cacheRetentionSpinBox = new QSpinBox(operationsGroup);
  m_cacheRetentionSpinBox->setObjectName(QStringLiteral("cacheRetentionSpinBox"));
  m_cacheRetentionSpinBox->setRange(0, 365);
  m_cacheRetentionSpinBox->setSuffix(tr(" days"));
  operationsForm->addRow(tr("Cache retention"), m_cacheRetentionSpinBox);

  m_cacheMaxSizeSpinBox = new QSpinBox(operationsGroup);
  m_cacheMaxSizeSpinBox->setObjectName(QStringLiteral("cacheMaxSizeSpinBox"));
  m_cacheMaxSizeSpinBox->setRange(0, 1024 * 1024);
  m_cacheMaxSizeSpinBox->setSpecialValueText(tr("No size limit"));
  m_cacheMaxSizeSpinBox->setSuffix(tr(" MB"));
  operationsForm->addRow(tr("Cache max size"), m_cacheMaxSizeSpinBox);

  m_clearCacheButton = new QPushButton(tr("Clear cache"), operationsGroup);
  m_clearCacheButton->setObjectName(QStringLiteral("clearCacheButton"));
  operationsForm->addRow(QString(), m_clearCacheButton);
  layout->addWidget(operationsGroup);

  m_validationMessage = new QLabel(this);
  m_validationMessage->setObjectName(QStringLiteral("settingsValidationLabel"));
  m_validationMessage->setWordWrap(true);
  layout->addWidget(m_validationMessage);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
  buttons->setObjectName(QStringLiteral("settingsDialogButtons"));
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
  connect(m_closeToTrayCheckBox, &QCheckBox::toggled, this,
          &SettingsDialog::updateTrayControls);
  connect(m_clearCacheButton, &QPushButton::clicked, this,
          &SettingsDialog::clearCache);

  setSettings(m_settings);
}

bool SettingsDialog::loadSettings() {
  if (m_settingsUseCase == nullptr) {
    setSettings(smb::core::ApplicationSettings::defaults());
    return true;
  }

  const auto loaded = m_settingsUseCase->loadSettings();
  if (!loaded.ok()) {
    setValidationMessage(loaded.error().userMessage);
    return false;
  }

  setSettings(loaded.value());
  return true;
}

void SettingsDialog::setSettings(
    const smb::core::ApplicationSettings &settings) {
  m_settings = settings;
  setComboByData(m_themeModeCombo, value(settings.themeMode));
  setComboByData(m_languageModeCombo, value(settings.languageMode));
  setComboByData(m_credentialStoreModeCombo,
                 value(settings.credentialStoreMode));
  m_closeToTrayCheckBox->setChecked(settings.closeToTray);
  m_trayNotificationsCheckBox->setChecked(settings.showTrayNotifications);
  const auto logLevelIndex = m_logLevelCombo->findData(settings.logLevel);
  m_logLevelCombo->setCurrentIndex(logLevelIndex < 0 ? 1 : logLevelIndex);
  m_operationTimeoutSpinBox->setValue(settings.operationTimeoutMs);
  m_cacheRetentionSpinBox->setValue(settings.cacheRetentionDays);
  m_cacheMaxSizeSpinBox->setValue(settings.cacheMaxSizeMb);
  setValidationMessage({});
  updateTrayControls();
}

smb::core::ApplicationSettings SettingsDialog::settings() const {
  auto next = m_settings;
  next.themeMode = selectedThemeMode();
  next.languageMode = selectedLanguageMode();
  next.credentialStoreMode = selectedCredentialStoreMode();
  next.closeToTray = m_closeToTrayCheckBox->isChecked();
  next.showTrayNotifications = m_trayNotificationsCheckBox->isChecked();
  next.logLevel = m_logLevelCombo->currentData().toString();
  next.operationTimeoutMs = m_operationTimeoutSpinBox->value();
  next.cacheRetentionDays = m_cacheRetentionSpinBox->value();
  next.cacheMaxSizeMb = m_cacheMaxSizeSpinBox->value();
  return next;
}

void SettingsDialog::accept() {
  setValidationMessage({});

  const auto next = settings();
  if (next.logLevel.isEmpty()) {
    setValidationMessage(tr("Log level is required."));
    return;
  }

  if (m_settingsUseCase != nullptr) {
    const auto saved = m_settingsUseCase->saveSettings(next);
    if (!saved.ok()) {
      setValidationMessage(saved.error().userMessage);
      return;
    }
  }

  m_settings = next;

  if (m_themeManager != nullptr) {
    auto *application =
        qobject_cast<QApplication *>(QCoreApplication::instance());
    if (application != nullptr) {
      m_themeManager->setThemeMode(next.themeMode);
      m_themeManager->apply(*application);
    }
  }
  if (m_localizationManager != nullptr && QCoreApplication::instance() != nullptr) {
    m_localizationManager->setLanguageMode(next.languageMode);
    m_localizationManager->apply(*QCoreApplication::instance());
  }

  QDialog::accept();
}

void SettingsDialog::updateTrayControls() {
  m_trayNotificationsCheckBox->setEnabled(m_closeToTrayCheckBox->isChecked());
}

void SettingsDialog::clearCache() {
  setValidationMessage({});
  if (m_tempFileCache == nullptr) {
    setValidationMessage(tr("Cache is unavailable."));
    return;
  }

  const auto cleared = m_tempFileCache->clearAll();
  if (!cleared.ok()) {
    setValidationMessage(cleared.error().userMessage);
    return;
  }

  setValidationMessage(tr("Cache cleared."));
}

void SettingsDialog::setValidationMessage(const QString &message) {
  m_validationMessage->setText(message);
  m_validationMessage->setVisible(!message.isEmpty());
}

smb::core::ThemeMode SettingsDialog::selectedThemeMode() const {
  return themeModeFromValue(m_themeModeCombo->currentData().toInt());
}

smb::core::LanguageMode SettingsDialog::selectedLanguageMode() const {
  return languageModeFromValue(m_languageModeCombo->currentData().toInt());
}

smb::core::CredentialStoreMode
SettingsDialog::selectedCredentialStoreMode() const {
  return credentialStoreModeFromValue(
      m_credentialStoreModeCombo->currentData().toInt());
}

void SettingsDialog::setComboByData(QComboBox *combo, int rawValue) {
  const auto index = combo->findData(rawValue);
  combo->setCurrentIndex(index < 0 ? 0 : index);
}

} // namespace smb::ui
