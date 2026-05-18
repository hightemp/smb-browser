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

  void cleanupEnforcesSizeLimitByRemovingOldestFiles() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::application::TempFileCache cache(tempDir.path());
    const auto firstPath = cache.localPathFor(QStringLiteral("connection-1"),
                                              QStringLiteral("/docs/first.bin"));
    const auto secondPath = cache.localPathFor(QStringLiteral("connection-1"),
                                               QStringLiteral("/docs/second.bin"));
    QVERIFY(firstPath.ok());
    QVERIFY(secondPath.ok());

    QFile firstFile(firstPath.value());
    QVERIFY(firstFile.open(QIODevice::WriteOnly));
    firstFile.write(QByteArray(10, 'a'));
    firstFile.close();
    QVERIFY(firstFile.open(QIODevice::ReadWrite));
    QVERIFY(firstFile.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2),
                                  QFileDevice::FileModificationTime));
    firstFile.close();

    QFile secondFile(secondPath.value());
    QVERIFY(secondFile.open(QIODevice::WriteOnly));
    secondFile.write(QByteArray(10, 'b'));
    secondFile.close();

    smb::application::TempFileCacheCleanupOptions options;
    options.maxSizeBytes = 10;
    const auto cleaned = cache.cleanup(options);
    QVERIFY(cleaned.ok());
    QCOMPARE(cleaned.value().filesRemoved, 1);
    QCOMPARE(cleaned.value().bytesRemaining, qint64{10});
    QVERIFY(!QFileInfo::exists(firstPath.value()));
    QVERIFY(QFileInfo::exists(secondPath.value()));
  }

  void cleanupAndClearKeepProtectedOpenFiles() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::application::TempFileCache cache(tempDir.path());
    const auto protectedPath = cache.localPathFor(
        QStringLiteral("connection-1"), QStringLiteral("/docs/open.txt"));
    const auto removablePath = cache.localPathFor(
        QStringLiteral("connection-1"), QStringLiteral("/docs/remove.txt"));
    QVERIFY(protectedPath.ok());
    QVERIFY(removablePath.ok());

    QFile protectedFile(protectedPath.value());
    QVERIFY(protectedFile.open(QIODevice::WriteOnly));
    protectedFile.write("open");
    protectedFile.close();

    QFile removableFile(removablePath.value());
    QVERIFY(removableFile.open(QIODevice::WriteOnly));
    removableFile.write("remove");
    removableFile.close();

    cache.protectPath(protectedPath.value());
    QVERIFY(cache.isProtectedPath(protectedPath.value()));

    const auto cleared = cache.clearAll();
    QVERIFY(cleared.ok());
    QVERIFY(QFileInfo::exists(protectedPath.value()));
    QVERIFY(!QFileInfo::exists(removablePath.value()));

    cache.unprotectPath(protectedPath.value());
    QVERIFY(!cache.isProtectedPath(protectedPath.value()));
    QVERIFY(cache.clearAll().ok());
    QVERIFY(!QFileInfo::exists(protectedPath.value()));
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
