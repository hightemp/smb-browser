#include "storage/SettingsRepository.h"

#include <QDateTime>
#include <QMap>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <utility>

namespace smb::infrastructure {

namespace {

smb::core::AppError storageError(const QString &details) {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::StorageError,
                                       smb::core::ErrorCategory::Storage,
                                       details, false);
}

QString nowIsoUtc() {
  return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

smb::core::ThemeMode themeModeFromString(const QString &value) {
  if (value == QStringLiteral("light")) {
    return smb::core::ThemeMode::Light;
  }
  if (value == QStringLiteral("dark")) {
    return smb::core::ThemeMode::Dark;
  }
  return smb::core::ThemeMode::System;
}

smb::core::LanguageMode languageModeFromString(const QString &value) {
  if (value == QStringLiteral("system")) {
    return smb::core::LanguageMode::System;
  }
  if (value == QStringLiteral("russian")) {
    return smb::core::LanguageMode::Russian;
  }
  return smb::core::LanguageMode::English;
}

smb::core::CredentialStoreMode
credentialStoreModeFromString(const QString &value) {
  if (value == QStringLiteral("system_keychain_only")) {
    return smb::core::CredentialStoreMode::SystemKeychainOnly;
  }
  if (value == QStringLiteral("encrypted_vault")) {
    return smb::core::CredentialStoreMode::EncryptedVault;
  }
  return smb::core::CredentialStoreMode::SystemKeychainWithVaultFallback;
}

bool boolFromString(const QString &value, bool fallback) {
  if (value == QStringLiteral("true")) {
    return true;
  }
  if (value == QStringLiteral("false")) {
    return false;
  }
  return fallback;
}

int intFromString(const QString &value, int fallback) {
  bool ok = false;
  const auto parsed = value.toInt(&ok);
  return ok ? parsed : fallback;
}

smb::core::AppError upsertSetting(QSqlDatabase database, const QString &key,
                                  const QString &value) {
  QSqlQuery query(database);
  query.prepare(
      QStringLiteral("INSERT INTO settings(key, value, updated_at) "
                     "VALUES(:key, :value, :updated_at) "
                     "ON CONFLICT(key) DO UPDATE SET value = excluded.value, "
                     "updated_at = excluded.updated_at"));
  query.bindValue(QStringLiteral(":key"), key);
  query.bindValue(QStringLiteral(":value"), value);
  query.bindValue(QStringLiteral(":updated_at"), nowIsoUtc());
  if (!query.exec()) {
    return storageError(query.lastError().text());
  }

  return smb::core::AppError::none();
}

} // namespace

SettingsRepository::SettingsRepository(QSqlDatabase database)
    : m_database(std::move(database)) {}

smb::core::Result<smb::core::ApplicationSettings>
SettingsRepository::load() const {
  QSqlQuery query(m_database);
  if (!query.exec(QStringLiteral("SELECT key, value FROM settings"))) {
    return smb::core::Result<smb::core::ApplicationSettings>::failure(
        storageError(query.lastError().text()));
  }

  QMap<QString, QString> values;
  while (query.next()) {
    values.insert(query.value(0).toString(), query.value(1).toString());
  }

  auto settings = smb::core::ApplicationSettings::defaults();
  settings.themeMode = themeModeFromString(values.value(
      QStringLiteral("theme_mode"), smb::core::toString(settings.themeMode)));
  settings.languageMode = languageModeFromString(
      values.value(QStringLiteral("language_mode"),
                   smb::core::toString(settings.languageMode)));
  settings.credentialStoreMode = credentialStoreModeFromString(
      values.value(QStringLiteral("credential_store_mode"),
                   smb::core::toString(settings.credentialStoreMode)));
  settings.closeToTray = boolFromString(
      values.value(QStringLiteral("close_to_tray"),
                   settings.closeToTray ? QStringLiteral("true")
                                        : QStringLiteral("false")),
      settings.closeToTray);
  settings.showTrayNotifications = boolFromString(
      values.value(QStringLiteral("show_tray_notifications"),
                   settings.showTrayNotifications ? QStringLiteral("true")
                                                  : QStringLiteral("false")),
      settings.showTrayNotifications);
  settings.logLevel =
      values.value(QStringLiteral("log_level"), settings.logLevel);
  settings.operationTimeoutMs =
      intFromString(values.value(QStringLiteral("operation_timeout_ms"),
                                 QString::number(settings.operationTimeoutMs)),
                    settings.operationTimeoutMs);
  settings.cacheRetentionDays =
      intFromString(values.value(QStringLiteral("cache_retention_days"),
                                 QString::number(settings.cacheRetentionDays)),
                    settings.cacheRetentionDays);

  return smb::core::Result<smb::core::ApplicationSettings>::success(
      std::move(settings));
}

smb::core::Result<bool>
SettingsRepository::save(const smb::core::ApplicationSettings &settings) {
  if (!m_database.transaction()) {
    return smb::core::Result<bool>::failure(
        storageError(m_database.lastError().text()));
  }

  const QMap<QString, QString> values{
      {QStringLiteral("theme_mode"), smb::core::toString(settings.themeMode)},
      {QStringLiteral("language_mode"),
       smb::core::toString(settings.languageMode)},
      {QStringLiteral("credential_store_mode"),
       smb::core::toString(settings.credentialStoreMode)},
      {QStringLiteral("close_to_tray"),
       settings.closeToTray ? QStringLiteral("true") : QStringLiteral("false")},
      {QStringLiteral("show_tray_notifications"),
       settings.showTrayNotifications ? QStringLiteral("true")
                                      : QStringLiteral("false")},
      {QStringLiteral("log_level"), settings.logLevel},
      {QStringLiteral("operation_timeout_ms"),
       QString::number(settings.operationTimeoutMs)},
      {QStringLiteral("cache_retention_days"),
       QString::number(settings.cacheRetentionDays)},
  };

  for (auto it = values.cbegin(); it != values.cend(); ++it) {
    const auto error = upsertSetting(m_database, it.key(), it.value());
    if (error.hasError()) {
      m_database.rollback();
      return smb::core::Result<bool>::failure(error);
    }
  }

  if (!m_database.commit()) {
    return smb::core::Result<bool>::failure(
        storageError(m_database.lastError().text()));
  }

  return smb::core::Result<bool>::success(true);
}

} // namespace smb::infrastructure
