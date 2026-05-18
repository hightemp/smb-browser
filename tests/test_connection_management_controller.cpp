#include "ui/ConnectionManagementController.h"

#include "storage/SqliteStorage.h"

#include <QHash>
#include <QListView>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>

namespace {

class FakeCredentialStore final : public smb::core::CredentialStore {
public:
  smb::core::Result<QString>
  save(const QString &ownerId,
       const smb::core::CredentialSecret &secret) override {
    const auto ref = QStringLiteral("fake:%1").arg(ownerId);
    values.insert(ref, secret.bytes);
    return smb::core::Result<QString>::success(ref);
  }

  smb::core::Result<smb::core::CredentialSecret>
  load(const QString &credentialRef) const override {
    if (!values.contains(credentialRef)) {
      return smb::core::Result<smb::core::CredentialSecret>::failure(
          smb::core::AppError::fromCode(
              smb::core::ErrorCode::CredentialNotFound,
              smb::core::ErrorCategory::Credentials,
              QStringLiteral("Credential was not found.")));
    }
    return smb::core::Result<smb::core::CredentialSecret>::success(
        smb::core::CredentialSecret{values.value(credentialRef)});
  }

  smb::core::Result<bool>
  update(const QString &credentialRef,
         const smb::core::CredentialSecret &secret) override {
    values.insert(credentialRef, secret.bytes);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> remove(const QString &credentialRef) override {
    removedRefs.push_back(credentialRef);
    values.remove(credentialRef);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> isAvailable() const override {
    return smb::core::Result<bool>::success(true);
  }

  QHash<QString, QByteArray> values;
  QVector<QString> removedRefs;
};

class FakePrompter final : public smb::ui::ConnectionActionPrompter {
public:
  bool
  confirmDeleteConnection(const smb::core::Connection &connection) override {
    confirmedConnections.push_back(connection.id);
    return confirmDelete;
  }

  void showError(const QString &title,
                 const smb::core::AppError &error) override {
    errors.push_back(title + QStringLiteral(":") +
                     smb::core::toString(error.code));
  }

  bool confirmDelete = true;
  QVector<QString> confirmedConnections;
  QVector<QString> errors;
};

smb::core::Connection sampleConnection() {
  auto connection = smb::core::Connection::createEmpty();
  connection.id = QStringLiteral("conn-1");
  connection.name = QStringLiteral("Finance");
  connection.inputPath = QStringLiteral("server/share");
  connection.normalizedUri = QStringLiteral("smb://server/share");
  connection.server = QStringLiteral("server");
  connection.share = QStringLiteral("share");
  connection.username = QStringLiteral("user");
  connection.authType = smb::core::AuthType::Password;
  return connection;
}

struct Fixture {
  QTemporaryDir tempDir;
  smb::infrastructure::SqliteStorage storage;
  std::unique_ptr<smb::infrastructure::ConnectionRepository> repository;
  std::unique_ptr<FakeCredentialStore> credentialStore;
  std::unique_ptr<smb::application::ConnectionService> service;
};

std::unique_ptr<Fixture> createFixture() {
  auto fixture = std::make_unique<Fixture>();
  if (!fixture->tempDir.isValid()) {
    return fixture;
  }
  const auto opened = fixture->storage.open(
      fixture->tempDir.filePath(QStringLiteral("app.db")));
  if (opened.hasError()) {
    return fixture;
  }
  const auto migrated = fixture->storage.migrate();
  if (migrated.hasError()) {
    return fixture;
  }

  fixture->repository =
      std::make_unique<smb::infrastructure::ConnectionRepository>(
          fixture->storage.database());
  fixture->credentialStore = std::make_unique<FakeCredentialStore>();
  fixture->service = std::make_unique<smb::application::ConnectionService>(
      *fixture->repository, *fixture->credentialStore);
  return fixture;
}

void selectFirstConnection(smb::ui::ConnectionsPanel &panel) {
  auto *list = panel.findChild<QListView *>(QStringLiteral("connectionsList"));
  QVERIFY(list != nullptr);
  list->selectionModel()->select(list->model()->index(0, 0),
                                 QItemSelectionModel::ClearAndSelect |
                                     QItemSelectionModel::Rows);
}

} // namespace

class ConnectionManagementControllerTest final : public QObject {
  Q_OBJECT

private slots:
  void deleteRequiresConfirmationAndRefreshesList() {
    auto fixture = createFixture();
    QVERIFY(fixture->service != nullptr);

    const auto created = fixture->service->create(
        sampleConnection(),
        smb::core::CredentialSecret{QByteArrayLiteral("synthetic-secret")});
    QVERIFY(created.ok());
    const auto credentialRef = created.value().credentialRef;

    smb::ui::ConnectionsPanel panel;
    FakePrompter prompter;
    smb::ui::ConnectionManagementController controller(panel, *fixture->service,
                                                       prompter);

    controller.refreshConnections();
    auto *list =
        panel.findChild<QListView *>(QStringLiteral("connectionsList"));
    QVERIFY(list != nullptr);
    QCOMPARE(list->model()->rowCount(), 1);

    selectFirstConnection(panel);
    auto *deleteButton = panel.findChild<QPushButton *>(
        QStringLiteral("panelDeleteConnectionButton"));
    QVERIFY(deleteButton != nullptr);
    deleteButton->click();

    QCOMPARE(prompter.confirmedConnections, QVector<QString>{"conn-1"});
    QCOMPARE(list->model()->rowCount(), 0);
    QVERIFY(fixture->credentialStore->removedRefs.contains(credentialRef));
    QVERIFY(!fixture->credentialStore->values.contains(credentialRef));
  }

  void cancelledConfirmationDoesNotDelete() {
    auto fixture = createFixture();
    QVERIFY(fixture->service != nullptr);
    QVERIFY(fixture->service
                ->create(sampleConnection(),
                         smb::core::CredentialSecret{
                             QByteArrayLiteral("synthetic-secret")})
                .ok());

    smb::ui::ConnectionsPanel panel;
    FakePrompter prompter;
    prompter.confirmDelete = false;
    smb::ui::ConnectionManagementController controller(panel, *fixture->service,
                                                       prompter);
    controller.refreshConnections();

    selectFirstConnection(panel);
    panel
        .findChild<QPushButton *>(QStringLiteral("panelDeleteConnectionButton"))
        ->click();

    const auto listed = fixture->service->list();
    QVERIFY(listed.ok());
    QCOMPARE(listed.value().size(), 1);
  }

  void serviceErrorIsShownToUser() {
    auto fixture = createFixture();
    QVERIFY(fixture->service != nullptr);

    smb::ui::ConnectionsPanel panel;
    FakePrompter prompter;
    smb::ui::ConnectionManagementController controller(panel, *fixture->service,
                                                       prompter);

    controller.deleteConnection(QStringLiteral("missing"));

    QVERIFY(!prompter.errors.isEmpty());
    QVERIFY(prompter.confirmedConnections.isEmpty());
  }
};

QTEST_MAIN(ConnectionManagementControllerTest)

#include "test_connection_management_controller.moc"
