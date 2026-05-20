#include "application/ImportExportService.h"
#include "application/TransferManager.h"
#include "fakes/FakeSmbClient.h"
#include "storage/ConnectionRepository.h"
#include "storage/SqliteStorage.h"

#ifdef SMB_BROWSER_WITH_NATIVE_SMB
#include "smb/NativeSmbErrorMapper.h"
#endif

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

constexpr auto kSecret = "plain-secret-42";
constexpr auto kToken = "access-token-42";
constexpr auto kCredentialRef = "credential-ref-42";

class FakeCredentialStore final : public smb::core::CredentialStore {
public:
  smb::core::Result<QString>
  save(const QString &ownerId,
       const smb::core::CredentialSecret &secret) override {
    secrets.insert(ownerId, secret);
    return smb::core::Result<QString>::success(ownerId);
  }

  smb::core::Result<smb::core::CredentialSecret>
  load(const QString &credentialRef) const override {
    if (!secrets.contains(credentialRef)) {
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

  QHash<QString, smb::core::CredentialSecret> secrets;
};

smb::core::Connection sampleConnection() {
  auto connection = smb::core::Connection::createEmpty();
  connection.id = QStringLiteral("conn-sec");
  connection.name = QStringLiteral("Security Test");
  connection.inputPath = QStringLiteral("\\\\server\\share");
  connection.normalizedUri = QStringLiteral("smb://server/share");
  connection.server = QStringLiteral("server");
  connection.share = QStringLiteral("share");
  connection.domain = QStringLiteral("DOMAIN");
  connection.username = QStringLiteral("user");
  connection.authType = smb::core::AuthType::Password;
  connection.credentialRef = QString::fromLatin1(kCredentialRef);
  connection.lastErrorCode = smb::core::ErrorCode::AuthenticationFailed;
  connection.lastErrorMessage =
      QStringLiteral("password=%1 token=%2")
          .arg(QString::fromLatin1(kSecret), QString::fromLatin1(kToken));
  return connection;
}

QJsonObject parseRoot(const QByteArray &bytes) {
  return QJsonDocument::fromJson(bytes).object();
}

QByteArray readFile(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

void verifyNoSyntheticSecrets(const QByteArray &bytes) {
  QVERIFY2(!bytes.contains(kSecret), bytes.constData());
  QVERIFY2(!bytes.contains(kToken), bytes.constData());
}

void verifyNoSyntheticSecrets(const QString &text) {
  verifyNoSyntheticSecrets(text.toUtf8());
}

} // namespace

class SecurityRegressionTest final : public QObject {
  Q_OBJECT

private slots:
  void defaultExportAndImportMetadataAreSecretFree() {
    smb::application::ImportExportService service;
    smb::application::ExportPayload payload;
    payload.connections = {sampleConnection()};

    const auto exported = service.exportConnections(payload);
    QVERIFY(exported.ok());
    verifyNoSyntheticSecrets(exported.value());
    QVERIFY(!exported.value().contains(kCredentialRef));
    QVERIFY(!exported.value().contains("credentialRef"));
    QVERIFY(!exported.value().contains("plainTextPassword"));

    const auto connection = parseRoot(exported.value())
                                .value(QStringLiteral("connections"))
                                .toArray()
                                .at(0)
                                .toObject();
    QCOMPARE(connection.value(QStringLiteral("lastErrorMessage")).toString(),
             QStringLiteral("password=*** token=***"));

    const auto imported = service.importConnections(exported.value());
    QVERIFY(imported.ok());
    QCOMPARE(imported.value().connections.size(), 1);
    QVERIFY(imported.value().connections.at(0).credentialRef.isEmpty());
    verifyNoSyntheticSecrets(
        imported.value().connections.at(0).lastErrorMessage);
  }

  void plainTextPasswordExportStillRequiresExplicitConfirmation() {
    smb::application::ImportExportService service;
    smb::application::ExportPayload payload;
    payload.connections = {sampleConnection()};

    smb::application::ExportOptions unconfirmed;
    unconfirmed.includePlainTextPasswords = true;
    auto exported = service.exportConnections(payload, unconfirmed);
    QVERIFY(!exported.ok());
    QVERIFY(exported.error().category == smb::core::ErrorCategory::Validation);

    smb::application::ExportOptions missingStore;
    missingStore.includePlainTextPasswords = true;
    missingStore.confirmPlainTextPasswordExport = true;
    exported = service.exportConnections(payload, missingStore);
    QVERIFY(!exported.ok());
    QVERIFY(exported.error().code ==
            smb::core::ErrorCode::CredentialStoreUnavailable);

    FakeCredentialStore credentialStore;
    credentialStore.secrets.insert(
        QString::fromLatin1(kCredentialRef),
        smb::core::CredentialSecret{QByteArray(kSecret)});

    smb::application::ExportOptions confirmed;
    confirmed.includePlainTextPasswords = true;
    confirmed.confirmPlainTextPasswordExport = true;
    confirmed.credentialStore = &credentialStore;
    exported = service.exportConnections(payload, confirmed);
    QVERIFY(exported.ok());
    QVERIFY(exported.value().contains(kSecret));
    QVERIFY(!exported.value().contains(kToken));

    const auto connection = parseRoot(exported.value())
                                .value(QStringLiteral("connections"))
                                .toArray()
                                .at(0)
                                .toObject();
    QCOMPARE(connection.value(QStringLiteral("plainTextPassword")).toString(),
             QString::fromLatin1(kSecret));
  }

  void sqlitePersistsOnlySanitizedErrorMetadataAndCredentialRefs() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto databasePath = tempDir.filePath(QStringLiteral("app.db"));

    {
      smb::infrastructure::SqliteStorage storage;
      QVERIFY(!storage.open(databasePath).hasError());
      QVERIFY(!storage.migrate().hasError());

      smb::infrastructure::ConnectionRepository repository(
          storage.database());
      auto added = repository.add(sampleConnection());
      QVERIFY(added.ok());

      auto listed = repository.list();
      QVERIFY(listed.ok());
      QCOMPARE(listed.value().size(), 1);
      verifyNoSyntheticSecrets(listed.value().at(0).lastErrorMessage);
      QVERIFY(listed.value().at(0).lastErrorMessage.contains(
          QStringLiteral("***")));

      QSqlQuery columns(storage.database());
      QVERIFY(columns.exec(QStringLiteral("PRAGMA table_info(connections)")));
      while (columns.next()) {
        const auto columnName = columns.value(1).toString().toLower();
        QVERIFY(!columnName.contains(QStringLiteral("password")));
        QVERIFY(!columnName.contains(QStringLiteral("secret")));
        QVERIFY(!columnName.contains(QStringLiteral("plain")));
      }
    }

    verifyNoSyntheticSecrets(readFile(databasePath));
  }

  void transferOperationSnapshotsDoNotExposeCredentials() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::tests::FakeSmbClient smbClient;
    smbClient.setRequirePassword(true);
    smbClient.setExpectedSecret(QByteArray(kSecret));
    smbClient.addFile(QStringLiteral("/remote.txt"), QByteArrayLiteral("data"));

    auto connection = sampleConnection();
    connection.normalizedUri =
        QStringLiteral("smb://DOMAIN;user:%1@server/share")
            .arg(QString::fromLatin1(kSecret));

    smb::application::OperationQueue queue(1);
    smb::application::TransferManager manager(queue, smbClient);
    const auto operationId = manager.downloadFile(
        connection, smb::core::CredentialSecret{QByteArray(kSecret)},
        QStringLiteral("/remote.txt"),
        tempDir.filePath(QStringLiteral("remote.txt")));

    QTRY_VERIFY(queue.snapshot(operationId).state ==
                smb::application::OperationState::Completed);

    const auto snapshot = queue.snapshot(operationId);
    verifyNoSyntheticSecrets(snapshot.name);
    QVERIFY(!snapshot.name.contains(connection.credentialRef));
    QVERIFY(!snapshot.name.contains(QStringLiteral("DOMAIN;user")));
    QVERIFY(!snapshot.error.sanitizedTechnicalDetails.contains(
        QString::fromLatin1(kSecret)));
  }

  void nativeErrorsAndSourcesDoNotExposeSecretsToDiagnostics() {
#ifdef SMB_BROWSER_WITH_NATIVE_SMB
    smb::core::LogSanitizer sanitizer;
    sanitizer.addSecretValue(QString::fromLatin1(kSecret));
    sanitizer.addSecretValue(QString::fromLatin1(kToken));
    const smb::native_smb::ProtocolError nativeError{
        smb::native_smb::ErrorCode::AuthenticationFailed,
        "session setup failed password=plain-secret-42 token=access-token-42 "
        "smb://DOMAIN;user:plain-secret-42@server/share"};

    const auto error =
        smb::infrastructure::makeNativeSmbError(nativeError, sanitizer);
    verifyNoSyntheticSecrets(error.sanitizedTechnicalDetails);
    QVERIFY(error.sanitizedTechnicalDetails.contains(QStringLiteral("***")));

    const QDir sourceDir(QStringLiteral(SMB_BROWSER_SOURCE_DIR));
    const auto nativeDir = sourceDir.filePath(QStringLiteral("src/native_smb"));
    QVERIFY2(QFileInfo::exists(nativeDir), qPrintable(nativeDir));

    const QByteArrayList forbiddenDiagnostics{
        QByteArrayLiteral("std::cout"),
        QByteArrayLiteral("std::cerr"),
        QByteArrayLiteral("stdout"),
        QByteArrayLiteral("stderr"),
        QByteArrayLiteral("qDebug"),
        QByteArrayLiteral("qWarning"),
        QByteArrayLiteral("printf("),
        QByteArrayLiteral("fprintf("),
    };

    QDirIterator it(nativeDir, QStringList{QStringLiteral("*.cpp"),
                                           QStringLiteral("*.h")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const auto path = it.next();
      const auto bytes = readFile(path);
      for (const auto &token : forbiddenDiagnostics) {
        QVERIFY2(!bytes.contains(token),
                 qPrintable(QStringLiteral("%1 contains %2")
                                .arg(path, QString::fromLatin1(token))));
      }
    }
#else
    QSKIP("Native SMB backend is disabled.");
#endif
  }
};

QTEST_MAIN(SecurityRegressionTest)

#include "test_security_regression.moc"
