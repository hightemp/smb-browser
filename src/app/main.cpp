#include "application/ConnectionService.h"
#include "application/ConnectionOpenService.h"
#include "application/ConnectionImportExportService.h"
#include "application/ConnectivityCheckService.h"
#include "application/OperationQueue.h"
#include "application/SettingsService.h"
#include "application/TempFileCache.h"
#include "core/SmbClient.h"
#include "credentials/QtKeychainCredentialStore.h"
#include "smb/DfsResolvingSmbClient.h"
#ifdef SMB_BROWSER_WITH_LIBSMB2
#include "smb/Libsmb2SmbClient.h"
#endif
#include "smb/SmbclientDfsReferralResolver.h"
#include "storage/ConnectionRepository.h"
#include "storage/ConnectionGroupRepository.h"
#include "storage/SettingsRepository.h"
#include "storage/SqliteStorage.h"
#include "ui/ConnectionManagementController.h"
#include "ui/ConnectionOpenController.h"
#include "ui/ConnectionClipboardController.h"
#include "ui/ConnectionsPanel.h"
#include "ui/ConnectivityCheckController.h"
#include "ui/DialogImportExportActionPrompter.h"
#include "ui/DialogRemoteFileActionPrompter.h"
#include "ui/MainWindow.h"
#include "ui/LocalizationManager.h"
#include "ui/MessageBoxConnectionActionPrompter.h"
#include "ui/RemoteBrowserWidget.h"
#include "ui/StatusPanel.h"
#include "ui/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStatusBar>

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

#ifndef SMB_BROWSER_WITH_LIBSMB2
class UnavailableSmbClient final : public smb::core::SmbClient {
public:
  smb::core::Result<bool>
  checkConnection(const smb::core::Connection &, const smb::core::CredentialSecret *,
                  const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::failure(unavailableError());
  }

  smb::core::Result<QVector<smb::core::RemoteFileEntry>>
  listDirectory(const smb::core::Connection &, const smb::core::CredentialSecret *,
                const QString &, const smb::core::OperationContext &) override {
    return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::failure(
        unavailableError());
  }

  smb::core::Result<bool>
  createDirectory(const smb::core::Connection &, const smb::core::CredentialSecret *,
                  const QString &, const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::failure(unavailableError());
  }

  smb::core::Result<bool>
  remove(const smb::core::Connection &, const smb::core::CredentialSecret *,
         const QString &, const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::failure(unavailableError());
  }

  smb::core::Result<bool>
  rename(const smb::core::Connection &, const smb::core::CredentialSecret *,
         const QString &, const QString &,
         const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::failure(unavailableError());
  }

  smb::core::Result<bool>
  downloadFile(const smb::core::Connection &, const smb::core::CredentialSecret *,
               const QString &, const QString &,
               const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::failure(unavailableError());
  }

  smb::core::Result<bool>
  uploadFile(const smb::core::Connection &, const smb::core::CredentialSecret *,
             const QString &, const QString &,
             const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::failure(unavailableError());
  }

  smb::core::Result<bool>
  copy(const smb::core::Connection &, const smb::core::CredentialSecret *,
       const QString &, const smb::core::Connection &,
       const smb::core::CredentialSecret *, const QString &,
       const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::failure(unavailableError());
  }

  smb::core::Result<bool>
  move(const smb::core::Connection &, const smb::core::CredentialSecret *,
       const QString &, const smb::core::Connection &,
       const smb::core::CredentialSecret *, const QString &,
       const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::failure(unavailableError());
  }

private:
  static smb::core::AppError unavailableError() {
    return smb::core::AppError::fromCode(
        smb::core::ErrorCode::ProtocolUnsupported,
        smb::core::ErrorCategory::Smb,
        QObject::tr("SMB backend is not enabled in this build."), false);
  }
};
#endif

QString statusForCheck(
    const smb::application::ConnectivityCheckResult &result) {
  if (result.available) {
    return QObject::tr("Connection: Available");
  }
  return QObject::tr("Connection: Unavailable");
}

} // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("SMB Browser"));
  QApplication::setOrganizationName(QStringLiteral("SMB Browser"));
  QApplication::setQuitOnLastWindowClosed(true);

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
  smb::infrastructure::ConnectionGroupRepository groupRepository(
      storage.database());
  smb::infrastructure::QtKeychainCredentialStore credentialStore;
  smb::application::ConnectionService connectionService(connectionRepository,
                                                       credentialStore);
  smb::application::ConnectionImportExportService importExportService(
      connectionRepository, groupRepository, credentialStore);
  smb::ui::DialogImportExportActionPrompter importExportPrompter(&window);
  window.attachImportExport(importExportService, importExportPrompter);
#ifdef SMB_BROWSER_WITH_LIBSMB2
  smb::infrastructure::Libsmb2SmbClient libsmb2Client(
      qMax(1, settings.operationTimeoutMs / 1000));
  smb::infrastructure::SmbclientDfsReferralResolver dfsReferralResolver(
      qMax(1, settings.operationTimeoutMs / 1000));
  smb::infrastructure::DfsResolvingSmbClient smbClient(libsmb2Client,
                                                       dfsReferralResolver);
#else
  UnavailableSmbClient smbClient;
#endif
  smb::application::ConnectivityCheckService connectivityCheckService(
      connectionRepository, credentialStore, smbClient);
  smb::application::ConnectionOpenService connectionOpenService(
      connectionRepository, credentialStore, smbClient);
  smb::application::OperationQueue operationQueue(2, &window);
  window.statusPanel()->bindOperationQueue(&operationQueue);

  smb::ui::MessageBoxConnectionActionPrompter connectionPrompter(&window);
  smb::ui::ConnectionManagementController connectionManagementController(
      *window.connectionsPanel(), connectionService, connectionPrompter);
  connectionManagementController.refreshConnections();
  QObject::connect(&window, &MainWindow::connectionsImported,
                   &connectionManagementController,
                   &smb::ui::ConnectionManagementController::refreshConnections);
  smb::ui::ConnectivityCheckController connectivityCheckController(
      *window.connectionsPanel(), connectivityCheckService, operationQueue,
      connectionPrompter);
  smb::ui::ConnectionOpenController connectionOpenController(
      *window.connectionsPanel(), connectionOpenService, operationQueue,
      connectionPrompter);
  smb::ui::ConnectionClipboardController connectionClipboardController(
      *window.connectionsPanel(), *QApplication::clipboard(),
      window.statusBar(), &window);

  smb::ui::DialogRemoteFileActionPrompter remoteFilePrompter;
  smb::ui::RemoteBrowserWidget remoteBrowser(
      connectionOpenService, connectionOpenService, connectionOpenService,
      operationQueue, remoteFilePrompter, &window);
  window.attachRemoteBrowser(remoteBrowser);

  QObject::connect(
      &connectivityCheckController,
      &smb::ui::ConnectivityCheckController::checkStarted, &window,
      [&window](const QString &, const QString &) {
        window.statusPanel()->setConnectionStatus(
            QObject::tr("Connection: Checking"));
        window.statusPanel()->clearLastError();
      });
  QObject::connect(
      &connectivityCheckController,
      &smb::ui::ConnectivityCheckController::checkCompleted, &window,
      [&window](const QString &,
                const smb::application::ConnectivityCheckResult &result) {
        window.statusPanel()->setConnectionStatus(statusForCheck(result));
        if (result.error.hasError()) {
          window.statusPanel()->setLastError(result.error);
        } else {
          window.statusPanel()->clearLastError();
        }
      });
  QObject::connect(&connectivityCheckController,
                   &smb::ui::ConnectivityCheckController::checkFailed, &window,
                   [&window](const QString &, const smb::core::AppError &error) {
                     window.statusPanel()->setConnectionStatus(
                         QObject::tr("Connection: Check failed"));
                     window.statusPanel()->setLastError(error);
                   });

  QObject::connect(
      &connectionOpenController,
      &smb::ui::ConnectionOpenController::openStarted, &window,
      [&window](const QString &, const QString &) {
        window.statusPanel()->setConnectionStatus(
            QObject::tr("Connection: Opening"));
        window.statusPanel()->clearLastError();
      });
  QObject::connect(
      &connectionOpenController,
      &smb::ui::ConnectionOpenController::connectionOpened, &window,
      [&remoteBrowser, &window](
          const smb::application::OpenConnectionResult &result) {
        remoteBrowser.setDirectory(result);
        const auto name = result.connection.name.isEmpty()
                              ? result.connection.normalizedUri
                              : result.connection.name;
        window.statusPanel()->setConnectionStatus(
            QObject::tr("Connection: %1").arg(name));
        window.statusPanel()->clearLastError();
      });
  QObject::connect(&connectionOpenController,
                   &smb::ui::ConnectionOpenController::connectionOpenFailed,
                   &window,
                   [&window](const QString &, const smb::core::AppError &error) {
                     window.statusPanel()->setConnectionStatus(
                         QObject::tr("Connection: Open failed"));
                     window.statusPanel()->setLastError(error);
                   });
  QObject::connect(
      &remoteBrowser, &smb::ui::RemoteBrowserWidget::directoryOpenFailed,
      &window, [&window](const QString &, const QString &,
                         const smb::core::AppError &error) {
        window.statusPanel()->setLastError(error);
      });
  QObject::connect(&remoteBrowser,
                   &smb::ui::RemoteBrowserWidget::remoteOperationFailed,
                   &window,
                   [&window](const QString &, const smb::core::AppError &error) {
                     window.statusPanel()->setLastError(error);
                   });

  window.show();

  return app.exec();
}
