#include "application/ConnectionService.h"
#include "application/SettingsService.h"
#include "application/TempFileCache.h"
#include "credentials/QtKeychainCredentialStore.h"
#include "storage/ConnectionRepository.h"
#include "storage/SettingsRepository.h"
#include "storage/SqliteStorage.h"
#include "ui/ConnectionManagementController.h"
#include "ui/MainWindow.h"
#include "ui/LocalizationManager.h"
#include "ui/MessageBoxConnectionActionPrompter.h"
#include "ui/ThemeManager.h"
#include "ui/TrayController.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

namespace {

QString writableAppDataPath() {
  auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (path.isEmpty()) {
    path = QDir::home().filePath(QStringLiteral(".smb-browser"));
  }
  return path;
}

bool showStartupErrorIfNeeded(const smb::core::AppError &error,
                              const QString &title) {
  if (!error.hasError()) {
    return false;
  }

  QMessageBox::critical(nullptr, title,
                        error.userMessage.isEmpty()
                            ? error.sanitizedTechnicalDetails
                            : error.userMessage);
  return true;
}

} // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("SMB Browser"));
  QApplication::setOrganizationName(QStringLiteral("SMB Browser"));

  const auto appDataPath = writableAppDataPath();
  if (!QDir().mkpath(appDataPath)) {
    QMessageBox::critical(
        nullptr, QObject::tr("Unable to Start"),
        QObject::tr("Unable to create application data directory: %1")
            .arg(appDataPath));
    return 1;
  }

  smb::infrastructure::SqliteStorage storage;
  if (showStartupErrorIfNeeded(
          storage.open(QDir(appDataPath).filePath(QStringLiteral("app.db"))),
          QObject::tr("Unable to Open Database"))) {
    return 1;
  }
  if (showStartupErrorIfNeeded(storage.migrate(),
                               QObject::tr("Unable to Migrate Database"))) {
    return 1;
  }

  smb::infrastructure::SettingsRepository settingsRepository(
      storage.database());
  smb::application::SettingsService settingsService(settingsRepository);
  auto settings = smb::core::ApplicationSettings::defaults();
  const auto loadedSettings = settingsService.loadSettings();
  if (loadedSettings.ok()) {
    settings = loadedSettings.value();
  }

  smb::ui::LocalizationManager localizationManager;
  localizationManager.setLanguageMode(settings.languageMode);
  localizationManager.apply(app);

  smb::ui::ThemeManager themeManager;
  themeManager.setThemeMode(settings.themeMode);
  themeManager.apply(app);

  MainWindow window;
  smb::application::TempFileCache tempFileCache;
  window.attachSettings(settingsService, themeManager, localizationManager,
                        tempFileCache);

  smb::infrastructure::ConnectionRepository connectionRepository(
      storage.database());
  smb::infrastructure::QtKeychainCredentialStore credentialStore;
  smb::application::ConnectionService connectionService(connectionRepository,
                                                       credentialStore);
  smb::ui::MessageBoxConnectionActionPrompter connectionPrompter(&window);
  smb::ui::ConnectionManagementController connectionManagementController(
      *window.connectionsPanel(), connectionService, connectionPrompter);
  connectionManagementController.refreshConnections();

  smb::ui::TrayController trayController;
  trayController.setMainWindow(&window);
  trayController.showTrayIcon();
  window.show();

  return app.exec();
}
