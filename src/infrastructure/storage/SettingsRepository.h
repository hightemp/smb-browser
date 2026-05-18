#pragma once

#include "core/Error.h"
#include "core/Settings.h"

#include <QSqlDatabase>

namespace smb::infrastructure {

class SettingsRepository {
public:
  explicit SettingsRepository(QSqlDatabase database);

  smb::core::Result<smb::core::ApplicationSettings> load() const;
  smb::core::Result<bool> save(const smb::core::ApplicationSettings &settings);

private:
  QSqlDatabase m_database;
};

} // namespace smb::infrastructure
