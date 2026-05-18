#include "application/ConnectionImportExportService.h"
#include "storage/SqliteStorage.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>

namespace {

class FakeCredentialStore final : public smb::core::CredentialStore {
public:
  smb::core::Result<QString>
  save(const QString &ownerId,
       const smb::core::CredentialSecret &secret) override {
    values.insert(ownerId, secret.bytes);
    return smb::core::Result<QString>::success(ownerId);
  }

  smb::core::Result<smb::core::CredentialSecret>
  load(const QString &credentialRef) const override {
    if (!values.contains(credentialRef)) {
      return smb::core::Result<smb::core::CredentialSecret>::failure(
          smb::core::AppError::fromCode(
              smb::core::ErrorCode::CredentialNotFound,
              smb::core::ErrorCategory::Credentials,
              QStringLiteral("missing credential")));
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
    return smb::core::Result<bool>::success(values.remove(credentialRef) > 0);
  }

  smb::core::Result<bool> isAvailable() const override {
    return smb::core::Result<bool>::success(true);
  }

  QHash<QString, QByteArray> values;
};

struct Fixture {
  QTemporaryDir tempDir;
  smb::infrastructure::SqliteStorage storage;
  std::unique_ptr<smb::infrastructure::ConnectionRepository>
      connectionRepository;
  std::unique_ptr<smb::infrastructure::ConnectionGroupRepository>
      groupRepository;
  FakeCredentialStore credentialStore;
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
  fixture->connectionRepository =
      std::make_unique<smb::infrastructure::ConnectionRepository>(
          fixture->storage.database());
  fixture->groupRepository =
      std::make_unique<smb::infrastructure::ConnectionGroupRepository>(
          fixture->storage.database());
  return fixture;
}

smb::core::Connection sampleConnection() {
  auto connection = smb::core::Connection::createEmpty();
  connection.id = QStringLiteral("conn-1");
  connection.name = QStringLiteral("Engineering");
  connection.inputPath = QStringLiteral("server/share");
  connection.normalizedUri = QStringLiteral("smb://server/share");
  connection.server = QStringLiteral("server");
  connection.share = QStringLiteral("share");
  connection.authType = smb::core::AuthType::Password;
  connection.credentialRef = QStringLiteral("cred-1");
  return connection;
}

QJsonObject rootObject(const QByteArray &bytes) {
  return QJsonDocument::fromJson(bytes).object();
}

} // namespace

class ConnectionImportExportServiceTest final : public QObject {
  Q_OBJECT

private slots:
  void exportsRepositoryDataWithoutPasswordsByDefault() {
    auto fixture = createFixture();
    QVERIFY(fixture->connectionRepository != nullptr);
    QVERIFY(fixture->connectionRepository->add(sampleConnection()).ok());
    fixture->credentialStore.values.insert(
        QStringLiteral("cred-1"), QByteArrayLiteral("plain-secret"));

    smb::application::ConnectionImportExportService service(
        *fixture->connectionRepository, *fixture->groupRepository,
        fixture->credentialStore);

    const auto exported = service.exportConnections({});
    QVERIFY(exported.ok());
    QVERIFY(!exported.value().contains("plain-secret"));
    QVERIFY(!exported.value().contains("cred-1"));
  }

  void confirmedDangerousExportReadsSecretsFromCredentialStore() {
    auto fixture = createFixture();
    QVERIFY(fixture->connectionRepository != nullptr);
    QVERIFY(fixture->connectionRepository->add(sampleConnection()).ok());
    fixture->credentialStore.values.insert(
        QStringLiteral("cred-1"), QByteArrayLiteral("plain-secret"));

    smb::application::ConnectionImportExportService service(
        *fixture->connectionRepository, *fixture->groupRepository,
        fixture->credentialStore);
    smb::application::ExportConnectionsRequest request;
    request.includePlainTextPasswords = true;
    request.plainTextPasswordExportConfirmed = true;

    const auto exported = service.exportConnections(request);
    QVERIFY(exported.ok());
    const auto connections =
        rootObject(exported.value()).value(QStringLiteral("connections")).toArray();
    QCOMPARE(connections.at(0)
                 .toObject()
                 .value(QStringLiteral("plainTextPassword"))
                 .toString(),
             QStringLiteral("plain-secret"));
  }

  void importsConnectionsIntoRepositoriesWithoutCredentialRefs() {
    smb::application::ImportExportService serializer;
    smb::application::ExportPayload payload;
    payload.connections = {sampleConnection()};
    const auto bytes = serializer.exportConnections(payload);
    QVERIFY(bytes.ok());

    auto fixture = createFixture();
    QVERIFY(fixture->connectionRepository != nullptr);
    smb::application::ConnectionImportExportService service(
        *fixture->connectionRepository, *fixture->groupRepository,
        fixture->credentialStore);

    const auto imported = service.importConnections(
        bytes.value(), smb::application::DuplicatePolicy::Skip);
    QVERIFY(imported.ok());
    QCOMPARE(imported.value().connections.size(), 1);

    const auto listed = fixture->connectionRepository->list();
    QVERIFY(listed.ok());
    QCOMPARE(listed.value().size(), 1);
    QCOMPARE(listed.value().at(0).normalizedUri,
             QStringLiteral("smb://server/share"));
    QVERIFY(listed.value().at(0).credentialRef.isEmpty());
  }
};

QTEST_MAIN(ConnectionImportExportServiceTest)

#include "test_connection_import_export_service.moc"
