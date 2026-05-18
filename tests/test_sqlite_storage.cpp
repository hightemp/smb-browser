#include "storage/SqliteStorage.h"

#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class SqliteStorageTest final : public QObject {
  Q_OBJECT

private slots:
  void migrationCreatesExpectedTablesAndVersion() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    QSqlQuery versionQuery(storage.database());
    QVERIFY(versionQuery.exec(QStringLiteral(
        "SELECT version FROM schema_migrations WHERE version = 1")));
    QVERIFY(versionQuery.next());
    QCOMPARE(versionQuery.value(0).toInt(), 1);

    QSqlQuery tableQuery(storage.database());
    QVERIFY(tableQuery.exec(
        QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table'")));
    QStringList tableNames;
    while (tableQuery.next()) {
      tableNames << tableQuery.value(0).toString();
    }

    QVERIFY(tableNames.contains(QStringLiteral("connections")));
    QVERIFY(tableNames.contains(QStringLiteral("connection_groups")));
    QVERIFY(tableNames.contains(QStringLiteral("settings")));
    QVERIFY(tableNames.contains(QStringLiteral("schema_migrations")));
  }

  void connectionsTableDoesNotContainPasswordColumns() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::infrastructure::SqliteStorage storage;
    QVERIFY(
        !storage.open(tempDir.filePath(QStringLiteral("app.db"))).hasError());
    QVERIFY(!storage.migrate().hasError());

    QSqlQuery columns(storage.database());
    QVERIFY(columns.exec(QStringLiteral("PRAGMA table_info(connections)")));
    while (columns.next()) {
      const auto columnName = columns.value(1).toString().toLower();
      QVERIFY2(!columnName.contains(QStringLiteral("password")),
               qPrintable(columnName));
      QVERIFY2(!columnName.contains(QStringLiteral("token")),
               qPrintable(columnName));
      QVERIFY2(!columnName.contains(QStringLiteral("master")),
               qPrintable(columnName));
      QVERIFY2(!columnName.contains(QStringLiteral("secret")),
               qPrintable(columnName));
    }
  }
};

QTEST_MAIN(SqliteStorageTest)

#include "test_sqlite_storage.moc"
