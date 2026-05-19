#include "application/DesktopServicesFileOpener.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <QUrl>

namespace smb::application {

smb::core::Result<bool>
DesktopServicesFileOpener::openLocalFile(const QString &localPath) {
  if (!QFileInfo::exists(localPath)) {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::FileNotFound, smb::core::ErrorCategory::Transfer,
        QStringLiteral("Local cached file was not found.")));
  }

  const auto url = QUrl::fromLocalFile(localPath);
  bool opened = false;
  auto *app = QCoreApplication::instance();
  if (app != nullptr && QThread::currentThread() != app->thread()) {
    QMetaObject::invokeMethod(
        app, [&opened, url]() { opened = QDesktopServices::openUrl(url); },
        Qt::BlockingQueuedConnection);
  } else {
    opened = QDesktopServices::openUrl(url);
  }
  if (!opened) {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::LocalIoError, smb::core::ErrorCategory::Transfer,
        QStringLiteral("System file opener rejected the local file.")));
  }

  return smb::core::Result<bool>::success(true);
}

} // namespace smb::application
