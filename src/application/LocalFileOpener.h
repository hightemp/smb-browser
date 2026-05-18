#pragma once

#include "core/Error.h"

#include <QString>

namespace smb::application {

class LocalFileOpener {
public:
  virtual ~LocalFileOpener() = default;

  virtual smb::core::Result<bool> openLocalFile(const QString &localPath) = 0;
};

} // namespace smb::application
