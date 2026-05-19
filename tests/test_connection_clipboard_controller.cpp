#include "ui/ConnectionClipboardController.h"

#include <QApplication>
#include <QClipboard>
#include <QListView>
#include <QPushButton>
#include <QSignalSpy>
#include <QStatusBar>
#include <QtTest/QtTest>

namespace {

smb::core::Connection sampleConnection() {
  auto connection = smb::core::Connection::createEmpty();
  connection.id = QStringLiteral("conn-1");
  connection.name = QStringLiteral("Finance");
  connection.normalizedUri = QStringLiteral("smb://server/share");
  connection.server = QStringLiteral("server");
  connection.share = QStringLiteral("share");
  return connection;
}

void selectFirstConnection(smb::ui::ConnectionsPanel &panel) {
  auto *list = panel.findChild<QListView *>(QStringLiteral("connectionsList"));
  QVERIFY(list != nullptr);
  list->selectionModel()->select(list->model()->index(0, 0),
                                 QItemSelectionModel::ClearAndSelect |
                                     QItemSelectionModel::Rows);
}

} // namespace

class ConnectionClipboardControllerTest final : public QObject {
  Q_OBJECT

private slots:
  void copyPathButtonWritesNormalizedUriToClipboard() {
    smb::ui::ConnectionsPanel panel;
    panel.setConnections({sampleConnection()});
    selectFirstConnection(panel);

    QStatusBar statusBar;
    auto *clipboard = QApplication::clipboard();
    QVERIFY(clipboard != nullptr);
    clipboard->clear();

    smb::ui::ConnectionClipboardController controller(panel, *clipboard,
                                                     &statusBar);
    QSignalSpy spy(&controller,
                   &smb::ui::ConnectionClipboardController::pathCopied);

    auto *copyButton =
        panel.findChild<QPushButton *>(QStringLiteral("panelCopyPathButton"));
    QVERIFY(copyButton != nullptr);
    copyButton->click();

    QCOMPARE(clipboard->text(), QStringLiteral("smb://server/share"));
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(),
             QStringLiteral("smb://server/share"));
    QVERIFY(statusBar.currentMessage().contains(
        QStringLiteral("smb://server/share")));
  }
};

QTEST_MAIN(ConnectionClipboardControllerTest)

#include "test_connection_clipboard_controller.moc"
