#pragma once

#include "core/Connection.h"
#include "core/Error.h"

#include <QString>

namespace smb::ui {

class ConnectionActionPrompter {
public:
  virtual ~ConnectionActionPrompter() = default;

  virtual bool
  confirmDeleteConnection(const smb::core::Connection &connection) = 0;
  virtual void showError(const QString &title,
                         const smb::core::AppError &error) = 0;
};

} // namespace smb::ui
