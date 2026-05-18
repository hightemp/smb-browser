#pragma once

#include "core/Error.h"
#include "core/Settings.h"
#include "storage/SettingsRepository.h"

namespace smb::application {

class SettingsUseCase {
public:
  virtual ~SettingsUseCase() = default;

  virtual smb::core::Result<smb::core::ApplicationSettings>
  loadSettings() const = 0;
  virtual smb::core::Result<bool>
  saveSettings(const smb::core::ApplicationSettings &settings) = 0;
};

class SettingsService final : public SettingsUseCase {
public:
  explicit SettingsService(smb::infrastructure::SettingsRepository &repository);

  smb::core::Result<smb::core::ApplicationSettings>
  loadSettings() const override;
  smb::core::Result<bool>
  saveSettings(const smb::core::ApplicationSettings &settings) override;

private:
  smb::infrastructure::SettingsRepository &m_repository;
};

} // namespace smb::application
