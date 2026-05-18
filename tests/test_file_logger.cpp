#include "logging/FileLogger.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class FileLoggerTest final : public QObject {
  Q_OBJECT

private slots:
  void writesSanitizedRecordsToFile() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto logPath = tempDir.filePath(QStringLiteral("logs/app.log"));
    smb::core::LogSanitizer sanitizer({QStringLiteral("real-test-secret")});
    smb::infrastructure::FileLogger logger(logPath, sanitizer);

    smb::infrastructure::LogRecord record;
    record.timestampUtc =
        QDateTime(QDate(2026, 5, 18), QTime(10, 11, 12, 13), Qt::UTC);
    record.level = smb::infrastructure::LogLevel::Warning;
    record.category = QStringLiteral("smb.check");
    record.correlationId = QStringLiteral("op-123");
    record.message = QStringLiteral("Failed with password=real-test-secret");
    record.technicalDetails =
        QStringLiteral("URI smb://user:secret@server/share token=abc123");

    QVERIFY(logger.log(record));

    QFile file(logPath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const auto contents = QString::fromUtf8(file.readAll());

    QVERIFY(contents.contains(QStringLiteral("[warning]")));
    QVERIFY(contents.contains(QStringLiteral("[smb.check]")));
    QVERIFY(contents.contains(QStringLiteral("[op-123]")));
    QVERIFY(contents.contains(QStringLiteral("password=***")));
    QVERIFY(contents.contains(QStringLiteral("smb://***@server/share")));
    QVERIFY(contents.contains(QStringLiteral("token=***")));
    QVERIFY(!contents.contains(QStringLiteral("real-test-secret")));
    QVERIFY(!contents.contains(QStringLiteral("user:secret")));
    QVERIFY(!contents.contains(QStringLiteral("abc123")));
  }

  void returnsFalseWhenLogPathCannotBeOpened() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto directoryPath = tempDir.filePath(QStringLiteral("directory"));
    QVERIFY(QDir().mkpath(directoryPath));

    smb::infrastructure::FileLogger logger(directoryPath);

    smb::infrastructure::LogRecord record;
    record.message = QStringLiteral("test");

    QVERIFY(!logger.log(record));
  }
};

QTEST_MAIN(FileLoggerTest)

#include "test_file_logger.moc"
