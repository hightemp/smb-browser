#include "ui/ConnectivityCheckController.h"

#include <QListView>
#include <QMutex>
#include <QPushButton>
#include <QtTest/QtTest>

namespace {

class FakeCheckUseCase final : public smb::application::ConnectivityCheckUseCase {
public:
  smb::core::Result<smb::application::ConnectivityCheckResult>
  check(const QString &connectionId,
        const smb::core::OperationContext &context) override {
    QMutexLocker locker(&mutex);
    ++callCount;
    lastConnectionId = connectionId;
    receivedCancellationToken = context.cancellationToken != nullptr;
    if (fail) {
      return smb::core::Result<
          smb::application::ConnectivityCheckResult>::failure(error);
    }
    return smb::core::Result<
        smb::application::ConnectivityCheckResult>::success(result);
  }

  QMutex mutex;
  int callCount = 0;
  QString lastConnectionId;
  bool receivedCancellationToken = false;
  bool fail = false;
  smb::core::AppError error = smb::core::AppError::none();
  smb::application::ConnectivityCheckResult result;
};

class FakePrompter final : public smb::ui::ConnectionActionPrompter {
public:
  bool confirmDeleteConnection(const smb::core::Connection &) override {
    return true;
  }

  void showError(const QString &title,
                 const smb::core::AppError &error) override {
    errors.push_back(title + QStringLiteral(":") +
                     smb::core::toString(error.code));
  }

  QVector<QString> errors;
};

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

class ConnectivityCheckControllerTest final : public QObject {
  Q_OBJECT

private slots:
  void checkButtonRunsCheckAsynchronously() {
    smb::ui::ConnectionsPanel panel;
    panel.setConnections({sampleConnection()});
    selectFirstConnection(panel);

    FakeCheckUseCase checkUseCase;
    checkUseCase.result.available = true;
    checkUseCase.result.status = smb::core::ConnectionStatus::Available;
    smb::application::OperationQueue operationQueue(1);
    FakePrompter prompter;
    smb::ui::ConnectivityCheckController controller(
        panel, checkUseCase, operationQueue, prompter);

    QVector<smb::application::ConnectivityCheckResult> completedChecks;
    connect(&controller, &smb::ui::ConnectivityCheckController::checkCompleted,
            this,
            [&completedChecks](
                const QString &,
                const smb::application::ConnectivityCheckResult &result) {
              completedChecks.push_back(result);
            });

    auto *checkButton = panel.findChild<QPushButton *>(
        QStringLiteral("panelCheckConnectionButton"));
    QVERIFY(checkButton != nullptr);
    checkButton->click();

    QTRY_COMPARE(completedChecks.size(), 1);
    QVERIFY(completedChecks.first().available);

    QMutexLocker locker(&checkUseCase.mutex);
    QCOMPARE(checkUseCase.callCount, 1);
    QCOMPARE(checkUseCase.lastConnectionId, QStringLiteral("conn-1"));
    QVERIFY(checkUseCase.receivedCancellationToken);
  }

  void failedCheckIsShownToUserAndEmittedAsStatusSignal() {
    smb::ui::ConnectionsPanel panel;
    FakeCheckUseCase checkUseCase;
    checkUseCase.fail = true;
    checkUseCase.error = smb::core::AppError::fromCode(
        smb::core::ErrorCode::Timeout, smb::core::ErrorCategory::Smb,
        QStringLiteral("Timed out."));
    smb::application::OperationQueue operationQueue(1);
    FakePrompter prompter;
    smb::ui::ConnectivityCheckController controller(
        panel, checkUseCase, operationQueue, prompter);

    QVector<smb::core::AppError> failedErrors;
    connect(&controller, &smb::ui::ConnectivityCheckController::checkFailed,
            this, [&failedErrors](const QString &,
                                  const smb::core::AppError &error) {
              failedErrors.push_back(error);
            });

    controller.checkConnection(QStringLiteral("conn-1"));

    QTRY_COMPARE(failedErrors.size(), 1);
    QVERIFY(failedErrors.first().code == smb::core::ErrorCode::Timeout);
    QTRY_VERIFY(!prompter.errors.isEmpty());
    QVERIFY(prompter.errors.first().contains(QStringLiteral("timeout")));
  }
};

QTEST_MAIN(ConnectivityCheckControllerTest)

#include "test_connectivity_check_controller.moc"
