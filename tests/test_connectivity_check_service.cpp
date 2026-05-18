#include "application/ConnectivityCheckService.h"
#include "fakes/FakeSmbClient.h"
#include "storage/ConnectionRepository.h"
#include "storage/SqliteStorage.h"

#include <QHash>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>
#include <utility>

namespace {

class FakeCredentialStore final : public smb::core::CredentialStore {
public:
  smb::core::Result<QString>
  save(const QString &, const smb::core::CredentialSecret &) override {
    return smb::core::Result<QString>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::Unknown, smb::core::ErrorCategory::Credentials,
        QStringLiteral("Not used.")));
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

  smb::core::Result<bool> update(const QString &,
                                 const smb::core::CredentialSecret &) override {
    return smb::core::Result<bool>::success(false);
  }

  smb::core::Result<bool> remove(const QString &) override {
    return smb::core::Result<bool>::success(false);
  }

  smb::core::Result<bool> isAvailable() const override {
    return smb::core::Result<bool>::success(true);
  }

  QHash<QString, QByteArray> values;
};

smb::core::Connection connection(QString id = QStringLiteral("conn-1")) {
  auto value = smb::core::Connection::createEmpty();
  value.id = std::move(id);
  value.name = QStringLiteral("Engineering Share");
  value.normalizedUri = QStringLiteral("smb://server/share");
  value.server = QStringLiteral("server");
  value.share = QStringLiteral("share");
  value.username = QStringLiteral("user");
  value.authType = smb::core::AuthType::Password;
  value.credentialRef = QStringLiteral("credential-ref");
  return value;
}

struct Fixture {
  QTemporaryDir tempDir;
  smb::infrastructure::SqliteStorage storage;
  std::unique_ptr<smb::infrastructure::ConnectionRepository> repository;
};

std::unique_ptr<Fixture> createFixture() {
  auto fixture = std::make_unique<Fixture>();
  if (!fixture->tempDir.isValid()) {
    return fixture;
  }
  const auto dbPath = fixture->tempDir.filePath(QStringLiteral("app.db"));
  const auto opened = fixture->storage.open(dbPath);
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
  return fixture;
}

} // namespace

class ConnectivityCheckServiceTest final : public QObject {
  Q_OBJECT

private slots:
  void successfulCheckUpdatesLastSuccessfulCheckAndClearsError() {
    auto fixture = createFixture();
    QVERIFY(fixture->tempDir.isValid());
    QVERIFY(fixture->repository != nullptr);
    QVERIFY(fixture->repository->add(connection()).ok());

    FakeCredentialStore credentialStore;
    credentialStore.values.insert(QStringLiteral("credential-ref"),
                                  QByteArrayLiteral("expected-secret"));

    smb::tests::FakeSmbClient smbClient;
    smbClient.setRequirePassword(true);
    smbClient.setExpectedSecret(QByteArrayLiteral("expected-secret"));

    smb::application::ConnectivityCheckService service(
        *fixture->repository, credentialStore, smbClient);
    const auto checked = service.check(QStringLiteral("conn-1"));

    QVERIFY2(checked.ok(),
             qPrintable(checked.error().sanitizedTechnicalDetails));
    QVERIFY(checked.value().available);
    QVERIFY(checked.value().status == smb::core::ConnectionStatus::Available);

    const auto loaded = fixture->repository->getById(QStringLiteral("conn-1"));
    QVERIFY(loaded.ok());
    QVERIFY(loaded.value().lastSuccessfulCheckAt.isValid());
    QVERIFY(loaded.value().lastErrorCode == smb::core::ErrorCode::None);
    QVERIFY(loaded.value().lastErrorMessage.isEmpty());
  }

  void failedCheckStoresSanitizedErrorAndTypedStatus() {
    auto fixture = createFixture();
    QVERIFY(fixture->repository != nullptr);
    QVERIFY(fixture->repository->add(connection()).ok());

    FakeCredentialStore credentialStore;
    credentialStore.values.insert(QStringLiteral("credential-ref"),
                                  QByteArrayLiteral("wrong-secret"));

    smb::tests::FakeSmbClient smbClient;
    smbClient.setRequirePassword(true);
    smbClient.setExpectedSecret(QByteArrayLiteral("expected-secret"));

    smb::application::ConnectivityCheckService service(
        *fixture->repository, credentialStore, smbClient);
    const auto checked = service.check(QStringLiteral("conn-1"));

    QVERIFY(checked.ok());
    QVERIFY(!checked.value().available);
    QVERIFY(checked.value().status ==
            smb::core::ConnectionStatus::AuthenticationFailed);

    const auto loaded = fixture->repository->getById(QStringLiteral("conn-1"));
    QVERIFY(loaded.ok());
    QVERIFY(loaded.value().lastErrorCode ==
            smb::core::ErrorCode::AuthenticationFailed);
    QVERIFY(!loaded.value().lastErrorMessage.contains(
        QStringLiteral("wrong-secret")));
    QVERIFY(!loaded.value().lastSuccessfulCheckAt.isValid());
  }

  void cancellationIsReturnedAsTypedStatus() {
    auto fixture = createFixture();
    QVERIFY(fixture->repository != nullptr);
    QVERIFY(fixture->repository->add(connection()).ok());

    FakeCredentialStore credentialStore;
    credentialStore.values.insert(QStringLiteral("credential-ref"),
                                  QByteArrayLiteral("expected-secret"));
    smb::tests::FakeSmbClient smbClient;
    smbClient.setRequirePassword(true);
    smbClient.setExpectedSecret(QByteArrayLiteral("expected-secret"));

    smb::core::CancellationToken token;
    token.cancel();
    smb::core::OperationContext context;
    context.cancellationToken = &token;

    smb::application::ConnectivityCheckService service(
        *fixture->repository, credentialStore, smbClient);
    const auto checked = service.check(QStringLiteral("conn-1"), context);

    QVERIFY(checked.ok());
    QVERIFY(checked.value().status == smb::core::ConnectionStatus::Error);
    QVERIFY(checked.value().error.code ==
            smb::core::ErrorCode::OperationCancelled);
  }
};

QTEST_MAIN(ConnectivityCheckServiceTest)

#include "test_connectivity_check_service.moc"
