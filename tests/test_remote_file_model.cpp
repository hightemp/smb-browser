#include "ui/RemoteFileModel.h"

#include <QIcon>
#include <QSignalSpy>
#include <QTableView>
#include <QtTest/QtTest>

namespace {

smb::core::RemoteFileEntry directoryEntry() {
  smb::core::RemoteFileEntry entry;
  entry.name = QStringLiteral("Documents");
  entry.remotePath = QStringLiteral("/Documents");
  entry.type = smb::core::RemoteFileType::Directory;
  entry.modifiedAt = QDateTime(QDate(2026, 5, 18), QTime(9, 15), Qt::UTC);
  entry.permissions = QStringLiteral("rwx");
  return entry;
}

smb::core::RemoteFileEntry fileEntry() {
  smb::core::RemoteFileEntry entry;
  entry.name = QStringLiteral("report.txt");
  entry.remotePath = QStringLiteral("/Documents/report.txt");
  entry.type = smb::core::RemoteFileType::File;
  entry.size = 42;
  entry.modifiedAt = QDateTime(QDate(2026, 5, 18), QTime(10, 30), Qt::UTC);
  entry.permissions = QStringLiteral("rw");
  entry.attributes = QStringLiteral("archive");
  return entry;
}

} // namespace

class RemoteFileModelTest final : public QObject {
  Q_OBJECT

private slots:
  void exposesRowsColumnsAndRolesForEntries() {
    smb::ui::RemoteFileModel model;
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    model.setEntries({directoryEntry(), fileEntry()},
                     QStringLiteral("/Documents"));

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.columnCount(), int(smb::ui::RemoteFileModel::ColumnCount));
    QCOMPARE(model.currentRemotePath(), QStringLiteral("/Documents"));

    const auto folderName =
        model.index(0, smb::ui::RemoteFileModel::NameColumn);
    QCOMPARE(model.data(folderName, Qt::DisplayRole).toString(),
             QStringLiteral("Documents"));
    QVERIFY(
        model.data(folderName, Qt::DecorationRole).value<QIcon>().isNull() ==
        false);
    QCOMPARE(model.data(folderName, smb::ui::RemoteFileModel::RemotePathRole)
                 .toString(),
             QStringLiteral("/Documents"));
    QCOMPARE(
        model.data(folderName, smb::ui::RemoteFileModel::TypeRole).toString(),
        QStringLiteral("directory"));

    const auto folderSize =
        model.index(0, smb::ui::RemoteFileModel::SizeColumn);
    QVERIFY(!model.data(folderSize, Qt::DisplayRole).isValid());

    const auto fileSize = model.index(1, smb::ui::RemoteFileModel::SizeColumn);
    QCOMPARE(model.data(fileSize, Qt::DisplayRole).toLongLong(), qint64(42));
    QCOMPARE(
        model.data(fileSize, smb::ui::RemoteFileModel::SizeRole).toLongLong(),
        qint64(42));

    QCOMPARE(model
                 .headerData(smb::ui::RemoteFileModel::NameColumn,
                             Qt::Horizontal, Qt::DisplayRole)
                 .toString(),
             QStringLiteral("Name"));
  }

  void entryAtAndClearHandleBounds() {
    smb::ui::RemoteFileModel model;
    model.setEntries({fileEntry()});

    QCOMPARE(model.entryAt(0).name, QStringLiteral("report.txt"));
    QVERIFY(model.entryAt(-1).name.isEmpty());
    QVERIFY(model.entryAt(99).name.isEmpty());

    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.currentRemotePath().isEmpty());
  }

  void tableViewCanDisplayRemoteEntries() {
    smb::ui::RemoteFileModel model;
    model.setEntries({directoryEntry(), fileEntry()},
                     QStringLiteral("/Documents"));

    QTableView view;
    view.setModel(&model);

    QCOMPARE(view.model()->rowCount(), 2);
    QCOMPARE(view.model()->columnCount(),
             int(smb::ui::RemoteFileModel::ColumnCount));
    QCOMPARE(
        view.model()
            ->data(view.model()->index(1, smb::ui::RemoteFileModel::NameColumn))
            .toString(),
        QStringLiteral("report.txt"));
  }

  void filterProxySearchesLoadedEntriesOnly() {
    smb::ui::RemoteFileModel model;
    model.setEntries({directoryEntry(), fileEntry()},
                     QStringLiteral("/Documents"));

    smb::ui::RemoteFileFilterProxyModel proxy;
    proxy.setSourceModel(&model);

    QCOMPARE(proxy.rowCount(), 2);

    proxy.setFilterText(QStringLiteral("report"));
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.data(proxy.index(0, smb::ui::RemoteFileModel::NameColumn))
                 .toString(),
             QStringLiteral("report.txt"));

    proxy.setFilterText(QStringLiteral("archive"));
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.data(proxy.index(0, smb::ui::RemoteFileModel::NameColumn))
                 .toString(),
             QStringLiteral("report.txt"));

    proxy.setFilterText(QStringLiteral("missing"));
    QCOMPARE(proxy.rowCount(), 0);
  }
};

QTEST_MAIN(RemoteFileModelTest)

#include "test_remote_file_model.moc"
