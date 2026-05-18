#include "application/OperationQueue.h"

#include <QSignalSpy>
#include <QThread>
#include <QtTest/QtTest>
#include <atomic>

class OperationQueueTest final : public QObject {
  Q_OBJECT

private slots:
  void runsOperationsInQueueOrder() {
    smb::application::OperationQueue queue(1);
    QVector<int> order;

    const auto first = queue.enqueue(
        QStringLiteral("first"), [&order](const smb::core::OperationContext &) {
          order.push_back(1);
          return smb::core::Result<bool>::success(true);
        });
    const auto second =
        queue.enqueue(QStringLiteral("second"),
                      [&order](const smb::core::OperationContext &) {
                        order.push_back(2);
                        return smb::core::Result<bool>::success(true);
                      });

    QTRY_VERIFY(queue.snapshot(first).state ==
                smb::application::OperationState::Completed);
    QTRY_VERIFY(queue.snapshot(second).state ==
                smb::application::OperationState::Completed);
    QCOMPARE(order, QVector<int>({1, 2}));
  }

  void reportsProgress() {
    smb::application::OperationQueue queue(1);

    const auto id = queue.enqueue(
        QStringLiteral("progress"),
        [](const smb::core::OperationContext &context) {
          if (context.progressCallback) {
            context.progressCallback(smb::core::TransferProgress{5, 10});
          }
          return smb::core::Result<bool>::success(true);
        });

    QTRY_VERIFY(queue.snapshot(id).progress.bytesTransferred == 5);
    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Completed);
    QCOMPARE(queue.snapshot(id).progress.totalBytes, qint64(10));
  }

  void cancellationIsDeliveredToOperation() {
    smb::application::OperationQueue queue(1);

    const auto id = queue.enqueue(
        QStringLiteral("cancel"),
        [](const smb::core::OperationContext &context) {
          while (context.cancellationToken != nullptr &&
                 !context.cancellationToken->isCancellationRequested()) {
            QThread::msleep(5);
          }
          return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
              smb::core::ErrorCode::OperationCancelled,
              smb::core::ErrorCategory::General,
              QStringLiteral("Operation cancelled.")));
        });

    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Running);
    QVERIFY(queue.cancel(id));
    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Cancelled);
  }

  void failedOperationKeepsTypedError() {
    smb::application::OperationQueue queue(1);

    const auto id = queue.enqueue(
        QStringLiteral("fail"), [](const smb::core::OperationContext &) {
          return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
              smb::core::ErrorCode::PermissionDenied,
              smb::core::ErrorCategory::Smb,
              QStringLiteral("Permission denied.")));
        });

    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Failed);
    QVERIFY(queue.snapshot(id).error.code ==
            smb::core::ErrorCode::PermissionDenied);
  }

  void destructorCancelsRunningOperationsAndWaitsForThem() {
    std::atomic_bool observedCancellation = false;

    {
      smb::application::OperationQueue queue(1);
      const auto id = queue.enqueue(
          QStringLiteral("long-running"),
          [&observedCancellation](
              const smb::core::OperationContext &context) {
            while (context.cancellationToken != nullptr &&
                   !context.cancellationToken->isCancellationRequested()) {
              QThread::msleep(5);
            }
            observedCancellation.store(true);
            return smb::core::Result<bool>::failure(
                smb::core::AppError::fromCode(
                    smb::core::ErrorCode::OperationCancelled,
                    smb::core::ErrorCategory::General,
                    QStringLiteral("cancelled by destructor")));
          });
      QTRY_VERIFY(queue.snapshot(id).state ==
                  smb::application::OperationState::Running);
    }

    QVERIFY(observedCancellation.load());
  }

  void progressAfterCancellationDoesNotOverwriteTerminalState() {
    smb::application::OperationQueue queue(1);

    const auto id = queue.enqueue(
        QStringLiteral("late-progress"),
        [](const smb::core::OperationContext &context) {
          while (context.cancellationToken != nullptr &&
                 !context.cancellationToken->isCancellationRequested()) {
            QThread::msleep(5);
          }
          if (context.progressCallback) {
            context.progressCallback(smb::core::TransferProgress{50, 100});
          }
          return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
              smb::core::ErrorCode::OperationCancelled,
              smb::core::ErrorCategory::General,
              QStringLiteral("Operation cancelled.")));
        });

    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Running);
    QVERIFY(queue.cancel(id));
    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Cancelled);
    QCOMPARE(queue.snapshot(id).progress.totalBytes, qint64(100));
  }

  void operationChangedIsDeliveredOnReceiverThread() {
    smb::application::OperationQueue queue(1);
    QVector<bool> deliveredOnTestThread;

    connect(&queue, &smb::application::OperationQueue::operationChanged, this,
            [&deliveredOnTestThread, this]() {
              deliveredOnTestThread.push_back(QThread::currentThread() ==
                                              this->thread());
            });

    const auto id = queue.enqueue(
        QStringLiteral("thread-check"),
        [](const smb::core::OperationContext &) {
          return smb::core::Result<bool>::success(true);
        });

    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Completed);
    QTRY_VERIFY(!deliveredOnTestThread.isEmpty());
    for (const auto delivered : deliveredOnTestThread) {
      QVERIFY(delivered);
    }
  }
};

QTEST_MAIN(OperationQueueTest)

#include "test_operation_queue.moc"
