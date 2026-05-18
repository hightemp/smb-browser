#include "storage/ConnectionGroupRepository.h"
#include "storage/ConnectionRepository.h"
#include "storage/SqliteStorage.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

smb::core::ConnectionGroup sampleGroup() {
  smb::core::ConnectionGroup group;
  group.name = QStringLiteral("Engineering");
  group.sortOrder = 10;
  return group;
}

smb::core::Connection sampleConnection(const QString &groupId) {
  auto connection = smb::core::Connection::createEmpty();
  connection.name = QStringLiteral("Grouped Share");
  connection.inputPath = QStringLiteral("server/share");
  connection.normalizedUri = QStringLiteral("smb://server/share");
  connection.server = QStringLiteral("server");
  connection.share = QStringLiteral("share");
  connection.authType = smb::core::AuthType::Guest;
  connection.groupId = groupId;
  return connection;
}

} // namespace

class ConnectionGroupRepositoryTest final : public QObject {
  Q_OBJECT

private slots:
  void crudPersistsGroups() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    smb::infrastructure::ConnectionGroupRepository repository(
        storage.database());
    const auto added = repository.add(sampleGroup());
    QVERIFY2(added.ok(), qPrintable(added.error().sanitizedTechnicalDetails));
    QVERIFY(!added.value().id.isEmpty());
    QVERIFY(added.value().createdAt.isValid());

    const auto loaded = repository.getById(added.value().id);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.value().name, QStringLiteral("Engineering"));

    auto updatedGroup = loaded.value();
    updatedGroup.name = QStringLiteral("Operations");
    updatedGroup.sortOrder = 5;
    const auto updated = repository.update(updatedGroup);
    QVERIFY(updated.ok());
    QCOMPARE(updated.value().name, QStringLiteral("Operations"));

    const auto groups = repository.list();
    QVERIFY(groups.ok());
    QCOMPARE(groups.value().size(), 1);

    const auto removed = repository.remove(added.value().id);
    QVERIFY(removed.ok());
    QVERIFY(removed.value());
  }

  void deletingGroupDoesNotDeleteConnections() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    smb::infrastructure::ConnectionGroupRepository groupRepository(
        storage.database());
    smb::infrastructure::ConnectionRepository connectionRepository(
        storage.database());

    const auto group = groupRepository.add(sampleGroup());
    QVERIFY(group.ok());
    const auto connection =
        connectionRepository.add(sampleConnection(group.value().id));
    QVERIFY2(connection.ok(),
             qPrintable(connection.error().sanitizedTechnicalDetails));

    QVERIFY(groupRepository.remove(group.value().id).ok());

    const auto loadedConnection =
        connectionRepository.getById(connection.value().id);
    QVERIFY(loadedConnection.ok());
    QVERIFY(loadedConnection.value().groupId.isEmpty());
  }
};

QTEST_MAIN(ConnectionGroupRepositoryTest)

#include "test_connection_group_repository.moc"
