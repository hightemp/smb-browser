#include "application/ConnectionService.h"
#include "application/ConnectionOpenService.h"
#include "application/ConnectionImportExportService.h"
#include "application/ConnectivityCheckService.h"
#include "application/OperationQueue.h"
#include "application/SettingsService.h"
#include "application/TempFileCache.h"
#include "core/SmbClient.h"
#include "credentials/QtKeychainCredentialStore.h"
#ifdef SMB_BROWSER_WITH_NATIVE_SMB
#include "smb/DfsResolvingSmbClient.h"
#include "smb/NativeDfsReferralResolver.h"
#include "smb/NativeSmbClient.h"
#elif defined(SMB_BROWSER_WITH_LIBSMB2)
#include "smb/DfsResolvingSmbClient.h"
#include "smb/Libsmb2SmbClient.h"
#include "smb/SmbclientDfsReferralResolver.h"
#endif
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
#include <QTextStream>
#include <QTimer>

#include <algorithm>

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

#if !defined(SMB_BROWSER_WITH_LIBSMB2) && !defined(SMB_BROWSER_WITH_NATIVE_SMB)
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

int smokeCloseDelayMs(const QStringList &arguments) {
  constexpr int kDisabled = -1;
  for (const auto &argument : arguments) {
    if (!argument.startsWith(QStringLiteral("--smoke-close-ms="))) {
      continue;
    }

    bool ok = false;
    const auto value =
        argument.mid(QStringLiteral("--smoke-close-ms=").size()).toInt(&ok);
    if (ok && value >= 0) {
      return value;
    }
    return 1000;
  }
  return kDisabled;
}

bool hasArgument(const QStringList &arguments, const QString &name) {
  return std::any_of(arguments.cbegin(), arguments.cend(),
                     [&name](const QString &argument) {
                       return argument == name;
                     });
}

QString smokeEnv(const char *name) {
  return QString::fromUtf8(qgetenv(name)).trimmed();
}

bool smokeFlagEnabled(const char *name) {
  const auto value = smokeEnv(name).toLower();
  return value == QStringLiteral("1") || value == QStringLiteral("true") ||
         value == QStringLiteral("yes") || value == QStringLiteral("on");
}

int smokeMaxEntries() {
  bool ok = false;
  const auto value = smokeEnv("SMB_BROWSER_SMOKE_MAX_ENTRIES").toInt(&ok);
  return ok && value > 0 ? value : 0;
}

void printSmokeError(QTextStream &stream, const smb::core::AppError &error) {
  stream << "error_code=" << smb::core::toString(error.code) << '\n'
         << "error_category=" << smb::core::toString(error.category) << '\n'
         << "error_message=" << error.userMessage << '\n'
         << "error_details=" << error.sanitizedTechnicalDetails << '\n';
}

void printSmokeEntries(const QVector<smb::core::RemoteFileEntry> &entries) {
  QTextStream output(stdout);
  output << "entries=" << entries.size() << '\n';
  if (!smokeFlagEnabled("SMB_BROWSER_SMOKE_PRINT_ENTRIES")) {
    return;
  }

  const auto maxEntries = smokeMaxEntries();
  int printed = 0;
  for (const auto &entry : entries) {
    if (maxEntries > 0 && printed >= maxEntries) {
      break;
    }
    output << "entry\t" << smb::core::toString(entry.type) << '\t'
           << entry.remotePath << '\t' << entry.name << '\n';
    ++printed;
  }
  if (maxEntries > 0 && entries.size() > maxEntries) {
    output << "entries_omitted=" << (entries.size() - maxEntries) << '\n';
  }
}

#ifdef SMB_BROWSER_WITH_NATIVE_SMB
struct SmbSmokeContext {
  smb::core::Connection connection = smb::core::Connection::createEmpty();
  smb::core::CredentialSecret secret;
  const smb::core::CredentialSecret *secretPtr = nullptr;
  int timeoutSeconds = 10;
};

bool loadSmbSmokeContext(SmbSmokeContext &context) {
  const auto server = smokeEnv("SMB_BROWSER_SMOKE_SERVER");
  const auto share = smokeEnv("SMB_BROWSER_SMOKE_SHARE");
  if (server.isEmpty() || share.isEmpty()) {
    return false;
  }

  context.connection.id = QStringLiteral("smoke");
  context.connection.name = QStringLiteral("Smoke");
  context.connection.server = server;
  context.connection.share = share;
  context.connection.normalizedUri =
      QStringLiteral("smb://%1/%2").arg(server, share);
  context.connection.domain = smokeEnv("SMB_BROWSER_SMOKE_DOMAIN");
  context.connection.username = smokeEnv("SMB_BROWSER_SMOKE_USER");

  const auto auth = smokeEnv("SMB_BROWSER_SMOKE_AUTH").toLower();
  if (auth == QStringLiteral("guest")) {
    context.connection.authType = smb::core::AuthType::Guest;
  } else if (auth == QStringLiteral("anonymous")) {
    context.connection.authType = smb::core::AuthType::Anonymous;
  } else {
    const auto password = qgetenv("SMB_BROWSER_SMOKE_PASSWORD");
    if (password.isEmpty()) {
      return false;
    }
    context.connection.authType = smb::core::AuthType::Password;
    context.secret.bytes = password;
    context.secretPtr = &context.secret;
  }

  bool ok = false;
  const auto timeoutSeconds =
      smokeEnv("SMB_BROWSER_SMOKE_TIMEOUT_SECONDS").toInt(&ok);
  context.timeoutSeconds = ok ? timeoutSeconds : 10;
  return true;
}
#endif

int runSmbListSmoke() {
#ifdef SMB_BROWSER_WITH_NATIVE_SMB
  SmbSmokeContext context;
  if (!loadSmbSmokeContext(context)) {
    return 2;
  }

  smb::infrastructure::NativeSmbClient nativeSmbClient(context.timeoutSeconds);
  smb::infrastructure::NativeDfsReferralResolver dfsReferralResolver(
      context.timeoutSeconds);
  smb::infrastructure::DfsResolvingSmbClient smbClient(nativeSmbClient,
                                                       dfsReferralResolver);
  const auto result = smbClient.listDirectory(
      context.connection, context.secretPtr, smokeEnv("SMB_BROWSER_SMOKE_PATH"),
      {});
  if (result.ok()) {
    printSmokeEntries(result.value());
    return 0;
  }

  QTextStream errorOutput(stderr);
  printSmokeError(errorOutput, result.error());

  const auto shareTargets =
      dfsReferralResolver.resolveTargets(context.connection, context.secretPtr,
                                         {});
  if (shareTargets.ok()) {
    errorOutput << "dfs_share_targets=" << shareTargets.value().size() << '\n';
    for (const auto &target : shareTargets.value()) {
      errorOutput << "dfs_share_target=smb://" << target.connection.server
                  << '/' << target.connection.share << '\n';
    }
  } else {
    const auto &dfsError = shareTargets.error();
    errorOutput << "dfs_share_error_code="
                << smb::core::toString(dfsError.code) << '\n'
                << "dfs_share_error_details="
                << dfsError.sanitizedTechnicalDetails << '\n';
  }

  const auto pathTargets = dfsReferralResolver.resolvePathTargets(
      context.connection, context.secretPtr, smokeEnv("SMB_BROWSER_SMOKE_PATH"),
      {});
  if (pathTargets.ok()) {
    errorOutput << "dfs_path_targets=" << pathTargets.value().size() << '\n';
    for (const auto &target : pathTargets.value()) {
      errorOutput << "dfs_path_target=smb://" << target.connection.server
                  << '/' << target.connection.share << " original_prefix="
                  << target.originalPathPrefix << " target_prefix="
                  << target.targetPathPrefix << '\n';
    }
  } else {
    const auto &dfsError = pathTargets.error();
    errorOutput << "dfs_path_error_code="
                << smb::core::toString(dfsError.code) << '\n'
                << "dfs_path_error_details="
                << dfsError.sanitizedTechnicalDetails << '\n';
  }
  return 1;
#else
  return 2;
#endif
}

int runSmbDownloadSmoke() {
#ifdef SMB_BROWSER_WITH_NATIVE_SMB
  SmbSmokeContext context;
  if (!loadSmbSmokeContext(context)) {
    return 2;
  }

  const auto remotePath = smokeEnv("SMB_BROWSER_SMOKE_PATH");
  const auto localPath = smokeEnv("SMB_BROWSER_SMOKE_LOCAL_PATH");
  if (remotePath.isEmpty() || localPath.isEmpty()) {
    return 2;
  }

  smb::infrastructure::NativeSmbClient nativeSmbClient(context.timeoutSeconds);
  smb::infrastructure::NativeDfsReferralResolver dfsReferralResolver(
      context.timeoutSeconds);
  smb::infrastructure::DfsResolvingSmbClient smbClient(nativeSmbClient,
                                                       dfsReferralResolver);
  const auto warmPath = smokeEnv("SMB_BROWSER_SMOKE_WARM_PATH");
  if (!warmPath.isEmpty()) {
    const auto warmResult = smbClient.listDirectory(
        context.connection, context.secretPtr, warmPath, {});
    if (!warmResult.ok()) {
      QTextStream errorOutput(stderr);
      errorOutput << "warmup_failed=1\n";
      printSmokeError(errorOutput, warmResult.error());
      return 1;
    }
    QTextStream(stdout) << "warmup_entries=" << warmResult.value().size()
                        << '\n';
  }

  smb::core::OperationContext operationContext;
  qint64 lastReportedBytes = -1;
  if (smokeFlagEnabled("SMB_BROWSER_SMOKE_PRINT_PROGRESS")) {
    operationContext.progressCallback =
        [&lastReportedBytes](const smb::core::TransferProgress &progress) {
          constexpr qint64 kProgressStep = 10 * 1024 * 1024;
          if (lastReportedBytes < 0 ||
              progress.bytesTransferred == progress.totalBytes ||
              progress.bytesTransferred - lastReportedBytes >= kProgressStep) {
            QTextStream(stdout)
                << "progress=" << progress.bytesTransferred << '/'
                << progress.totalBytes << '\n';
            lastReportedBytes = progress.bytesTransferred;
          }
        };
  }
  const auto result = smbClient.downloadFile(
      context.connection, context.secretPtr, remotePath, localPath,
      operationContext);
  if (result.ok()) {
    QTextStream(stdout) << "download=ok\n";
    return 0;
  }

  QTextStream errorOutput(stderr);
  printSmokeError(errorOutput, result.error());
  return 1;
#else
  return 2;
#endif
}

} // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("SMB Browser"));
  QApplication::setOrganizationName(QStringLiteral("SMB Browser"));
  QApplication::setQuitOnLastWindowClosed(true);
  if (hasArgument(app.arguments(), QStringLiteral("--smoke-smb-list"))) {
    return runSmbListSmoke();
  }
  if (hasArgument(app.arguments(), QStringLiteral("--smoke-smb-download"))) {
    return runSmbDownloadSmoke();
  }

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
#ifdef SMB_BROWSER_WITH_NATIVE_SMB
  smb::infrastructure::NativeSmbClient nativeSmbClient(
      qMax(1, settings.operationTimeoutMs / 1000));
  smb::infrastructure::NativeDfsReferralResolver dfsReferralResolver(
      qMax(1, settings.operationTimeoutMs / 1000));
  smb::infrastructure::DfsResolvingSmbClient smbClient(nativeSmbClient,
                                                       dfsReferralResolver);
#elif defined(SMB_BROWSER_WITH_LIBSMB2)
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
  const auto closeDelayMs = smokeCloseDelayMs(app.arguments());
  if (closeDelayMs >= 0) {
    QTimer::singleShot(closeDelayMs, &window, [&window]() { window.close(); });
  }

  return app.exec();
}
