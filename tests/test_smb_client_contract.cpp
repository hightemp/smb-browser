#include "core/SmbClient.h"
#include "fakes/FakeSmbClient.h"

#ifdef SMB_BROWSER_WITH_NATIVE_SMB
#include "smb/NativeSmbClient.h"
#endif

#include <QFile>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest/QtTest>

namespace {

smb::core::Connection fakeConnection() {
  auto value = smb::core::Connection::createEmpty();
  value.id = QStringLiteral("contract-fake");
  value.name = QStringLiteral("Contract Share");
  value.normalizedUri = QStringLiteral("smb://server/share");
  value.server = QStringLiteral("server");
  value.share = QStringLiteral("share");
  return value;
}

QString resultDetails(const smb::core::AppError &error) {
  return QStringLiteral("%1: %2")
      .arg(smb::core::toString(error.code), error.sanitizedTechnicalDetails);
}

template <typename T>
void verifyOk(const smb::core::Result<T> &result, const char *operation) {
  QVERIFY2(result.ok(),
           qPrintable(QStringLiteral("%1 failed: %2")
                          .arg(QString::fromLatin1(operation),
                               resultDetails(result.error()))));
}

bool containsEntry(const QVector<smb::core::RemoteFileEntry> &entries,
                   const QString &name, smb::core::RemoteFileType type) {
  for (const auto &entry : entries) {
    if (entry.name == name && entry.type == type) {
      return true;
    }
  }
  return false;
}

void writeLocalFile(const QString &path, const QByteArray &payload) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(file.write(payload), static_cast<qint64>(payload.size()));
}

QByteArray readLocalFile(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

void runMutatingContract(smb::core::SmbClient &client,
                         const smb::core::Connection &connection,
                         const smb::core::CredentialSecret *secret,
                         const QString &rootPath) {
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());

  verifyOk(client.checkConnection(connection, secret, {}), "checkConnection");

  const auto docsPath = rootPath + QStringLiteral("/docs");
  verifyOk(client.createDirectory(connection, secret, rootPath, {}),
           "create root directory");
  verifyOk(client.createDirectory(connection, secret, docsPath, {}),
           "create child directory");

  auto listedRoot = client.listDirectory(connection, secret, rootPath, {});
  verifyOk(listedRoot, "list root");
  QVERIFY(containsEntry(listedRoot.value(), QStringLiteral("docs"),
                        smb::core::RemoteFileType::Directory));

  const auto payload = QByteArrayLiteral("contract-payload");
  const auto localSource = tempDir.filePath(QStringLiteral("source.txt"));
  writeLocalFile(localSource, payload);

  bool uploadProgress = false;
  smb::core::OperationContext uploadContext;
  uploadContext.progressCallback =
      [&uploadProgress](const smb::core::TransferProgress &progress) {
        uploadProgress = true;
        QVERIFY(progress.bytesTransferred >= 0);
        QVERIFY(progress.totalBytes >= 0);
      };
  const auto uploadedPath = docsPath + QStringLiteral("/uploaded.txt");
  verifyOk(client.uploadFile(connection, secret, localSource, uploadedPath,
                             uploadContext),
           "uploadFile");
  QVERIFY(uploadProgress);

  auto listedDocs = client.listDirectory(connection, secret, docsPath, {});
  verifyOk(listedDocs, "list docs");
  QVERIFY(containsEntry(listedDocs.value(), QStringLiteral("uploaded.txt"),
                        smb::core::RemoteFileType::File));

  bool downloadProgress = false;
  smb::core::OperationContext downloadContext;
  downloadContext.progressCallback =
      [&downloadProgress](const smb::core::TransferProgress &progress) {
        downloadProgress = true;
        QVERIFY(progress.bytesTransferred >= 0);
        QVERIFY(progress.totalBytes >= 0);
      };
  const auto localDownload = tempDir.filePath(QStringLiteral("download.txt"));
  verifyOk(client.downloadFile(connection, secret, uploadedPath, localDownload,
                               downloadContext),
           "downloadFile");
  QVERIFY(downloadProgress);
  QCOMPARE(readLocalFile(localDownload), payload);

  const auto renamedPath = docsPath + QStringLiteral("/renamed.txt");
  verifyOk(client.rename(connection, secret, uploadedPath, renamedPath, {}),
           "rename");

  const auto copiedPath = docsPath + QStringLiteral("/copied.txt");
  verifyOk(client.copy(connection, secret, renamedPath, connection, secret,
                       copiedPath, {}),
           "copy");

  const auto movedPath = docsPath + QStringLiteral("/moved.txt");
  verifyOk(client.move(connection, secret, copiedPath, connection, secret,
                       movedPath, {}),
           "move");

  const auto localMoved = tempDir.filePath(QStringLiteral("moved.txt"));
  verifyOk(client.downloadFile(connection, secret, movedPath, localMoved, {}),
           "download moved");
  QCOMPARE(readLocalFile(localMoved), payload);

  verifyOk(client.remove(connection, secret, movedPath, {}), "remove moved");
  verifyOk(client.remove(connection, secret, renamedPath, {}), "remove renamed");
  verifyOk(client.remove(connection, secret, docsPath, {}), "remove docs");
  verifyOk(client.remove(connection, secret, rootPath, {}), "remove root");
}

void runCancellationContract(smb::core::SmbClient &client,
                             const smb::core::Connection &connection,
                             const smb::core::CredentialSecret *secret) {
  smb::core::CancellationToken token;
  token.cancel();
  smb::core::OperationContext context;
  context.cancellationToken = &token;

  const auto cancelled =
      client.listDirectory(connection, secret, QStringLiteral("/"), context);
  QVERIFY(!cancelled.ok());
  QCOMPARE(static_cast<int>(cancelled.error().code),
           static_cast<int>(smb::core::ErrorCode::OperationCancelled));
}

QString envString(const char *name) {
  return QString::fromUtf8(qgetenv(name)).trimmed();
}

#ifdef SMB_BROWSER_WITH_NATIVE_SMB
struct NativeFixture {
  smb::core::Connection connection;
  smb::core::CredentialSecret secret;
  const smb::core::CredentialSecret *secretPtr = nullptr;
};

bool nativeFixtureFromEnv(NativeFixture *fixture, QString *skipReason) {
  const auto server = envString("SMB_BROWSER_NATIVE_CONTRACT_SERVER");
  const auto share = envString("SMB_BROWSER_NATIVE_CONTRACT_SHARE");
  if (server.isEmpty() || share.isEmpty()) {
    *skipReason =
        QStringLiteral("Set SMB_BROWSER_NATIVE_CONTRACT_SERVER and "
                       "SMB_BROWSER_NATIVE_CONTRACT_SHARE to run native "
                       "contract tests.");
    return false;
  }

  fixture->connection = smb::core::Connection::createEmpty();
  fixture->connection.id = QStringLiteral("contract-native");
  fixture->connection.name = QStringLiteral("Native Contract Share");
  fixture->connection.server = server;
  fixture->connection.share = share;
  fixture->connection.normalizedUri =
      QStringLiteral("smb://%1/%2").arg(server, share);
  fixture->connection.domain = envString("SMB_BROWSER_NATIVE_CONTRACT_DOMAIN");
  fixture->connection.username = envString("SMB_BROWSER_NATIVE_CONTRACT_USER");

  const auto auth =
      envString("SMB_BROWSER_NATIVE_CONTRACT_AUTH").toLower();
  if (auth == QStringLiteral("anonymous")) {
    fixture->connection.authType = smb::core::AuthType::Anonymous;
  } else if (auth == QStringLiteral("guest")) {
    fixture->connection.authType = smb::core::AuthType::Guest;
  } else {
    fixture->connection.authType = smb::core::AuthType::Password;
    const auto password = qgetenv("SMB_BROWSER_NATIVE_CONTRACT_PASSWORD");
    if (password.isEmpty()) {
      *skipReason = QStringLiteral(
          "Password auth requires SMB_BROWSER_NATIVE_CONTRACT_PASSWORD, or "
          "set SMB_BROWSER_NATIVE_CONTRACT_AUTH=guest/anonymous.");
      return false;
    }
    fixture->secret = smb::core::CredentialSecret{password};
    fixture->secretPtr = &fixture->secret;
  }

  return true;
}
#endif

} // namespace

class SmbClientContractTest final : public QObject {
  Q_OBJECT

private slots:
  void fakeBackendPassesMutatingContract() {
    smb::tests::FakeSmbClient client;
    client.addDirectory(QStringLiteral("/"));

    runMutatingContract(client, fakeConnection(), nullptr,
                        QStringLiteral("/contract"));
  }

  void fakeBackendCoversSymlinkListingTimeoutAndCancellation() {
    smb::tests::FakeSmbClient client;
    client.addSymlink(QStringLiteral("/links/current"));

    auto listed = client.listDirectory(fakeConnection(), nullptr,
                                       QStringLiteral("/links"), {});
    verifyOk(listed, "list symlink parent");
    QVERIFY(containsEntry(listed.value(), QStringLiteral("current"),
                          smb::core::RemoteFileType::Symlink));

    client.failOperation(smb::tests::FakeSmbOperation::CheckConnection,
                         smb::core::ErrorCode::Timeout);
    const auto timeout = client.checkConnection(fakeConnection(), nullptr, {});
    QVERIFY(!timeout.ok());
    QCOMPARE(static_cast<int>(timeout.error().code),
             static_cast<int>(smb::core::ErrorCode::Timeout));

    client.clearFailures();
    runCancellationContract(client, fakeConnection(), nullptr);
  }

  void fakeBackendExposesCapabilityReportContract() {
    smb::tests::FakeSmbClient client;
    smb::core::SmbClientCapabilities capabilities;
    capabilities.canReadExtendedAttributes = true;
    capabilities.canBrowseShares = false;
    client.setCapabilities(capabilities);

    const auto report =
        client.probeCapabilities(fakeConnection(), nullptr, {});

    verifyOk(report, "probeCapabilities");
    QVERIFY(report.value().capabilities.canReadExtendedAttributes);
    QVERIFY(!report.value().capabilities.canBrowseShares);
  }

  void nativeBackendPassesMutatingContractWhenConfigured() {
#ifdef SMB_BROWSER_WITH_NATIVE_SMB
    NativeFixture fixture;
    QString skipReason;
    if (!nativeFixtureFromEnv(&fixture, &skipReason)) {
      QSKIP(qPrintable(skipReason));
    }
    smb::infrastructure::NativeSmbClient client(10);
    const auto rootPath =
        QStringLiteral("/smb-browser-contract-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    runMutatingContract(client, fixture.connection, fixture.secretPtr,
                        rootPath);
    runCancellationContract(client, fixture.connection, fixture.secretPtr);
#else
    QSKIP("Native SMB backend is disabled.");
#endif
  }

  void nativeBackendCoversOptionalSymlinkAndDfsFixturesWhenConfigured() {
#ifdef SMB_BROWSER_WITH_NATIVE_SMB
    NativeFixture fixture;
    QString skipReason;
    if (!nativeFixtureFromEnv(&fixture, &skipReason)) {
      QSKIP(qPrintable(skipReason));
    }
    smb::infrastructure::NativeSmbClient client(10);

    const auto symlinkParent =
        envString("SMB_BROWSER_NATIVE_CONTRACT_SYMLINK_PARENT");
    const auto symlinkName =
        envString("SMB_BROWSER_NATIVE_CONTRACT_SYMLINK_NAME");
    if (!symlinkParent.isEmpty() && !symlinkName.isEmpty()) {
      auto listed =
          client.listDirectory(fixture.connection, fixture.secretPtr,
                               symlinkParent, {});
      verifyOk(listed, "list native symlink parent");
      QVERIFY(containsEntry(listed.value(), symlinkName,
                            smb::core::RemoteFileType::Symlink));
    }

    const auto dfsPath = envString("SMB_BROWSER_NATIVE_CONTRACT_DFS_PATH");
    if (!dfsPath.isEmpty()) {
      auto listed =
          client.listDirectory(fixture.connection, fixture.secretPtr, dfsPath,
                               {});
      verifyOk(listed, "list native DFS path");
    }

    if (symlinkParent.isEmpty() && dfsPath.isEmpty()) {
      QSKIP("Set SMB_BROWSER_NATIVE_CONTRACT_SYMLINK_* or "
            "SMB_BROWSER_NATIVE_CONTRACT_DFS_PATH for optional native "
            "symlink/DFS contract coverage.");
    }
#else
    QSKIP("Native SMB backend is disabled.");
#endif
  }
};

QTEST_MAIN(SmbClientContractTest)

#include "test_smb_client_contract.moc"
