#include "ui/MainWindow.h"

#include <QLineEdit>
#include <QListView>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QtTest/QtTest>

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
};

QTEST_MAIN(MainWindowTest)

#include "test_main_window.moc"
