#include "ui/LogViewer.h"

#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class LogViewerTest final : public QObject {
  Q_OBJECT

private slots:
  void displaysSanitizedLinesAndFiltersByLevelAndSearch() {
    smb::core::LogSanitizer sanitizer;
    smb::ui::LogViewer viewer(QString(), sanitizer);
    viewer.setLogLines({
        QStringLiteral("2026-05-18T10:00:00.000Z [info] [app] [-] Ready"),
        QStringLiteral("2026-05-18T10:01:00.000Z [error] [smb] [op-1] "
                       "Failed password=secret token=abc123"),
        QStringLiteral("2026-05-18T10:02:00.000Z [warning] [smb] [op-2] "
                       "Timeout"),
    });

    QVERIFY(viewer.displayText().contains(QStringLiteral("[info]")));
    QVERIFY(viewer.displayText().contains(QStringLiteral("[error]")));
    QVERIFY(!viewer.displayText().contains(QStringLiteral("secret")));
    QVERIFY(!viewer.displayText().contains(QStringLiteral("abc123")));
    QVERIFY(viewer.displayText().contains(QStringLiteral("password=***")));
    QVERIFY(viewer.displayText().contains(QStringLiteral("token=***")));

    auto *level = viewer.findChild<QComboBox *>(QStringLiteral("logLevelFilter"));
    auto *search = viewer.findChild<QLineEdit *>(QStringLiteral("logSearchEdit"));
    QVERIFY(level != nullptr);
    QVERIFY(search != nullptr);

    level->setCurrentText(QStringLiteral("Error"));
    QVERIFY(viewer.displayText().contains(QStringLiteral("[error]")));
    QVERIFY(!viewer.displayText().contains(QStringLiteral("[warning]")));

    search->setText(QStringLiteral("missing"));
    QVERIFY(viewer.displayText().isEmpty());
  }

  void reloadReadsFileFromDisk() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto logPath = tempDir.filePath(QStringLiteral("app.log"));
    QFile file(logPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("2026-05-18T10:00:00.000Z [debug] [app] [-] Loaded\n");
    file.close();

    smb::ui::LogViewer viewer(logPath);
    QVERIFY(viewer.reload());
    QVERIFY(viewer.displayText().contains(QStringLiteral("[debug]")));

    auto *text = viewer.findChild<QPlainTextEdit *>(QStringLiteral("logText"));
    QVERIFY(text != nullptr);
    QVERIFY(text->isReadOnly());
  }
};

QTEST_MAIN(LogViewerTest)

#include "test_log_viewer.moc"
