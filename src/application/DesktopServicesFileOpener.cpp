#include "application/DesktopServicesFileOpener.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

namespace smb::application {

smb::core::Result<bool>
DesktopServicesFileOpener::openLocalFile(const QString &localPath) {
  if (!QFileInfo::exists(localPath)) {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::FileNotFound, smb::core::ErrorCategory::Transfer,
        QStringLiteral("Local cached file was not found.")));
  }

  const auto opened = QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
  if (!opened) {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::LocalIoError, smb::core::ErrorCategory::Transfer,
        QStringLiteral("System file opener rejected the local file.")));
  }

  return smb::core::Result<bool>::success(true);
}

} // namespace smb::application
