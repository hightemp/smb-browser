#include "ui/StatusPanel.h"

#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QThread>
#include <QtTest/QtTest>

class StatusPanelTest final : public QObject {
  Q_OBJECT

private slots:
  void showsConnectionStatusAndSanitizedError() {
    smb::ui::StatusPanel panel;
    panel.setConnectionStatus(QStringLiteral("Connection: Engineering"));

    auto error = smb::core::AppError::fromCode(
        smb::core::ErrorCode::PermissionDenied,
        smb::core::ErrorCategory::Smb,
        QStringLiteral("password=secret-token token=abc123"), false);
    panel.setLastError(error);

    QCOMPARE(panel.connectionStatusText(),
             QStringLiteral("Connection: Engineering"));
    QVERIFY(panel.lastErrorText().contains(QStringLiteral("permission"),
                                           Qt::CaseInsensitive));
    QVERIFY(!panel.lastErrorText().contains(QStringLiteral("secret-token")));
    QVERIFY(!panel.lastErrorText().contains(QStringLiteral("abc123")));
    QVERIFY(panel.lastErrorText().contains(QStringLiteral("password=***")));
    QVERIFY(panel.lastErrorText().contains(QStringLiteral("token=***")));
  }

  void longErrorsDoNotForceHugeStatusPanel() {
    smb::ui::StatusPanel panel;
    const auto initialWidth = panel.sizeHint().width();

    auto error = smb::core::AppError::fromCode(
        smb::core::ErrorCode::NetworkError, smb::core::ErrorCategory::Smb,
        QString(4000, QLatin1Char('x')), false);
    panel.setLastError(error);

    QVERIFY(panel.lastErrorText().size() < 220);
    QVERIFY(panel.sizeHint().width() < initialWidth + 300);
    auto *label = panel.findChild<QLabel *>(QStringLiteral("lastErrorLabel"));
    QVERIFY(label != nullptr);
    QVERIFY(label->toolTip().size() > panel.lastErrorText().size());
  }

  void followsOperationProgressAndCancellation() {
    smb::application::OperationQueue queue(1);
    smb::ui::StatusPanel panel;
    panel.bindOperationQueue(&queue);

    auto *progress =
        panel.findChild<QProgressBar *>(QStringLiteral("operationProgressBar"));
    auto *cancel =
        panel.findChild<QPushButton *>(QStringLiteral("cancelOperationButton"));
    QVERIFY(progress != nullptr);
    QVERIFY(cancel != nullptr);

    QSignalSpy cancelSpy(&panel, &smb::ui::StatusPanel::cancelRequested);
    const auto id = queue.enqueue(
        QStringLiteral("Upload"),
        [](const smb::core::OperationContext &context) {
          while (context.cancellationToken != nullptr &&
                 !context.cancellationToken->isCancellationRequested()) {
            if (context.progressCallback) {
              context.progressCallback(smb::core::TransferProgress{5, 10});
            }
            QThread::msleep(5);
          }
          return smb::core::Result<bool>::failure(
              smb::core::AppError::fromCode(
                  smb::core::ErrorCode::OperationCancelled,
                  smb::core::ErrorCategory::General,
                  QStringLiteral("Operation cancelled.")));
        });

    QTRY_COMPARE(panel.activeOperationId(), id);
    QTRY_VERIFY(cancel->isEnabled());
    QTRY_COMPARE(progress->value(), 50);

    QTest::mouseClick(cancel, Qt::LeftButton);

    QTRY_COMPARE(cancelSpy.count(), 1);
    QCOMPARE(cancelSpy.takeFirst().at(0).toString(), id);
    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Cancelled);
    QTRY_VERIFY(!cancel->isEnabled());
  }
};

QTEST_MAIN(StatusPanelTest)

#include "test_status_panel.moc"
