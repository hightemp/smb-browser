#include "core/RemoteFileEntry.h"

#include <QtTest/QtTest>

class RemoteFileEntryTest final : public QObject {
  Q_OBJECT

private slots:
  void directoryEntryReportsType() {
    smb::core::RemoteFileEntry entry;
    entry.name = QStringLiteral("Documents");
    entry.remotePath = QStringLiteral("/Documents");
    entry.type = smb::core::RemoteFileType::Directory;

    QVERIFY(entry.isDirectory());
    QVERIFY(!entry.isFile());
    QCOMPARE(smb::core::toString(entry.type), QStringLiteral("directory"));
  }

  void fileEntryStoresDisplayFields() {
    smb::core::RemoteFileEntry entry;
    entry.name = QStringLiteral("report.txt");
    entry.remotePath = QStringLiteral("/Documents/report.txt");
    entry.type = smb::core::RemoteFileType::File;
    entry.size = 42;
    entry.permissions = QStringLiteral("rw");
    entry.attributes = QStringLiteral("archive");

    QVERIFY(entry.isFile());
    QCOMPARE(entry.size, 42);
    QCOMPARE(entry.permissions, QStringLiteral("rw"));
    QCOMPARE(entry.attributes, QStringLiteral("archive"));
  }
};

QTEST_MAIN(RemoteFileEntryTest)

#include "test_remote_file_entry.moc"
