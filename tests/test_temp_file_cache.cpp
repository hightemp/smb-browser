#include "application/TempFileCache.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class TempFileCacheTest final : public QObject {
  Q_OBJECT

private slots:
  void generatedPathIsStableAndDoesNotExposeRemotePath() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::application::TempFileCache cache(tempDir.path());

    const auto first = cache.localPathFor(
        QStringLiteral("connection-1"),
        QStringLiteral("/finance/passwords/server-share/report.txt"));
    const auto second = cache.localPathFor(
        QStringLiteral("connection-1"),
        QStringLiteral("/finance/passwords/server-share/report.txt"));

    QVERIFY(first.ok());
    QVERIFY(second.ok());
    QCOMPARE(first.value(), second.value());
    QVERIFY(first.value().startsWith(tempDir.path()));
    QVERIFY(first.value().endsWith(QStringLiteral(".txt")));
    QVERIFY(!first.value().contains(QStringLiteral("passwords")));
    QVERIFY(!first.value().contains(QStringLiteral("server-share")));
    QVERIFY(!first.value().contains(QStringLiteral("report")));
  }

  void differentRemotePathsUseDifferentCacheFiles() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::application::TempFileCache cache(tempDir.path());
    const auto first = cache.localPathFor(QStringLiteral("connection-1"),
                                          QStringLiteral("/docs/a.txt"));
    const auto second = cache.localPathFor(QStringLiteral("connection-1"),
                                           QStringLiteral("/docs/b.txt"));

    QVERIFY(first.ok());
    QVERIFY(second.ok());
    QVERIFY(first.value() != second.value());
  }

  void cleanupRemovesOnlyOldFiles() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::application::TempFileCache cache(tempDir.path());
    const auto oldPath = cache.localPathFor(QStringLiteral("connection-1"),
                                            QStringLiteral("/docs/old.txt"));
    const auto freshPath = cache.localPathFor(
        QStringLiteral("connection-1"), QStringLiteral("/docs/fresh.txt"));
    QVERIFY(oldPath.ok());
    QVERIFY(freshPath.ok());

    {
      QFile oldFile(oldPath.value());
      QVERIFY(oldFile.open(QIODevice::WriteOnly));
      oldFile.write("old");
      oldFile.close();
    }
    {
      QFile oldFile(oldPath.value());
      QVERIFY(oldFile.open(QIODevice::ReadWrite));
      QVERIFY(oldFile.setFileTime(QDateTime::currentDateTimeUtc().addDays(-10),
                                  QFileDevice::FileModificationTime));
      oldFile.close();
    }
    {
      QFile freshFile(freshPath.value());
      QVERIFY(freshFile.open(QIODevice::WriteOnly));
      freshFile.write("fresh");
    }

    const auto removed =
        cache.removeOlderThan(QDateTime::currentDateTimeUtc().addDays(-1));
    QVERIFY(removed.ok());
    QCOMPARE(removed.value(), 1);
    QVERIFY(!QFileInfo::exists(oldPath.value()));
    QVERIFY(QFileInfo::exists(freshPath.value()));
  }

  void clearAllRemovesCacheRoot() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::application::TempFileCache cache(
        tempDir.filePath(QStringLiteral("cache")));
    const auto path = cache.localPathFor(QStringLiteral("connection-1"),
                                         QStringLiteral("/docs/file.txt"));
    QVERIFY(path.ok());
    QFile file(path.value());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("content");
    file.close();

    const auto cleared = cache.clearAll();
    QVERIFY(cleared.ok());
    QVERIFY(cleared.value());
    QVERIFY(!QFileInfo::exists(cache.rootPath()));
  }
};

QTEST_MAIN(TempFileCacheTest)

#include "test_temp_file_cache.moc"
