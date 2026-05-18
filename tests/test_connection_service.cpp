#include "application/ConnectionService.h"
#include "storage/ConnectionRepository.h"
#include "storage/SqliteStorage.h"

#include <QFile>
#include <QHash>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest/QtTest>

namespace {

class FakeCredentialStore final : public smb::core::CredentialStore {
public:
  smb::core::Result<QString>
  save(const QString &ownerId,
       const smb::core::CredentialSecret &secret) override {
    const auto ref =
        QStringLiteral("fake:%1:%2")
            .arg(ownerId, QUuid::createUuid().toString(QUuid::WithoutBraces));
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
    if (!values.contains(credentialRef)) {
      return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
          smb::core::ErrorCode::CredentialNotFound,
          smb::core::ErrorCategory::Credentials,
          QStringLiteral("Credential was not found.")));
    }
    values.insert(credentialRef, secret.bytes);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> remove(const QString &credentialRef) override {
    removedRefs.push_back(credentialRef);
    return smb::core::Result<bool>::success(values.remove(credentialRef) > 0);
  }

  smb::core::Result<bool> isAvailable() const override {
    return smb::core::Result<bool>::success(true);
  }

  QHash<QString, QByteArray> values;
  QVector<QString> removedRefs;
};

smb::core::Connection sampleConnection() {
  auto connection = smb::core::Connection::createEmpty();
  connection.name = QStringLiteral("Engineering Share");
  connection.inputPath = QStringLiteral("\\\\server\\share");
  connection.normalizedUri = QStringLiteral("smb://server/share");
  connection.server = QStringLiteral("server");
  connection.share = QStringLiteral("share");
  connection.domain = QStringLiteral("DOMAIN");
  connection.username = QStringLiteral("user");
  connection.authType = smb::core::AuthType::Password;
  return connection;
}

} // namespace

class ConnectionServiceTest final : public QObject {
  Q_OBJECT

private slots:
  void createPasswordConnectionStoresOnlyCredentialRefInSqlite() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto dbPath = tempDir.filePath(QStringLiteral("app.db"));

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(!storage.open(dbPath).hasError());
    QVERIFY(!storage.migrate().hasError());

    smb::infrastructure::ConnectionRepository repository(storage.database());
    FakeCredentialStore credentialStore;
    smb::application::ConnectionService service(repository, credentialStore);

    const smb::core::CredentialSecret secret{
        QByteArrayLiteral("SuperSecretPassword123!")};
    const auto created = service.create(sampleConnection(), secret);
    QVERIFY2(created.ok(),
             qPrintable(created.error().sanitizedTechnicalDetails));
    QVERIFY(!created.value().credentialRef.isEmpty());
    QVERIFY(credentialStore.values.contains(created.value().credentialRef));

    const auto loaded = repository.getById(created.value().id);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.value().credentialRef, created.value().credentialRef);

    QFile dbFile(dbPath);
    QVERIFY(dbFile.open(QIODevice::ReadOnly));
    const auto bytes = dbFile.readAll();
    QVERIFY(!bytes.contains("SuperSecretPassword123!"));
  }

  void guestConnectionDoesNotStoreCredential() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    smb::infrastructure::ConnectionRepository repository(storage.database());
    FakeCredentialStore credentialStore;
    smb::application::ConnectionService service(repository, credentialStore);

    auto connection = sampleConnection();
    connection.authType = smb::core::AuthType::Guest;

    const auto created = service.create(connection, std::nullopt);
    QVERIFY(created.ok());
    QVERIFY(created.value().credentialRef.isEmpty());
    QVERIFY(credentialStore.values.isEmpty());
  }

  void switchingFromPasswordToGuestRemovesSecret() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    smb::infrastructure::ConnectionRepository repository(storage.database());
    FakeCredentialStore credentialStore;
    smb::application::ConnectionService service(repository, credentialStore);

    const auto created = service.create(
        sampleConnection(),
        smb::core::CredentialSecret{QByteArrayLiteral("secret")});
    QVERIFY(created.ok());
    const auto credentialRef = created.value().credentialRef;

    auto updatedConnection = created.value();
    updatedConnection.authType = smb::core::AuthType::Guest;
    const auto updated = service.update(updatedConnection, std::nullopt);
    QVERIFY2(updated.ok(),
             qPrintable(updated.error().sanitizedTechnicalDetails));
    QVERIFY(updated.value().credentialRef.isEmpty());
    QVERIFY(!credentialStore.values.contains(credentialRef));
    QVERIFY(credentialStore.removedRefs.contains(credentialRef));
  }

  void removingConnectionRemovesUnsharedSecret() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    smb::infrastructure::ConnectionRepository repository(storage.database());
    FakeCredentialStore credentialStore;
    smb::application::ConnectionService service(repository, credentialStore);

    const auto created = service.create(
        sampleConnection(),
        smb::core::CredentialSecret{QByteArrayLiteral("secret")});
    QVERIFY(created.ok());
    const auto credentialRef = created.value().credentialRef;

    const auto removed = service.remove(created.value().id);
    QVERIFY2(removed.ok(),
             qPrintable(removed.error().sanitizedTechnicalDetails));
    QVERIFY(removed.value());
    QVERIFY(!credentialStore.values.contains(credentialRef));
    QVERIFY(credentialStore.removedRefs.contains(credentialRef));
  }

  void listReturnsConnectionsThroughServiceLayer() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    smb::infrastructure::ConnectionRepository repository(storage.database());
    FakeCredentialStore credentialStore;
    smb::application::ConnectionService service(repository, credentialStore);

    auto first = sampleConnection();
    first.name = QStringLiteral("First");
    auto second = sampleConnection();
    second.name = QStringLiteral("Second");

    QVERIFY(service
                .create(first, smb::core::CredentialSecret{QByteArrayLiteral(
                                   "first-secret")})
                .ok());
    QVERIFY(service
                .create(second, smb::core::CredentialSecret{QByteArrayLiteral(
                                    "second-secret")})
                .ok());

    const auto listed = service.list();
    QVERIFY(listed.ok());
    QCOMPARE(listed.value().size(), 2);
  }
};

QTEST_MAIN(ConnectionServiceTest)

#include "test_connection_service.moc"
