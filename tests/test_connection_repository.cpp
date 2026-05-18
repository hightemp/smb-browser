#include "storage/ConnectionRepository.h"
#include "storage/SqliteStorage.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

smb::core::Connection sampleConnection() {
  auto connection = smb::core::Connection::createEmpty();
  connection.name = QStringLiteral("Engineering Share");
  connection.inputPath = QStringLiteral("\\\\server\\share");
  connection.normalizedUri = QStringLiteral("smb://server/share");
  connection.server = QStringLiteral("server");
  connection.share = QStringLiteral("share");
  connection.domain = QStringLiteral("DOMAIN");
  connection.username = QStringLiteral("user");
  connection.credentialRef = QStringLiteral("credential-ref-1");
  connection.comment = QStringLiteral("test connection");
  connection.isFavorite = true;
  return connection;
}

} // namespace

class ConnectionRepositoryTest final : public QObject {
  Q_OBJECT

private slots:
  void crudPersistsConnectionMetadataOnly() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto dbPath = tempDir.filePath(QStringLiteral("app.db"));

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(!storage.open(dbPath).hasError());
    QVERIFY(!storage.migrate().hasError());

    smb::infrastructure::ConnectionRepository repository(storage.database());
    const auto added = repository.add(sampleConnection());
    QVERIFY2(added.ok(), qPrintable(added.error().sanitizedTechnicalDetails));
    QVERIFY(!added.value().id.isEmpty());
    QVERIFY(added.value().createdAt.isValid());
    QVERIFY(added.value().updatedAt.isValid());

    const auto loaded = repository.getById(added.value().id);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.value().name, QStringLiteral("Engineering Share"));
    QCOMPARE(loaded.value().credentialRef, QStringLiteral("credential-ref-1"));
    QVERIFY(loaded.value().usesStoredCredential());

    auto updatedConnection = loaded.value();
    updatedConnection.name = QStringLiteral("Renamed Share");
    updatedConnection.comment = QStringLiteral("updated");
    const auto updated = repository.update(updatedConnection);
    QVERIFY(updated.ok());
    QCOMPARE(updated.value().name, QStringLiteral("Renamed Share"));

    const auto all = repository.list();
    QVERIFY(all.ok());
    QCOMPARE(all.value().size(), 1);

    const auto removed = repository.remove(added.value().id);
    QVERIFY(removed.ok());
    QVERIFY(removed.value());

    const auto missing = repository.getById(added.value().id);
    QVERIFY(!missing.ok());
    QVERIFY(missing.error().code == smb::core::ErrorCode::FileNotFound);

    QFile dbFile(dbPath);
    QVERIFY(dbFile.open(QIODevice::ReadOnly));
    const auto bytes = dbFile.readAll();
    QVERIFY(!bytes.contains("SuperSecretPassword123!"));
  }

  void statusFieldsCanBeUpdatedSeparately() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    smb::infrastructure::ConnectionRepository repository(storage.database());
    const auto added = repository.add(sampleConnection());
    QVERIFY2(added.ok(), qPrintable(added.error().sanitizedTechnicalDetails));

    const auto openedAt = QDateTime::currentDateTimeUtc();
    QVERIFY(repository.updateLastOpened(added.value().id, openedAt).ok());
    QVERIFY(repository
                .updateLastError(added.value().id,
                                 smb::core::ErrorCode::PermissionDenied,
                                 QStringLiteral("permission denied"))
                .ok());
    QVERIFY(
        repository.updateLastSuccessfulCheck(added.value().id, openedAt).ok());

    const auto loaded = repository.getById(added.value().id);
    QVERIFY(loaded.ok());
    QVERIFY(loaded.value().lastOpenedAt.isValid());
    QVERIFY(loaded.value().lastSuccessfulCheckAt.isValid());
    QVERIFY(loaded.value().lastErrorCode == smb::core::ErrorCode::None);
    QVERIFY(loaded.value().lastErrorMessage.isEmpty());
  }

  void duplicateIdReturnsStorageError() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    auto connection = sampleConnection();
    connection.id = QStringLiteral("fixed-id");

    smb::infrastructure::ConnectionRepository repository(storage.database());
    QVERIFY(repository.add(connection).ok());

    const auto duplicate = repository.add(connection);
    QVERIFY(!duplicate.ok());
    QVERIFY(duplicate.error().code == smb::core::ErrorCode::StorageError);
    QVERIFY(duplicate.error().category == smb::core::ErrorCategory::Storage);
  }
};

QTEST_MAIN(ConnectionRepositoryTest)

#include "test_connection_repository.moc"
