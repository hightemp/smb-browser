#include "application/ImportExportService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

namespace {

class FakeCredentialStore final : public smb::core::CredentialStore {
public:
  QHash<QString, smb::core::CredentialSecret> secrets;
  bool failLoad = false;

  smb::core::Result<QString>
  save(const QString &ownerId,
       const smb::core::CredentialSecret &secret) override {
    secrets.insert(ownerId, secret);
    return smb::core::Result<QString>::success(ownerId);
  }

  smb::core::Result<smb::core::CredentialSecret>
  load(const QString &credentialRef) const override {
    if (failLoad || !secrets.contains(credentialRef)) {
      return smb::core::Result<smb::core::CredentialSecret>::failure(
          smb::core::AppError::fromCode(
              smb::core::ErrorCode::CredentialNotFound,
              smb::core::ErrorCategory::Credentials,
              QStringLiteral("credential not found")));
    }
    return smb::core::Result<smb::core::CredentialSecret>::success(
        secrets.value(credentialRef));
  }

  smb::core::Result<bool>
  update(const QString &credentialRef,
         const smb::core::CredentialSecret &secret) override {
    secrets.insert(credentialRef, secret);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> remove(const QString &credentialRef) override {
    return smb::core::Result<bool>::success(secrets.remove(credentialRef) > 0);
  }

  smb::core::Result<bool> isAvailable() const override {
    return smb::core::Result<bool>::success(true);
  }
};

smb::core::Connection sampleConnection() {
  auto connection = smb::core::Connection::createEmpty();
  connection.id = QStringLiteral("conn-1");
  connection.name = QStringLiteral("Engineering");
  connection.inputPath = QStringLiteral("\\\\server\\share");
  connection.normalizedUri = QStringLiteral("smb://server/share");
  connection.server = QStringLiteral("server");
  connection.share = QStringLiteral("share");
  connection.initialRemotePath = QStringLiteral("docs");
  connection.domain = QStringLiteral("DOMAIN");
  connection.username = QStringLiteral("user");
  connection.authType = smb::core::AuthType::Password;
  connection.credentialRef = QStringLiteral("known-secret-ref");
  connection.comment = QStringLiteral("metadata only");
  connection.groupId = QStringLiteral("group-1");
  connection.isFavorite = true;
  connection.lastErrorCode = smb::core::ErrorCode::AuthenticationFailed;
  connection.lastErrorMessage =
      QStringLiteral("password=known-secret-value token=abc123");
  return connection;
}

smb::core::ConnectionGroup sampleGroup() {
  smb::core::ConnectionGroup group;
  group.id = QStringLiteral("group-1");
  group.name = QStringLiteral("Engineering");
  group.sortOrder = 10;
  return group;
}

QJsonObject parseRoot(const QByteArray &bytes) {
  const auto document = QJsonDocument::fromJson(bytes);
  return document.object();
}

} // namespace

class ImportExportServiceTest final : public QObject {
  Q_OBJECT

private slots:
  void defaultExportWritesVersionedMetadata() {
    smb::application::ImportExportService service;
    smb::application::ExportPayload payload;
    payload.connections = {sampleConnection()};
    payload.groups = {sampleGroup()};

    const auto exported = service.exportConnections(payload);
    QVERIFY(exported.ok());

    const auto root = parseRoot(exported.value());
    QCOMPARE(root.value(QStringLiteral("schema")).toString(),
             QStringLiteral("smb-browser.connections.export"));
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(root.value(QStringLiteral("exportedAt")).isString());

    const auto connections =
        root.value(QStringLiteral("connections")).toArray();
    QCOMPARE(connections.size(), 1);
    const auto connection = connections.at(0).toObject();
    QCOMPARE(connection.value(QStringLiteral("normalizedUri")).toString(),
             QStringLiteral("smb://server/share"));
    QCOMPARE(connection.value(QStringLiteral("server")).toString(),
             QStringLiteral("server"));
    QCOMPARE(connection.value(QStringLiteral("share")).toString(),
             QStringLiteral("share"));
    QVERIFY(connection.value(QStringLiteral("favorite")).toBool());

    const auto groups = root.value(QStringLiteral("groups")).toArray();
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("Engineering"));
  }

  void defaultExportDoesNotContainCredentialDataOrKnownSecrets() {
    smb::application::ImportExportService service;
    smb::application::ExportPayload payload;
    payload.connections = {sampleConnection()};

    const auto exported = service.exportConnections(payload);
    QVERIFY(exported.ok());

    const auto bytes = exported.value();
    QVERIFY(!bytes.contains("known-secret-ref"));
    QVERIFY(!bytes.contains("known-secret-value"));
    QVERIFY(!bytes.contains("abc123"));
    QVERIFY(!bytes.contains("credentialRef"));
    QVERIFY(!bytes.contains("plainTextPassword"));

    const auto root = parseRoot(bytes);
    const auto connection =
        root.value(QStringLiteral("connections")).toArray().at(0).toObject();
    QVERIFY(!connection.contains(QStringLiteral("credentialRef")));
    QVERIFY(!connection.contains(QStringLiteral("password")));
    QVERIFY(!connection.contains(QStringLiteral("plainTextPassword")));
    QCOMPARE(connection.value(QStringLiteral("lastErrorMessage")).toString(),
             QStringLiteral("password=*** token=***"));
  }

  void plainTextPasswordExportIsRejectedByDefaultService() {
    smb::application::ImportExportService service;
    smb::application::ExportPayload payload;
    payload.connections = {sampleConnection()};
    smb::application::ExportOptions options;
    options.includePlainTextPasswords = true;

    const auto exported = service.exportConnections(payload, options);
    QVERIFY(!exported.ok());
    QVERIFY(exported.error().category == smb::core::ErrorCategory::Validation);
  }

  void plainTextPasswordExportRequiresCredentialStore() {
    smb::application::ImportExportService service;
    smb::application::ExportPayload payload;
    payload.connections = {sampleConnection()};
    smb::application::ExportOptions options;
    options.includePlainTextPasswords = true;
    options.confirmPlainTextPasswordExport = true;

    const auto exported = service.exportConnections(payload, options);
    QVERIFY(!exported.ok());
    QVERIFY(exported.error().code ==
            smb::core::ErrorCode::CredentialStoreUnavailable);
  }

  void confirmedPlainTextPasswordExportIncludesPasswords() {
    FakeCredentialStore credentialStore;
    credentialStore.secrets.insert(
        QStringLiteral("known-secret-ref"),
        smb::core::CredentialSecret{QByteArrayLiteral("plain-secret")});

    auto guestConnection = sampleConnection();
    guestConnection.id = QStringLiteral("guest-1");
    guestConnection.authType = smb::core::AuthType::Guest;
    guestConnection.credentialRef.clear();

    smb::application::ImportExportService service;
    smb::application::ExportPayload payload;
    payload.connections = {sampleConnection(), guestConnection};
    smb::application::ExportOptions options;
    options.includePlainTextPasswords = true;
    options.confirmPlainTextPasswordExport = true;
    options.credentialStore = &credentialStore;

    const auto exported = service.exportConnections(payload, options);
    QVERIFY(exported.ok());

    const auto connections =
        parseRoot(exported.value()).value(QStringLiteral("connections")).toArray();
    QCOMPARE(connections.size(), 2);
    QCOMPARE(connections.at(0)
                 .toObject()
                 .value(QStringLiteral("plainTextPassword"))
                 .toString(),
             QStringLiteral("plain-secret"));
    QVERIFY(!connections.at(1).toObject().contains(
        QStringLiteral("plainTextPassword")));
  }

  void confirmedPlainTextPasswordExportFailsWhenSecretCannotBeRead() {
    FakeCredentialStore credentialStore;
    credentialStore.failLoad = true;

    smb::application::ImportExportService service;
    smb::application::ExportPayload payload;
    payload.connections = {sampleConnection()};
    smb::application::ExportOptions options;
    options.includePlainTextPasswords = true;
    options.confirmPlainTextPasswordExport = true;
    options.credentialStore = &credentialStore;

    const auto exported = service.exportConnections(payload, options);
    QVERIFY(!exported.ok());
    QVERIFY(exported.error().code == smb::core::ErrorCode::CredentialNotFound);
  }

  void importsVersionedExportAndClearsCredentialRefs() {
    smb::application::ImportExportService service;
    smb::application::ExportPayload payload;
    payload.connections = {sampleConnection()};
    payload.groups = {sampleGroup()};

    const auto exported = service.exportConnections(payload);
    QVERIFY(exported.ok());

    const auto imported = service.importConnections(exported.value());
    QVERIFY(imported.ok());
    QCOMPARE(imported.value().connections.size(), 1);
    QCOMPARE(imported.value().groups.size(), 1);
    QVERIFY(imported.value().errors.isEmpty());

    const auto connection = imported.value().connections.at(0);
    QCOMPARE(connection.normalizedUri, QStringLiteral("smb://server/share"));
    QCOMPARE(connection.server, QStringLiteral("server"));
    QCOMPARE(connection.share, QStringLiteral("share"));
    QVERIFY(connection.authType == smb::core::AuthType::Password);
    QVERIFY(connection.credentialRef.isEmpty());
    QVERIFY(!connection.lastErrorMessage.contains(
        QStringLiteral("known-secret-value")));
  }

  void rejectsUnsupportedSchemaVersion() {
    smb::application::ImportExportService service;
    auto root = parseRoot(service.exportConnections({}).value());
    root.insert(QStringLiteral("version"), 999);

    const auto imported =
        service.importConnections(QJsonDocument(root).toJson());
    QVERIFY(!imported.ok());
    QVERIFY(imported.error().category == smb::core::ErrorCategory::Validation);
  }

  void reportsInvalidConnectionPathWithoutSecrets() {
    QJsonObject connection;
    connection.insert(QStringLiteral("name"), QStringLiteral("Broken"));
    connection.insert(QStringLiteral("normalizedUri"),
                      QStringLiteral("smb://server"));
    connection.insert(QStringLiteral("lastErrorMessage"),
                      QStringLiteral("password=known-secret-value"));

    QJsonObject root;
    root.insert(QStringLiteral("schema"),
                QStringLiteral("smb-browser.connections.export"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("connections"), QJsonArray{connection});
    root.insert(QStringLiteral("groups"), QJsonArray{});

    smb::application::ImportExportService service;
    const auto imported =
        service.importConnections(QJsonDocument(root).toJson());

    QVERIFY(imported.ok());
    QVERIFY(imported.value().connections.isEmpty());
    QCOMPARE(imported.value().errors.size(), 1);
    QCOMPARE(imported.value().errors.at(0).recordName,
             QStringLiteral("Broken"));
    QVERIFY(!imported.value()
                 .errors.at(0)
                 .error.sanitizedTechnicalDetails.contains(
                     QStringLiteral("known-secret-value")));
  }

  void duplicatePolicyCanSkipReplaceOrCreateCopy() {
    smb::application::ImportExportService service;
    smb::application::ExportPayload payload;
    payload.connections = {sampleConnection()};
    const auto exported = service.exportConnections(payload);
    QVERIFY(exported.ok());

    smb::application::ImportOptions skipOptions;
    skipOptions.existingConnectionIds.insert(QStringLiteral("conn-1"));
    skipOptions.duplicatePolicy = smb::application::DuplicatePolicy::Skip;
    const auto skipped = service.importConnections(exported.value(), skipOptions);
    QVERIFY(skipped.ok());
    QVERIFY(skipped.value().connections.isEmpty());
    QCOMPARE(skipped.value().skippedDuplicates, 1);

    smb::application::ImportOptions replaceOptions = skipOptions;
    replaceOptions.duplicatePolicy = smb::application::DuplicatePolicy::Replace;
    const auto replaced =
        service.importConnections(exported.value(), replaceOptions);
    QVERIFY(replaced.ok());
    QCOMPARE(replaced.value().connections.size(), 1);
    QCOMPARE(replaced.value().connections.at(0).id, QStringLiteral("conn-1"));

    smb::application::ImportOptions copyOptions = skipOptions;
    copyOptions.duplicatePolicy = smb::application::DuplicatePolicy::CreateCopy;
    const auto copied = service.importConnections(exported.value(), copyOptions);
    QVERIFY(copied.ok());
    QCOMPARE(copied.value().connections.size(), 1);
    QVERIFY(copied.value().connections.at(0).id != QStringLiteral("conn-1"));
    QVERIFY(copied.value().connections.at(0).name.endsWith(
        QStringLiteral(" Copy")));
  }
};

QTEST_MAIN(ImportExportServiceTest)

#include "test_import_export_service.moc"
