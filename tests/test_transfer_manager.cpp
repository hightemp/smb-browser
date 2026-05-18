#include "application/TransferManager.h"
#include "fakes/FakeSmbClient.h"

#include <QFile>
#include <QTemporaryDir>
#include <QThread>
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

smb::core::Connection archiveConnection() {
  auto value = connection();
  value.name = QStringLiteral("Archive Share");
  value.normalizedUri = QStringLiteral("smb://server/archive");
  value.share = QStringLiteral("archive");
  return value;
}

class SlowDownloadSmbClient final : public smb::core::SmbClient {
public:
  smb::core::Result<bool>
  checkConnection(const smb::core::Connection &,
                  const smb::core::CredentialSecret *,
                  const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<QVector<smb::core::RemoteFileEntry>>
  listDirectory(const smb::core::Connection &,
                const smb::core::CredentialSecret *, const QString &,
                const smb::core::OperationContext &) override {
    return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::success({});
  }

  smb::core::Result<bool>
  createDirectory(const smb::core::Connection &,
                  const smb::core::CredentialSecret *, const QString &,
                  const smb::core::OperationContext &) override {
    return unsupported();
  }

  smb::core::Result<bool> remove(const smb::core::Connection &,
                                 const smb::core::CredentialSecret *,
                                 const QString &,
                                 const smb::core::OperationContext &) override {
    return unsupported();
  }

  smb::core::Result<bool> rename(const smb::core::Connection &,
                                 const smb::core::CredentialSecret *,
                                 const QString &, const QString &,
                                 const smb::core::OperationContext &) override {
    return unsupported();
  }

  smb::core::Result<bool>
  downloadFile(const smb::core::Connection &,
               const smb::core::CredentialSecret *, const QString &,
               const QString &,
               const smb::core::OperationContext &context) override {
    while (context.cancellationToken != nullptr &&
           !context.cancellationToken->isCancellationRequested()) {
      QThread::msleep(5);
    }
    return smb::core::Result<bool>::failure(
        smb::core::AppError::fromCode(smb::core::ErrorCode::OperationCancelled,
                                      smb::core::ErrorCategory::Transfer,
                                      QStringLiteral("Download cancelled.")));
  }

  smb::core::Result<bool>
  uploadFile(const smb::core::Connection &, const smb::core::CredentialSecret *,
             const QString &, const QString &,
             const smb::core::OperationContext &) override {
    return unsupported();
  }

  smb::core::Result<bool> copy(const smb::core::Connection &,
                               const smb::core::CredentialSecret *,
                               const QString &, const smb::core::Connection &,
                               const smb::core::CredentialSecret *,
                               const QString &,
                               const smb::core::OperationContext &) override {
    return unsupported();
  }

  smb::core::Result<bool> move(const smb::core::Connection &,
                               const smb::core::CredentialSecret *,
                               const QString &, const smb::core::Connection &,
                               const smb::core::CredentialSecret *,
                               const QString &,
                               const smb::core::OperationContext &) override {
    return unsupported();
  }

private:
  static smb::core::Result<bool> unsupported() {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::Unknown, smb::core::ErrorCategory::Smb,
        QStringLiteral("Unsupported.")));
  }
};

} // namespace

class TransferManagerTest final : public QObject {
  Q_OBJECT

private slots:
  void downloadAndUploadUseOperationQueue() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::tests::FakeSmbClient smbClient;
    smbClient.addFile(QStringLiteral("/remote.txt"), QByteArrayLiteral("data"));

    smb::application::OperationQueue queue(1);
    smb::application::TransferManager manager(queue, smbClient);

    const auto downloadedPath = tempDir.filePath(QStringLiteral("local.txt"));
    const auto downloadId =
        manager.downloadFile(connection(), std::nullopt,
                             QStringLiteral("/remote.txt"), downloadedPath);
    QTRY_VERIFY(queue.snapshot(downloadId).state ==
                smb::application::OperationState::Completed);

    QFile downloaded(downloadedPath);
    QVERIFY(downloaded.open(QIODevice::ReadOnly));
    QCOMPARE(downloaded.readAll(), QByteArrayLiteral("data"));

    const auto uploadSource = tempDir.filePath(QStringLiteral("upload.txt"));
    {
      QFile file(uploadSource);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("uploaded");
    }

    const auto uploadId =
        manager.uploadFile(connection(), std::nullopt, uploadSource,
                           QStringLiteral("/upload.txt"));
    QTRY_VERIFY(queue.snapshot(uploadId).state ==
                smb::application::OperationState::Completed);

    const auto verifyPath = tempDir.filePath(QStringLiteral("verify.txt"));
    QVERIFY(smbClient
                .downloadFile(connection(), nullptr,
                              QStringLiteral("/upload.txt"), verifyPath, {})
                .ok());
    QFile uploaded(verifyPath);
    QVERIFY(uploaded.open(QIODevice::ReadOnly));
    QCOMPARE(uploaded.readAll(), QByteArrayLiteral("uploaded"));
  }

  void copyAndMoveDelegateToBackend() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::tests::FakeSmbClient smbClient;
    smbClient.addFile(QStringLiteral("/source.txt"), QByteArrayLiteral("copy"));

    smb::application::OperationQueue queue(1);
    smb::application::TransferManager manager(queue, smbClient);

    const auto copyId =
        manager.copy(connection(), std::nullopt, QStringLiteral("/source.txt"),
                     connection(), std::nullopt, QStringLiteral("/copy.txt"));
    QTRY_VERIFY(queue.snapshot(copyId).state ==
                smb::application::OperationState::Completed);

    const auto moveId =
        manager.move(connection(), std::nullopt, QStringLiteral("/copy.txt"),
                     connection(), std::nullopt, QStringLiteral("/moved.txt"));
    QTRY_VERIFY(queue.snapshot(moveId).state ==
                smb::application::OperationState::Completed);

    const auto target = tempDir.filePath(QStringLiteral("moved.txt"));
    QVERIFY(smbClient
                .downloadFile(connection(), nullptr,
                              QStringLiteral("/moved.txt"), target, {})
                .ok());
  }

  void crossShareMoveDoesNotDeleteSourceWhenCopyFails() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::tests::FakeSmbClient smbClient;
    smbClient.addFile(QStringLiteral("/source.txt"), QByteArrayLiteral("copy"));
    smbClient.failOperation(smb::tests::FakeSmbOperation::Copy,
                            smb::core::ErrorCode::PermissionDenied);

    smb::application::OperationQueue queue(1);
    smb::application::TransferManager manager(queue, smbClient);

    const auto moveId = manager.move(
        connection(), std::nullopt, QStringLiteral("/source.txt"),
        archiveConnection(), std::nullopt, QStringLiteral("/moved.txt"));
    QTRY_VERIFY(queue.snapshot(moveId).state ==
                smb::application::OperationState::Failed);
    QVERIFY(queue.snapshot(moveId).error.code ==
            smb::core::ErrorCode::PermissionDenied);

    const auto source = tempDir.filePath(QStringLiteral("source.txt"));
    QVERIFY(smbClient
                .downloadFile(connection(), nullptr,
                              QStringLiteral("/source.txt"), source, {})
                .ok());
    QFile sourceFile(source);
    QVERIFY(sourceFile.open(QIODevice::ReadOnly));
    QCOMPARE(sourceFile.readAll(), QByteArrayLiteral("copy"));

    const auto target = tempDir.filePath(QStringLiteral("target.txt"));
    QVERIFY(!smbClient
                 .downloadFile(archiveConnection(), nullptr,
                               QStringLiteral("/moved.txt"), target, {})
                 .ok());
  }

  void cancellationIsForwardedThroughOperationQueue() {
    SlowDownloadSmbClient smbClient;
    smb::application::OperationQueue queue(1);
    smb::application::TransferManager manager(queue, smbClient);

    const auto id =
        manager.downloadFile(connection(), std::nullopt,
                             QStringLiteral("/slow.txt"), QStringLiteral("x"));

    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Running);
    QVERIFY(queue.cancel(id));
    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Cancelled);
  }
};

QTEST_MAIN(TransferManagerTest)

#include "test_transfer_manager.moc"
