#pragma once

#include <QString>

namespace smb::core {

enum class ThemeMode {
  System,
  Light,
  Dark,
};

enum class LanguageMode {
  System,
  English,
  Russian,
};

enum class CredentialStoreMode {
  SystemKeychainWithVaultFallback,
  SystemKeychainOnly,
  EncryptedVault,
};

struct ApplicationSettings {
  ThemeMode themeMode = ThemeMode::System;
  LanguageMode languageMode = LanguageMode::English;
  CredentialStoreMode credentialStoreMode =
      CredentialStoreMode::SystemKeychainWithVaultFallback;
  QString logLevel = QStringLiteral("info");
  int operationTimeoutMs = 30000;
  int cacheRetentionDays = 7;
  int cacheMaxSizeMb = 512;

  static ApplicationSettings defaults();
};

QString toString(ThemeMode mode);
QString toString(LanguageMode mode);
QString toString(CredentialStoreMode mode);

} // namespace smb::core
