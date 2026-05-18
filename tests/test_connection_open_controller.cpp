#include "ui/ConnectionOpenController.h"

#include <QAtomicInteger>
#include <QListView>
#include <QMutex>
#include <QPushButton>
#include <QtTest/QtTest>

namespace {

class FakeOpenUseCase final : public smb::application::ConnectionOpenUseCase {
public:
  smb::core::Result<smb::application::OpenConnectionResult>
  open(const QString &connectionId,
       const smb::core::OperationContext &context) override {
    QMutexLocker locker(&mutex);
    ++callCount;
    lastConnectionId = connectionId;
    receivedCancellationToken = context.cancellationToken != nullptr;
    if (fail) {
      return smb::core::Result<smb::application::OpenConnectionResult>::failure(
          error);
    }
    return smb::core::Result<smb::application::OpenConnectionResult>::success(
        result);
  }

  QMutex mutex;
  int callCount = 0;
  QString lastConnectionId;
  bool receivedCancellationToken = false;
  bool fail = false;
  smb::core::AppError error = smb::core::AppError::none();
  smb::application::OpenConnectionResult result;
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
  connection.username = QStringLiteral("user");
  connection.authType = smb::core::AuthType::Password;
  return connection;
}

smb::application::OpenConnectionResult openResult() {
  smb::application::OpenConnectionResult result;
  result.connection = sampleConnection();
  result.currentRemotePath = QStringLiteral("/");

  smb::core::RemoteFileEntry entry;
  entry.name = QStringLiteral("budget.xlsx");
  entry.remotePath = QStringLiteral("/budget.xlsx");
  entry.type = smb::core::RemoteFileType::File;
  entry.size = 42;
  result.entries.push_back(entry);
  return result;
}

void selectFirstConnection(smb::ui::ConnectionsPanel &panel) {
  auto *list = panel.findChild<QListView *>(QStringLiteral("connectionsList"));
  QVERIFY(list != nullptr);
  list->selectionModel()->select(list->model()->index(0, 0),
                                 QItemSelectionModel::ClearAndSelect |
                                     QItemSelectionModel::Rows);
}

} // namespace

class ConnectionOpenControllerTest final : public QObject {
  Q_OBJECT

private slots:
  void connectButtonOpensConnectionAsynchronously() {
    smb::ui::ConnectionsPanel panel;
    panel.setConnections({sampleConnection()});
    selectFirstConnection(panel);

    FakeOpenUseCase openUseCase;
    openUseCase.result = openResult();
    smb::application::OperationQueue operationQueue(1);
    FakePrompter prompter;
    smb::ui::ConnectionOpenController controller(panel, openUseCase,
                                                 operationQueue, prompter);

    QVector<smb::application::OpenConnectionResult> openedConnections;
    connect(&controller, &smb::ui::ConnectionOpenController::connectionOpened,
            this,
            [&openedConnections](
                const smb::application::OpenConnectionResult &result) {
              openedConnections.push_back(result);
            });

    auto *connectButton =
        panel.findChild<QPushButton *>(QStringLiteral("panelConnectButton"));
    QVERIFY(connectButton != nullptr);
    connectButton->click();

    QTRY_COMPARE(openedConnections.size(), 1);
    QCOMPARE(openedConnections.first().connection.id, QStringLiteral("conn-1"));
    QCOMPARE(openedConnections.first().entries.size(), 1);
    QCOMPARE(openedConnections.first().entries.first().name,
             QStringLiteral("budget.xlsx"));

    QMutexLocker locker(&openUseCase.mutex);
    QCOMPARE(openUseCase.callCount, 1);
    QCOMPARE(openUseCase.lastConnectionId, QStringLiteral("conn-1"));
    QVERIFY(openUseCase.receivedCancellationToken);
  }

  void failedOpenIsShownToUserAndEmittedAsStatusSignal() {
    smb::ui::ConnectionsPanel panel;
    FakeOpenUseCase openUseCase;
    openUseCase.fail = true;
    openUseCase.error = smb::core::AppError::fromCode(
        smb::core::ErrorCode::PermissionDenied, smb::core::ErrorCategory::Smb,
        QStringLiteral("Permission denied."));
    smb::application::OperationQueue operationQueue(1);
    FakePrompter prompter;
    smb::ui::ConnectionOpenController controller(panel, openUseCase,
                                                 operationQueue, prompter);

    QVector<smb::core::AppError> failedErrors;
    connect(&controller,
            &smb::ui::ConnectionOpenController::connectionOpenFailed, this,
            [&failedErrors](const QString &, const smb::core::AppError &error) {
              failedErrors.push_back(error);
            });

    controller.openConnection(QStringLiteral("conn-1"));

    QTRY_COMPARE(failedErrors.size(), 1);
    QVERIFY(failedErrors.first().code ==
            smb::core::ErrorCode::PermissionDenied);
    QTRY_VERIFY(!prompter.errors.isEmpty());
    QVERIFY(
        prompter.errors.first().contains(QStringLiteral("permission_denied")));
  }
};

QTEST_MAIN(ConnectionOpenControllerTest)

#include "test_connection_open_controller.moc"
