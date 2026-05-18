#include "core/Settings.h"

namespace smb::core {

QString toString(ThemeMode mode) {
  switch (mode) {
  case ThemeMode::System:
    return QStringLiteral("system");
  case ThemeMode::Light:
    return QStringLiteral("light");
  case ThemeMode::Dark:
    return QStringLiteral("dark");
  }

  return QStringLiteral("system");
}

QString toString(LanguageMode mode) {
  switch (mode) {
  case LanguageMode::System:
    return QStringLiteral("system");
  case LanguageMode::English:
    return QStringLiteral("english");
  case LanguageMode::Russian:
    return QStringLiteral("russian");
  }

  return QStringLiteral("english");
}

QString toString(CredentialStoreMode mode) {
  switch (mode) {
  case CredentialStoreMode::SystemKeychainWithVaultFallback:
    return QStringLiteral("system_keychain_with_vault_fallback");
  case CredentialStoreMode::SystemKeychainOnly:
    return QStringLiteral("system_keychain_only");
  case CredentialStoreMode::EncryptedVault:
    return QStringLiteral("encrypted_vault");
  }

  return QStringLiteral("system_keychain_with_vault_fallback");
}

ApplicationSettings ApplicationSettings::defaults() {
  ApplicationSettings settings;
  settings.themeMode = ThemeMode::System;
  settings.languageMode = LanguageMode::English;
  settings.credentialStoreMode =
      CredentialStoreMode::SystemKeychainWithVaultFallback;
  settings.closeToTray = true;
  settings.showTrayNotifications = true;
  settings.logLevel = QStringLiteral("info");
  settings.operationTimeoutMs = 30000;
  settings.cacheRetentionDays = 7;
  settings.cacheMaxSizeMb = 512;
  return settings;
}

} // namespace smb::core
