#include "fakes/FakeSmbClient.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

smb::core::Connection connection() {
  auto value = smb::core::Connection::createEmpty();
  value.name = QStringLiteral("Test Share");
  value.normalizedUri = QStringLiteral("smb://server/share");
  value.server = QStringLiteral("server");
  value.share = QStringLiteral("share");
  return value;
}

} // namespace

class FakeSmbClientTest final : public QObject {
  Q_OBJECT

private slots:
  void checkConnectionValidatesCredentials() {
    smb::tests::FakeSmbClient client;
    client.setRequirePassword(true);
    client.setExpectedSecret(QByteArrayLiteral("expected-secret"));

    const smb::core::CredentialSecret wrongSecret{
        QByteArrayLiteral("wrong-secret")};
    const auto wrong = client.checkConnection(connection(), &wrongSecret, {});
    QVERIFY(!wrong.ok());
    QVERIFY(wrong.error().code == smb::core::ErrorCode::AuthenticationFailed);

    const smb::core::CredentialSecret expectedSecret{
        QByteArrayLiteral("expected-secret")};
    const auto correct =
        client.checkConnection(connection(), &expectedSecret, {});
    QVERIFY(correct.ok());
    QVERIFY(correct.value());
  }

  void listDirectoryReturnsEntries() {
    smb::tests::FakeSmbClient client;
    client.addDirectory(QStringLiteral("/docs"));
    client.addFile(QStringLiteral("/docs/readme.txt"),
                   QByteArrayLiteral("hello"));

    const auto listed = client.listDirectory(connection(), nullptr,
                                             QStringLiteral("/docs"), {});
    QVERIFY(listed.ok());
    QCOMPARE(listed.value().size(), 1);
    QCOMPARE(listed.value().at(0).name, QStringLiteral("readme.txt"));
    QVERIFY(listed.value().at(0).type == smb::core::RemoteFileType::File);
  }

  void createDeleteAndRename() {
    smb::tests::FakeSmbClient client;
    client.addDirectory(QStringLiteral("/"));

    QVERIFY(
        client
            .createDirectory(connection(), nullptr, QStringLiteral("/new"), {})
            .ok());
    QVERIFY(client
                .rename(connection(), nullptr, QStringLiteral("/new"),
                        QStringLiteral("/renamed"), {})
                .ok());

    const auto oldPath =
        client.listDirectory(connection(), nullptr, QStringLiteral("/new"), {});
    QVERIFY(!oldPath.ok());

    QVERIFY(client.remove(connection(), nullptr, QStringLiteral("/renamed"), {})
                .ok());
  }

  void uploadAndDownloadWithProgress() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto localSource = tempDir.filePath(QStringLiteral("source.txt"));
    {
      QFile file(localSource);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("payload");
    }

    smb::tests::FakeSmbClient client;
    client.addDirectory(QStringLiteral("/docs"));

    bool uploadProgress = false;
    smb::core::OperationContext uploadContext;
    uploadContext.progressCallback =
        [&uploadProgress](const smb::core::TransferProgress &progress) {
          uploadProgress = true;
          QCOMPARE(progress.bytesTransferred, progress.totalBytes);
        };
    QVERIFY(client
                .uploadFile(connection(), nullptr, localSource,
                            QStringLiteral("/docs/remote.txt"), uploadContext)
                .ok());
    QVERIFY(uploadProgress);

    bool downloadProgress = false;
    smb::core::OperationContext downloadContext;
    downloadContext.progressCallback =
        [&downloadProgress](const smb::core::TransferProgress &progress) {
          downloadProgress = true;
          QCOMPARE(progress.bytesTransferred, progress.totalBytes);
        };

    const auto localTarget = tempDir.filePath(QStringLiteral("target.txt"));
    QVERIFY(client
                .downloadFile(connection(), nullptr,
                              QStringLiteral("/docs/remote.txt"), localTarget,
                              downloadContext)
                .ok());
    QVERIFY(downloadProgress);

    QFile downloaded(localTarget);
    QVERIFY(downloaded.open(QIODevice::ReadOnly));
    QCOMPARE(downloaded.readAll(), QByteArrayLiteral("payload"));
  }

  void copyMoveAndFailureModes() {
    smb::tests::FakeSmbClient client;
    client.addFile(QStringLiteral("/source.txt"), QByteArrayLiteral("content"));

    QVERIFY(client
                .copy(connection(), nullptr, QStringLiteral("/source.txt"),
                      connection(), nullptr, QStringLiteral("/copy.txt"), {})
                .ok());
    QVERIFY(client
                .move(connection(), nullptr, QStringLiteral("/copy.txt"),
                      connection(), nullptr, QStringLiteral("/moved.txt"), {})
                .ok());

    client.failOperation(smb::tests::FakeSmbOperation::ListDirectory,
                         smb::core::ErrorCode::PermissionDenied);
    const auto denied =
        client.listDirectory(connection(), nullptr, QStringLiteral("/"), {});
    QVERIFY(!denied.ok());
    QVERIFY(denied.error().code == smb::core::ErrorCode::PermissionDenied);

    client.clearFailures();
    client.failOperation(smb::tests::FakeSmbOperation::CheckConnection,
                         smb::core::ErrorCode::Timeout);
    const auto timeout = client.checkConnection(connection(), nullptr, {});
    QVERIFY(!timeout.ok());
    QVERIFY(timeout.error().code == smb::core::ErrorCode::Timeout);
  }

  void sameShareMoveUsesRenameSemantics() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::tests::FakeSmbClient client;
    client.addFile(QStringLiteral("/source.txt"), QByteArrayLiteral("content"));
    client.failOperation(smb::tests::FakeSmbOperation::Copy,
                         smb::core::ErrorCode::PermissionDenied);
    client.failOperation(smb::tests::FakeSmbOperation::Remove,
                         smb::core::ErrorCode::PermissionDenied);

    QVERIFY(client
                .move(connection(), nullptr, QStringLiteral("/source.txt"),
                      connection(), nullptr, QStringLiteral("/moved.txt"), {})
                .ok());

    const auto oldPath = tempDir.filePath(QStringLiteral("old.txt"));
    QVERIFY(!client
                 .downloadFile(connection(), nullptr,
                               QStringLiteral("/source.txt"), oldPath, {})
                 .ok());

    const auto movedPath = tempDir.filePath(QStringLiteral("moved.txt"));
    QVERIFY(client
                .downloadFile(connection(), nullptr,
                              QStringLiteral("/moved.txt"), movedPath, {})
                .ok());
    QFile moved(movedPath);
    QVERIFY(moved.open(QIODevice::ReadOnly));
    QCOMPARE(moved.readAll(), QByteArrayLiteral("content"));
  }

  void cancellationIsReported() {
    smb::tests::FakeSmbClient client;
    smb::core::CancellationToken token;
    token.cancel();

    smb::core::OperationContext context;
    context.cancellationToken = &token;

    const auto result = client.listDirectory(connection(), nullptr,
                                             QStringLiteral("/"), context);
    QVERIFY(!result.ok());
    QVERIFY(result.error().code == smb::core::ErrorCode::OperationCancelled);
  }
};

QTEST_MAIN(FakeSmbClientTest)

#include "test_fake_smb_client.moc"
