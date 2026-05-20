#include "core/SmbClient.h"

#ifdef SMB_BROWSER_WITH_NATIVE_SMB
#include "smb/NativeSmbClient.h"
#elif defined(SMB_BROWSER_WITH_LIBSMB2)
#include "smb/Libsmb2SmbClient.h"
#endif

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest/QtTest>
#include <memory>

namespace {

QString envValue(const QString &name, const QString &fallback) {
  const auto env = QProcessEnvironment::systemEnvironment();
  return env.value(name, fallback);
}

smb::core::Connection sambaConnection() {
  auto connection = smb::core::Connection::createEmpty();
  connection.id = QStringLiteral("docker-samba");
  connection.name = QStringLiteral("Docker Samba");
  connection.server = envValue(QStringLiteral("SMB_BROWSER_SAMBA_SERVER"),
                               QStringLiteral("127.0.0.1:1445"));
  connection.share = envValue(QStringLiteral("SMB_BROWSER_SAMBA_SHARE"),
                              QStringLiteral("public"));
  connection.normalizedUri =
      QStringLiteral("smb://%1/%2").arg(connection.server, connection.share);
  connection.username = envValue(QStringLiteral("SMB_BROWSER_SAMBA_USER"),
                                 QStringLiteral("smbtest"));
  connection.authType = smb::core::AuthType::Password;
  return connection;
}

smb::core::Connection sambaConnectionForShare(const QString &share) {
  auto connection = sambaConnection();
  connection.share = share;
  connection.normalizedUri =
      QStringLiteral("smb://%1/%2").arg(connection.server, connection.share);
  return connection;
}

smb::core::Connection guestConnection() {
  auto connection = sambaConnectionForShare(QStringLiteral("guest"));
  connection.id = QStringLiteral("docker-samba-guest");
  connection.name = QStringLiteral("Docker Samba Guest");
  connection.username.clear();
  connection.authType = smb::core::AuthType::Guest;
  return connection;
}

smb::core::CredentialSecret sambaSecret() {
  return smb::core::CredentialSecret{
      envValue(QStringLiteral("SMB_BROWSER_SAMBA_PASSWORD"),
               QStringLiteral("synthetic-password"))
          .toUtf8()};
}

std::unique_ptr<smb::core::SmbClient> createClient() {
#ifdef SMB_BROWSER_WITH_NATIVE_SMB
  return std::make_unique<smb::infrastructure::NativeSmbClient>(5);
#elif defined(SMB_BROWSER_WITH_LIBSMB2)
  return std::make_unique<smb::infrastructure::Libsmb2SmbClient>(5);
#else
  return {};
#endif
}

bool nativeWireOptIn() {
#ifdef SMB_BROWSER_WITH_NATIVE_SMB
  return envValue(QStringLiteral("SMB_BROWSER_DOCKER_SAMBA_NATIVE_WIRE"),
                  QStringLiteral("0")) == QStringLiteral("1");
#else
  return true;
#endif
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

bool containsShare(const QVector<smb::core::SmbShareInfo> &shares,
                   const QString &name) {
  for (const auto &share : shares) {
    if (share.name == name) {
      return true;
    }
  }
  return false;
}

QByteArray deterministicBytes(int size) {
  QByteArray bytes;
  bytes.resize(size);
  for (int index = 0; index < size; ++index) {
    bytes[index] = static_cast<char>((index * 31 + 17) & 0xFF);
  }
  return bytes;
}

void writeFile(const QString &path, const QByteArray &data) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QCOMPARE(file.write(data), static_cast<qint64>(data.size()));
}

QByteArray readFile(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

bool progressIsMonotonic(const QVector<smb::core::TransferProgress> &events) {
  qint64 previous = -1;
  for (const auto &event : events) {
    if (event.bytesTransferred < previous ||
        event.bytesTransferred > event.totalBytes) {
      return false;
    }
    previous = event.bytesTransferred;
  }
  return true;
}

} // namespace

class DockerSambaIntegrationTest final : public QObject {
  Q_OBJECT

private slots:
  void fixtureProvidesPasswordGuestNestedLargeAndMetadataEntries() {
    if (!nativeWireOptIn()) {
      QSKIP("Native Docker Samba wire validation is opt-in. Set "
            "SMB_BROWSER_DOCKER_SAMBA_NATIVE_WIRE=1 to run it explicitly.");
    }
    const auto client = createClient();
    QVERIFY(client != nullptr);
    const auto connection = sambaConnection();
    const auto secret = sambaSecret();

    const auto root = client->listDirectory(connection, &secret,
                                           QStringLiteral("/"), {});
    QVERIFY2(root.ok(), qPrintable(root.error().sanitizedTechnicalDetails));
    QVERIFY(containsEntry(root.value(), QStringLiteral("root.txt"),
                          smb::core::RemoteFileType::File));
    QVERIFY(containsEntry(root.value(), QStringLiteral("nested"),
                          smb::core::RemoteFileType::Directory));
    QVERIFY(containsEntry(root.value(), QStringLiteral("large.bin"),
                          smb::core::RemoteFileType::File));
    QVERIFY(containsEntry(root.value(), QStringLiteral("metadata.txt"),
                          smb::core::RemoteFileType::File));

    const auto nested = client->listDirectory(connection, &secret,
                                             QStringLiteral("/nested"), {});
    QVERIFY2(nested.ok(),
             qPrintable(nested.error().sanitizedTechnicalDetails));
    QVERIFY(containsEntry(nested.value(), QStringLiteral("readme.txt"),
                          smb::core::RemoteFileType::File));

    const auto archive = client->listDirectory(
        sambaConnectionForShare(QStringLiteral("archive")), &secret,
        QStringLiteral("/"), {});
    QVERIFY2(archive.ok(),
             qPrintable(archive.error().sanitizedTechnicalDetails));
    QVERIFY(containsEntry(archive.value(), QStringLiteral("archive.txt"),
                          smb::core::RemoteFileType::File));

    const auto guest = client->listDirectory(guestConnection(), nullptr,
                                            QStringLiteral("/"), {});
    QVERIFY2(guest.ok(), qPrintable(guest.error().sanitizedTechnicalDetails));
    QVERIFY(containsEntry(guest.value(), QStringLiteral("guest.txt"),
                          smb::core::RemoteFileType::File));
  }

  void shareBrowsingListsConfiguredShares() {
    if (!nativeWireOptIn()) {
      QSKIP("Native Docker Samba wire validation is opt-in. Set "
            "SMB_BROWSER_DOCKER_SAMBA_NATIVE_WIRE=1 to run it explicitly.");
    }
    const auto client = createClient();
    QVERIFY(client != nullptr);
    const auto connection = sambaConnection();
    const auto secret = sambaSecret();

    const auto shares = client->listShares(connection, &secret, {});
    QVERIFY2(shares.ok(), qPrintable(shares.error().sanitizedTechnicalDetails));
    QVERIFY(containsShare(shares.value(), QStringLiteral("public")));
    QVERIFY(containsShare(shares.value(), QStringLiteral("archive")));
    QVERIFY(containsShare(shares.value(), QStringLiteral("guest")));
    QVERIFY(containsShare(shares.value(), QStringLiteral("encrypted")));

    const auto report = client->probeCapabilities(connection, &secret, {});
    QVERIFY2(report.ok(), qPrintable(report.error().sanitizedTechnicalDetails));
    QVERIFY(report.value().capabilities.canBrowseShares);
  }

  void encryptedShareListsThroughNativeTransform() {
    if (!nativeWireOptIn()) {
      QSKIP("Native Docker Samba wire validation is opt-in. Set "
            "SMB_BROWSER_DOCKER_SAMBA_NATIVE_WIRE=1 to run it explicitly.");
    }
    const auto client = createClient();
    QVERIFY(client != nullptr);
    const auto connection = sambaConnectionForShare(QStringLiteral("encrypted"));
    const auto secret = sambaSecret();

    const auto entries = client->listDirectory(connection, &secret,
                                              QStringLiteral("/"), {});
    QVERIFY2(entries.ok(), qPrintable(entries.error().sanitizedTechnicalDetails));
    QVERIFY(containsEntry(entries.value(), QStringLiteral("encrypted.txt"),
                          smb::core::RemoteFileType::File));

    const auto report = client->probeCapabilities(connection, &secret, {});
    QVERIFY2(report.ok(), qPrintable(report.error().sanitizedTechnicalDetails));
    QVERIFY(report.value().encryptionRequired);
  }

  void connectListUploadDownloadRenameAndDelete() {
    if (!nativeWireOptIn()) {
      QSKIP("Native Docker Samba wire validation is opt-in. Set "
            "SMB_BROWSER_DOCKER_SAMBA_NATIVE_WIRE=1 to run it explicitly.");
    }
    const auto client = createClient();
    QVERIFY(client != nullptr);
    const auto connection = sambaConnection();
    const auto secret = sambaSecret();

    const auto checked = client->checkConnection(connection, &secret, {});
    QVERIFY2(checked.ok(),
             qPrintable(checked.error().sanitizedTechnicalDetails));

    const auto root = client->listDirectory(connection, &secret,
                                           QStringLiteral("/"), {});
    QVERIFY2(root.ok(), qPrintable(root.error().sanitizedTechnicalDetails));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto localUpload = tempDir.filePath(QStringLiteral("upload.txt"));
    QFile uploadFile(localUpload);
    QVERIFY(uploadFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    uploadFile.write("synthetic docker samba content");
    uploadFile.close();

    const auto suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto remoteFile =
        QStringLiteral("/codex-upload-%1.txt").arg(suffix);
    const auto renamedFile =
        QStringLiteral("/codex-renamed-%1.txt").arg(suffix);
    const auto downloaded = tempDir.filePath(QStringLiteral("download.txt"));

    const auto uploaded =
        client->uploadFile(connection, &secret, localUpload, remoteFile, {});
    QVERIFY2(uploaded.ok(),
             qPrintable(uploaded.error().sanitizedTechnicalDetails));

    const auto renamed =
        client->rename(connection, &secret, remoteFile, renamedFile, {});
    QVERIFY2(renamed.ok(),
             qPrintable(renamed.error().sanitizedTechnicalDetails));

    const auto downloadedResult =
        client->downloadFile(connection, &secret, renamedFile, downloaded, {});
    QVERIFY2(downloadedResult.ok(),
             qPrintable(downloadedResult.error().sanitizedTechnicalDetails));

    QFile downloadedFile(downloaded);
    QVERIFY(downloadedFile.open(QIODevice::ReadOnly));
    QCOMPARE(downloadedFile.readAll(),
             QByteArrayLiteral("synthetic docker samba content"));

    const auto removed = client->remove(connection, &secret, renamedFile, {});
    QVERIFY2(removed.ok(), qPrintable(removed.error().sanitizedTechnicalDetails));
  }

  void largeFileTransfersReportProgressAndOverwrite() {
    if (!nativeWireOptIn()) {
      QSKIP("Native Docker Samba wire validation is opt-in. Set "
            "SMB_BROWSER_DOCKER_SAMBA_NATIVE_WIRE=1 to run it explicitly.");
    }
    const auto client = createClient();
    QVERIFY(client != nullptr);
    const auto connection = sambaConnection();
    const auto secret = sambaSecret();

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QVector<smb::core::TransferProgress> fixtureDownloadProgress;
    smb::core::OperationContext fixtureDownloadContext;
    fixtureDownloadContext.progressCallback =
        [&fixtureDownloadProgress](const smb::core::TransferProgress &event) {
          fixtureDownloadProgress.push_back(event);
        };
    const auto fixtureDownload = tempDir.filePath(QStringLiteral("large.bin"));
    const auto fixtureResult = client->downloadFile(
        connection, &secret, QStringLiteral("/large.bin"), fixtureDownload,
        fixtureDownloadContext);
    QVERIFY2(fixtureResult.ok(),
             qPrintable(fixtureResult.error().sanitizedTechnicalDetails));
    QCOMPARE(QFileInfo(fixtureDownload).size(), qint64(2 * 1024 * 1024));
    QVERIFY(!fixtureDownloadProgress.isEmpty());
    QVERIFY(progressIsMonotonic(fixtureDownloadProgress));
    QCOMPARE(fixtureDownloadProgress.last().bytesTransferred,
             fixtureDownloadProgress.last().totalBytes);

    const auto suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto remoteFile =
        QStringLiteral("/codex-large-%1.bin").arg(suffix);
    const auto uploadPath = tempDir.filePath(QStringLiteral("upload-large.bin"));
    const auto firstPayload = deterministicBytes(2 * 1024 * 1024 + 123);
    writeFile(uploadPath, firstPayload);

    QVector<smb::core::TransferProgress> uploadProgress;
    smb::core::OperationContext uploadContext;
    uploadContext.progressCallback =
        [&uploadProgress](const smb::core::TransferProgress &event) {
          uploadProgress.push_back(event);
        };
    const auto uploaded =
        client->uploadFile(connection, &secret, uploadPath, remoteFile,
                           uploadContext);
    QVERIFY2(uploaded.ok(),
             qPrintable(uploaded.error().sanitizedTechnicalDetails));
    QVERIFY(!uploadProgress.isEmpty());
    QVERIFY(progressIsMonotonic(uploadProgress));
    QCOMPARE(uploadProgress.last().bytesTransferred,
             static_cast<qint64>(firstPayload.size()));

    const auto secondPayload = deterministicBytes(64 * 1024 + 7);
    writeFile(uploadPath, secondPayload);
    const auto overwritten =
        client->uploadFile(connection, &secret, uploadPath, remoteFile, {});
    QVERIFY2(overwritten.ok(),
             qPrintable(overwritten.error().sanitizedTechnicalDetails));

    const auto verifyPath = tempDir.filePath(QStringLiteral("verify-large.bin"));
    const auto downloaded =
        client->downloadFile(connection, &secret, remoteFile, verifyPath, {});
    QVERIFY2(downloaded.ok(),
             qPrintable(downloaded.error().sanitizedTechnicalDetails));
    QCOMPARE(readFile(verifyPath), secondPayload);

    const auto removed = client->remove(connection, &secret, remoteFile, {});
    QVERIFY2(removed.ok(), qPrintable(removed.error().sanitizedTechnicalDetails));
  }

  void crossShareCopyUsesSyntheticFixtures() {
    if (!nativeWireOptIn()) {
      QSKIP("Native Docker Samba wire validation is opt-in. Set "
            "SMB_BROWSER_DOCKER_SAMBA_NATIVE_WIRE=1 to run it explicitly.");
    }
    const auto client = createClient();
    QVERIFY(client != nullptr);
    const auto publicConnection = sambaConnection();
    const auto archiveConnection =
        sambaConnectionForShare(QStringLiteral("archive"));
    const auto secret = sambaSecret();
    const auto suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto sameShareTarget =
        QStringLiteral("/same-share-copied-%1.txt").arg(suffix);
    const auto target = QStringLiteral("/copied-%1.txt").arg(suffix);

    const auto sameShareCopied =
        client->copy(publicConnection, &secret, QStringLiteral("/root.txt"),
                     publicConnection, &secret, sameShareTarget, {});
    QVERIFY2(sameShareCopied.ok(),
             qPrintable(sameShareCopied.error().sanitizedTechnicalDetails));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto sameShareDownloaded =
        tempDir.filePath(QStringLiteral("same-share-copied.txt"));
    const auto sameShareDownloadedResult = client->downloadFile(
        publicConnection, &secret, sameShareTarget, sameShareDownloaded, {});
    QVERIFY2(sameShareDownloadedResult.ok(),
             qPrintable(
                 sameShareDownloadedResult.error().sanitizedTechnicalDetails));
    QCOMPARE(readFile(sameShareDownloaded), QByteArrayLiteral("root fixture\n"));

    const auto copied =
        client->copy(publicConnection, &secret, QStringLiteral("/root.txt"),
                     archiveConnection, &secret, target, {});
    QVERIFY2(copied.ok(), qPrintable(copied.error().sanitizedTechnicalDetails));

    const auto downloaded = tempDir.filePath(QStringLiteral("copied.txt"));
    const auto downloadedResult =
        client->downloadFile(archiveConnection, &secret, target, downloaded,
                             {});
    QVERIFY2(downloadedResult.ok(),
             qPrintable(downloadedResult.error().sanitizedTechnicalDetails));

    QFile downloadedFile(downloaded);
    QVERIFY(downloadedFile.open(QIODevice::ReadOnly));
    QCOMPARE(downloadedFile.readAll(), QByteArrayLiteral("root fixture\n"));

    const auto sameShareRemoved =
        client->remove(publicConnection, &secret, sameShareTarget, {});
    QVERIFY2(sameShareRemoved.ok(),
             qPrintable(sameShareRemoved.error().sanitizedTechnicalDetails));
    const auto removed = client->remove(archiveConnection, &secret, target, {});
    QVERIFY2(removed.ok(), qPrintable(removed.error().sanitizedTechnicalDetails));
  }
};

QTEST_MAIN(DockerSambaIntegrationTest)

#include "test_docker_samba_integration.moc"
