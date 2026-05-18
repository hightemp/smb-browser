#include "application/SettingsService.h"

namespace smb::application {

SettingsService::SettingsService(
    smb::infrastructure::SettingsRepository &repository)
    : m_repository(repository) {}

smb::core::Result<smb::core::ApplicationSettings>
SettingsService::loadSettings() const {
  return m_repository.load();
}

smb::core::Result<bool>
SettingsService::saveSettings(
    const smb::core::ApplicationSettings &settings) {
  return m_repository.save(settings);
}

} // namespace smb::application
