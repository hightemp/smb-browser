#include "smb/Libsmb2SmbClient.h"

#include <QByteArray>
#include <QFile>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest/QtTest>

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

smb::core::CredentialSecret sambaSecret() {
  return smb::core::CredentialSecret{
      envValue(QStringLiteral("SMB_BROWSER_SAMBA_PASSWORD"),
               QStringLiteral("synthetic-password"))
          .toUtf8()};
}

} // namespace

class DockerSambaIntegrationTest final : public QObject {
  Q_OBJECT

private slots:
  void connectListUploadDownloadRenameAndDelete() {
    smb::infrastructure::Libsmb2SmbClient client(5);
    const auto connection = sambaConnection();
    const auto secret = sambaSecret();

    const auto checked = client.checkConnection(connection, &secret, {});
    QVERIFY2(checked.ok(),
             qPrintable(checked.error().sanitizedTechnicalDetails));

    const auto root = client.listDirectory(connection, &secret,
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
        client.uploadFile(connection, &secret, localUpload, remoteFile, {});
    QVERIFY2(uploaded.ok(),
             qPrintable(uploaded.error().sanitizedTechnicalDetails));

    const auto renamed =
        client.rename(connection, &secret, remoteFile, renamedFile, {});
    QVERIFY2(renamed.ok(),
             qPrintable(renamed.error().sanitizedTechnicalDetails));

    const auto downloadedResult =
        client.downloadFile(connection, &secret, renamedFile, downloaded, {});
    QVERIFY2(downloadedResult.ok(),
             qPrintable(downloadedResult.error().sanitizedTechnicalDetails));

    QFile downloadedFile(downloaded);
    QVERIFY(downloadedFile.open(QIODevice::ReadOnly));
    QCOMPARE(downloadedFile.readAll(),
             QByteArrayLiteral("synthetic docker samba content"));

    const auto removed = client.remove(connection, &secret, renamedFile, {});
    QVERIFY2(removed.ok(), qPrintable(removed.error().sanitizedTechnicalDetails));
  }
};

QTEST_MAIN(DockerSambaIntegrationTest)

#include "test_docker_samba_integration.moc"
