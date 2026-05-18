#pragma once

#include "application/LocalFileOpener.h"

namespace smb::application {

class DesktopServicesFileOpener final : public LocalFileOpener {
public:
  smb::core::Result<bool> openLocalFile(const QString &localPath) override;
};

} // namespace smb::application
