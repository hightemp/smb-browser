#include "ui/ConnectionsPanel.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest/QtTest>

namespace {

smb::core::Connection makeConnection(const QString &id, const QString &name,
                                     const QString &server,
                                     const QString &share,
                                     const QString &groupId, bool favorite) {
  auto connection = smb::core::Connection::createEmpty();
  connection.id = id;
  connection.name = name;
  connection.normalizedUri = QStringLiteral("smb://%1/%2").arg(server, share);
  connection.server = server;
  connection.share = share;
  connection.groupId = groupId;
  connection.isFavorite = favorite;
  return connection;
}

QVector<smb::core::Connection> sampleConnections() {
  return {
      makeConnection(QStringLiteral("conn-1"), QStringLiteral("Finance"),
                     QStringLiteral("fs01"), QStringLiteral("reports"),
                     QStringLiteral("Accounting"), true),
      makeConnection(QStringLiteral("conn-2"), QStringLiteral("Archive"),
                     QStringLiteral("nas02"), QStringLiteral("cold"),
                     QStringLiteral("Storage"), false),
  };
}

} // namespace

class ConnectionsPanelTest final : public QObject {
  Q_OBJECT

private slots:
  void displaysAndFiltersConnections() {
    smb::ui::ConnectionsPanel panel;
    panel.setConnections(sampleConnections());

    auto *list =
        panel.findChild<QListView *>(QStringLiteral("connectionsList"));
    QVERIFY(list != nullptr);
    QCOMPARE(list->model()->rowCount(), 2);

    auto *filter =
        panel.findChild<QLineEdit *>(QStringLiteral("connectionFilterEdit"));
    QVERIFY(filter != nullptr);
    filter->setText(QStringLiteral("fs01"));
    QCOMPARE(list->model()->rowCount(), 1);

    filter->clear();
    auto *favorites =
        panel.findChild<QCheckBox *>(QStringLiteral("favoriteConnectionsOnly"));
    QVERIFY(favorites != nullptr);
    favorites->setChecked(true);
    QCOMPARE(list->model()->rowCount(), 1);
  }

  void emitsActionsForSelectedConnection() {
    smb::ui::ConnectionsPanel panel;
    panel.setConnections(sampleConnections());

    auto *list =
        panel.findChild<QListView *>(QStringLiteral("connectionsList"));
    QVERIFY(list != nullptr);
    list->selectionModel()->select(list->model()->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect |
                                       QItemSelectionModel::Rows);

    QSignalSpy checkSpy(&panel, &smb::ui::ConnectionsPanel::checkRequested);
    QSignalSpy copySpy(&panel, &smb::ui::ConnectionsPanel::copyPathRequested);

    auto *checkButton = panel.findChild<QPushButton *>(
        QStringLiteral("panelCheckConnectionButton"));
    QVERIFY(checkButton != nullptr);
    checkButton->click();
    QCOMPARE(checkSpy.size(), 1);
    QCOMPARE(checkSpy.takeFirst().at(0).toString(), QStringLiteral("conn-1"));

    auto *copyButton =
        panel.findChild<QPushButton *>(QStringLiteral("panelCopyPathButton"));
    QVERIFY(copyButton != nullptr);
    copyButton->click();
    QCOMPARE(copySpy.size(), 1);
    QCOMPARE(copySpy.takeFirst().at(0).toString(),
             QStringLiteral("smb://fs01/reports"));
  }
};

QTEST_MAIN(ConnectionsPanelTest)

#include "test_connections_panel.moc"
