#include "application/SettingsService.h"
#include "application/TempFileCache.h"
#include "ui/ConnectionsPanel.h"
#include "ui/LocalizationManager.h"
#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/ThemeManager.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

class FakeSettingsUseCase final : public smb::application::SettingsUseCase {
public:
  smb::core::ApplicationSettings loaded =
      smb::core::ApplicationSettings::defaults();
  smb::core::ApplicationSettings saved;
  bool saveCalled = false;

  smb::core::Result<smb::core::ApplicationSettings>
  loadSettings() const override {
    return smb::core::Result<smb::core::ApplicationSettings>::success(loaded);
  }

  smb::core::Result<bool>
  saveSettings(const smb::core::ApplicationSettings &settings) override {
    saved = settings;
    saveCalled = true;
    return smb::core::Result<bool>::success(true);
  }
};

} // namespace

class MainWindowTest final : public QObject {
  Q_OBJECT

private slots:
  void createsExpectedShellWidgets() {
    MainWindow window;

    QVERIFY(window.findChild<QWidget *>(QStringLiteral("mainToolbar")) !=
            nullptr);
    QVERIFY(window.findChild<QLineEdit *>(QStringLiteral("globalSearchEdit")) !=
            nullptr);
    QVERIFY(window.findChild<QWidget *>(QStringLiteral("connectionsPanel")) !=
            nullptr);
    QVERIFY(window.findChild<QListView *>(QStringLiteral("connectionsList")) !=
            nullptr);
    QVERIFY(window.findChild<QWidget *>(QStringLiteral("browserArea")) !=
            nullptr);
    QVERIFY(window.findChild<QTableView *>(QStringLiteral("remoteFilesView")) !=
            nullptr);
    QVERIFY(window.findChild<QWidget *>(QStringLiteral("statusPanel")) !=
            nullptr);
    QVERIFY(window.findChild<QProgressBar *>(
                QStringLiteral("operationProgressBar")) != nullptr);
  }

  void exposesPrimaryActions() {
    MainWindow window;

    const QStringList requiredButtons = {
        QStringLiteral("addConnectionButton"),
        QStringLiteral("editConnectionButton"),
        QStringLiteral("deleteConnectionButton"),
        QStringLiteral("checkConnectionButton"),
        QStringLiteral("connectButton"),
        QStringLiteral("importButton"),
        QStringLiteral("exportButton"),
        QStringLiteral("logsButton"),
        QStringLiteral("settingsButton"),
        QStringLiteral("backButton"),
        QStringLiteral("forwardButton"),
        QStringLiteral("upButton"),
        QStringLiteral("refreshButton"),
        QStringLiteral("cancelOperationButton"),
    };

    for (const auto &buttonName : requiredButtons) {
      QVERIFY2(window.findChild<QPushButton *>(buttonName) != nullptr,
               qPrintable(buttonName));
    }
  }

  void topAddButtonForwardsToConnectionsPanel() {
    MainWindow window;
    QVERIFY(window.connectionsPanel() != nullptr);

    QSignalSpy spy(window.connectionsPanel(),
                   &smb::ui::ConnectionsPanel::addRequested);
    auto *button =
        window.findChild<QPushButton *>(QStringLiteral("addConnectionButton"));
    QVERIFY(button != nullptr);

    button->click();
    QCOMPARE(spy.count(), 1);
  }

  void topCheckAndConnectButtonsForwardSelectedConnection() {
    MainWindow window;
    QVERIFY(window.connectionsPanel() != nullptr);

    auto connection = smb::core::Connection::createEmpty();
    connection.id = QStringLiteral("conn-1");
    connection.name = QStringLiteral("Finance");
    connection.normalizedUri = QStringLiteral("smb://server/share");
    connection.server = QStringLiteral("server");
    connection.share = QStringLiteral("share");
    window.connectionsPanel()->setConnections({connection});

    auto *list =
        window.findChild<QListView *>(QStringLiteral("connectionsList"));
    QVERIFY(list != nullptr);
    list->selectionModel()->select(list->model()->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect |
                                       QItemSelectionModel::Rows);

    QSignalSpy checkSpy(window.connectionsPanel(),
                        &smb::ui::ConnectionsPanel::checkRequested);
    QSignalSpy connectSpy(window.connectionsPanel(),
                          &smb::ui::ConnectionsPanel::connectRequested);

    auto *checkButton =
        window.findChild<QPushButton *>(QStringLiteral("checkConnectionButton"));
    auto *connectButton =
        window.findChild<QPushButton *>(QStringLiteral("connectButton"));
    QVERIFY(checkButton != nullptr);
    QVERIFY(connectButton != nullptr);

    checkButton->click();
    connectButton->click();

    QCOMPARE(checkSpy.count(), 1);
    QCOMPARE(checkSpy.takeFirst().at(0).toString(), QStringLiteral("conn-1"));
    QCOMPARE(connectSpy.count(), 1);
    QCOMPARE(connectSpy.takeFirst().at(0).toString(), QStringLiteral("conn-1"));
  }

  void remoteBrowserCanReplacePlaceholderShell() {
    MainWindow window;
    auto *placeholder =
        window.findChild<QLabel *>(QStringLiteral("browserPlaceholder"));
    QVERIFY(placeholder != nullptr);

    QWidget browser;
    browser.setObjectName(QStringLiteral("attachedRemoteBrowser"));
    window.attachRemoteBrowser(browser);

    QCOMPARE(browser.parentWidget(),
             window.findChild<QWidget *>(QStringLiteral("browserArea")));
    QVERIFY(window.findChild<QWidget *>(
                QStringLiteral("attachedRemoteBrowser")) != nullptr);
  }

  void settingsButtonUsesAttachedDependencies() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    FakeSettingsUseCase useCase;
    smb::ui::ThemeManager themeManager;
    smb::ui::LocalizationManager localizationManager(QStringList{});
    smb::application::TempFileCache tempFileCache(
        tempDir.filePath(QStringLiteral("cache")));

    MainWindow window;
    window.attachSettings(useCase, themeManager, localizationManager,
                          tempFileCache);

    auto *button =
        window.findChild<QPushButton *>(QStringLiteral("settingsButton"));
    QVERIFY(button != nullptr);
    button->click();

    auto *dialog = window.findChild<smb::ui::SettingsDialog *>();
    QTRY_VERIFY(dialog != nullptr);
    auto *theme =
        dialog->findChild<QComboBox *>(QStringLiteral("themeModeCombo"));
    QVERIFY(theme != nullptr);
    theme->setCurrentText(QStringLiteral("Dark"));

    dialog->accept();

    QVERIFY(useCase.saveCalled);
    QVERIFY(useCase.saved.themeMode == smb::core::ThemeMode::Dark);
    QVERIFY(themeManager.themeMode() == smb::core::ThemeMode::Dark);
  }

  void languageChangeRetranslatesMainWindowAndConnectionsPanel() {
#ifndef SMB_BROWSER_TRANSLATION_DIR
    QSKIP("No compiled translation directory configured.");
#else
    const auto translationPath =
        QStringLiteral(SMB_BROWSER_TRANSLATION_DIR "/smb-browser_ru.qm");
    if (!QFileInfo::exists(translationPath)) {
      QSKIP("Compiled Russian translation is not available.");
    }

    auto *application = QCoreApplication::instance();
    QVERIFY(application != nullptr);

    MainWindow window;
    smb::ui::LocalizationManager localizationManager(
        QStringList{QStringLiteral(SMB_BROWSER_TRANSLATION_DIR)});
    localizationManager.setLanguageMode(smb::core::LanguageMode::Russian);
    const auto applied = localizationManager.apply(*application);
    QVERIFY(applied.translatorInstalled);

    QEvent languageChange(QEvent::LanguageChange);
    QCoreApplication::sendEvent(&window, &languageChange);

    QCOMPARE(window.findChild<QPushButton *>(
                         QStringLiteral("addConnectionButton"))
                 ->text(),
             QStringLiteral("Добавить"));
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("connectionsTitle"))
                 ->text(),
             QStringLiteral("Подключения"));

    localizationManager.setLanguageMode(smb::core::LanguageMode::English);
    localizationManager.apply(*application);
    QCoreApplication::sendEvent(&window, &languageChange);

    QCOMPARE(window.findChild<QPushButton *>(
                         QStringLiteral("addConnectionButton"))
                 ->text(),
             QStringLiteral("Add"));
#endif
  }
};

QTEST_MAIN(MainWindowTest)

#include "test_main_window.moc"
