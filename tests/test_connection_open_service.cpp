#include "application/ConnectionOpenService.h"
#include "fakes/FakeSmbClient.h"
#include "storage/ConnectionRepository.h"
#include "storage/SqliteStorage.h"

#include <QFile>
#include <QHash>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>
#include <utility>

namespace {

class FakeCredentialStore final : public smb::core::CredentialStore {
public:
  smb::core::Result<QString>
  save(const QString &, const smb::core::CredentialSecret &) override {
    return smb::core::Result<QString>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::Unknown, smb::core::ErrorCategory::Credentials,
        QStringLiteral("Not used.")));
  }

  smb::core::Result<smb::core::CredentialSecret>
  load(const QString &credentialRef) const override {
    if (!values.contains(credentialRef)) {
      return smb::core::Result<smb::core::CredentialSecret>::failure(
          smb::core::AppError::fromCode(
              smb::core::ErrorCode::CredentialNotFound,
              smb::core::ErrorCategory::Credentials,
              QStringLiteral("Credential was not found.")));
    }

    return smb::core::Result<smb::core::CredentialSecret>::success(
        smb::core::CredentialSecret{values.value(credentialRef)});
  }

  smb::core::Result<bool> update(const QString &,
                                 const smb::core::CredentialSecret &) override {
    return smb::core::Result<bool>::success(false);
  }

  smb::core::Result<bool> remove(const QString &) override {
    return smb::core::Result<bool>::success(false);
  }

  smb::core::Result<bool> isAvailable() const override {
    return smb::core::Result<bool>::success(true);
  }

  QHash<QString, QByteArray> values;
};

smb::core::Connection connection(QString id = QStringLiteral("conn-1")) {
  auto value = smb::core::Connection::createEmpty();
  value.id = std::move(id);
  value.name = QStringLiteral("Engineering Share");
  value.normalizedUri = QStringLiteral("smb://server/share");
  value.server = QStringLiteral("server");
  value.share = QStringLiteral("share");
  value.initialRemotePath = QStringLiteral("reports");
  value.username = QStringLiteral("user");
  value.authType = smb::core::AuthType::Password;
  value.credentialRef = QStringLiteral("credential-ref");
  return value;
}

struct Fixture {
  QTemporaryDir tempDir;
  smb::infrastructure::SqliteStorage storage;
  std::unique_ptr<smb::infrastructure::ConnectionRepository> repository;
};

std::unique_ptr<Fixture> createFixture() {
  auto fixture = std::make_unique<Fixture>();
  if (!fixture->tempDir.isValid()) {
    return fixture;
  }

  const auto opened = fixture->storage.open(
      fixture->tempDir.filePath(QStringLiteral("app.db")));
  if (opened.hasError()) {
    return fixture;
  }
  const auto migrated = fixture->storage.migrate();
  if (migrated.hasError()) {
    return fixture;
  }

  fixture->repository =
      std::make_unique<smb::infrastructure::ConnectionRepository>(
          fixture->storage.database());
  return fixture;
}

} // namespace

class ConnectionOpenServiceTest final : public QObject {
  Q_OBJECT

private slots:
  void successfulOpenListsInitialPathAndUpdatesLastOpened() {
    auto fixture = createFixture();
    QVERIFY(fixture->repository != nullptr);
    QVERIFY(fixture->repository->add(connection()).ok());

    FakeCredentialStore credentialStore;
    credentialStore.values.insert(QStringLiteral("credential-ref"),
                                  QByteArrayLiteral("expected-secret"));

    smb::tests::FakeSmbClient smbClient;
    smbClient.setRequirePassword(true);
    smbClient.setExpectedSecret(QByteArrayLiteral("expected-secret"));
    smbClient.addDirectory(QStringLiteral("/reports/archive"));
    smbClient.addFile(QStringLiteral("/reports/readme.txt"),
                      QByteArrayLiteral("hello"));

    smb::application::ConnectionOpenService service(*fixture->repository,
                                                    credentialStore, smbClient);
    const auto opened = service.open(QStringLiteral("conn-1"));

    QVERIFY2(opened.ok(), qPrintable(opened.error().sanitizedTechnicalDetails));
    QCOMPARE(opened.value().currentRemotePath, QStringLiteral("/reports"));
    QCOMPARE(opened.value().entries.size(), 2);
    QCOMPARE(opened.value().entries.at(0).name, QStringLiteral("archive"));
    QCOMPARE(opened.value().entries.at(1).name, QStringLiteral("readme.txt"));
    QVERIFY(opened.value().connection.lastOpenedAt.isValid());

    const auto loaded = fixture->repository->getById(QStringLiteral("conn-1"));
    QVERIFY(loaded.ok());
    QVERIFY(loaded.value().lastOpenedAt.isValid());
    QVERIFY(loaded.value().lastErrorCode == smb::core::ErrorCode::None);
  }

  void failedOpenStoresSanitizedErrorAndDoesNotUpdateLastOpened() {
    auto fixture = createFixture();
    QVERIFY(fixture->repository != nullptr);
    QVERIFY(fixture->repository->add(connection()).ok());

    FakeCredentialStore credentialStore;
    credentialStore.values.insert(QStringLiteral("credential-ref"),
                                  QByteArrayLiteral("wrong-secret"));

    smb::tests::FakeSmbClient smbClient;
    smbClient.setRequirePassword(true);
    smbClient.setExpectedSecret(QByteArrayLiteral("expected-secret"));

    smb::application::ConnectionOpenService service(*fixture->repository,
                                                    credentialStore, smbClient);
    const auto opened = service.open(QStringLiteral("conn-1"));

    QVERIFY(!opened.ok());
    QVERIFY(opened.error().code == smb::core::ErrorCode::AuthenticationFailed);

    const auto loaded = fixture->repository->getById(QStringLiteral("conn-1"));
    QVERIFY(loaded.ok());
    QVERIFY(!loaded.value().lastOpenedAt.isValid());
    QVERIFY(loaded.value().lastErrorCode ==
            smb::core::ErrorCode::AuthenticationFailed);
    QVERIFY(!loaded.value().lastErrorMessage.contains(
        QStringLiteral("wrong-secret")));
  }

  void cancellationIsStoredAsTypedError() {
    auto fixture = createFixture();
    QVERIFY(fixture->repository != nullptr);
    QVERIFY(fixture->repository->add(connection()).ok());

    FakeCredentialStore credentialStore;
    credentialStore.values.insert(QStringLiteral("credential-ref"),
                                  QByteArrayLiteral("expected-secret"));

    smb::tests::FakeSmbClient smbClient;
    smbClient.setRequirePassword(true);
    smbClient.setExpectedSecret(QByteArrayLiteral("expected-secret"));
    smbClient.addDirectory(QStringLiteral("/reports"));

    smb::core::CancellationToken token;
    token.cancel();
    smb::core::OperationContext context;
    context.cancellationToken = &token;

    smb::application::ConnectionOpenService service(*fixture->repository,
                                                    credentialStore, smbClient);
    const auto opened = service.open(QStringLiteral("conn-1"), context);

    QVERIFY(!opened.ok());
    QVERIFY(opened.error().code == smb::core::ErrorCode::OperationCancelled);

    const auto loaded = fixture->repository->getById(QStringLiteral("conn-1"));
    QVERIFY(loaded.ok());
    QVERIFY(loaded.value().lastErrorCode ==
            smb::core::ErrorCode::OperationCancelled);
  }

  void fileOperationsDelegateToSmbBackend() {
    auto fixture = createFixture();
    QVERIFY(fixture->repository != nullptr);
    QVERIFY(fixture->repository->add(connection()).ok());

    FakeCredentialStore credentialStore;
    credentialStore.values.insert(QStringLiteral("credential-ref"),
                                  QByteArrayLiteral("expected-secret"));

    smb::tests::FakeSmbClient smbClient;
    smbClient.setRequirePassword(true);
    smbClient.setExpectedSecret(QByteArrayLiteral("expected-secret"));
    smbClient.addDirectory(QStringLiteral("/reports"));

    smb::application::ConnectionOpenService service(*fixture->repository,
                                                    credentialStore, smbClient);

    auto created = service.createDirectory(
        QStringLiteral("conn-1"), QStringLiteral("/reports/new-folder"));
    QVERIFY2(created.ok(),
             qPrintable(created.error().sanitizedTechnicalDetails));

    auto renamed = service.rename(QStringLiteral("conn-1"),
                                  QStringLiteral("/reports/new-folder"),
                                  QStringLiteral("/reports/renamed"));
    QVERIFY2(renamed.ok(),
             qPrintable(renamed.error().sanitizedTechnicalDetails));

    auto listed = service.listDirectory(QStringLiteral("conn-1"),
                                        QStringLiteral("/reports"));
    QVERIFY(listed.ok());
    QCOMPARE(listed.value().entries.size(), 1);
    QCOMPARE(listed.value().entries.first().name, QStringLiteral("renamed"));

    auto removed = service.remove(QStringLiteral("conn-1"),
                                  QStringLiteral("/reports/renamed"));
    QVERIFY2(removed.ok(),
             qPrintable(removed.error().sanitizedTechnicalDetails));

    listed = service.listDirectory(QStringLiteral("conn-1"),
                                   QStringLiteral("/reports"));
    QVERIFY(listed.ok());
    QCOMPARE(listed.value().entries.size(), 0);
  }

  void failedFileOperationStoresLastError() {
    auto fixture = createFixture();
    QVERIFY(fixture->repository != nullptr);
    QVERIFY(fixture->repository->add(connection()).ok());

    FakeCredentialStore credentialStore;
    credentialStore.values.insert(QStringLiteral("credential-ref"),
                                  QByteArrayLiteral("expected-secret"));

    smb::tests::FakeSmbClient smbClient;
    smbClient.setRequirePassword(true);
    smbClient.setExpectedSecret(QByteArrayLiteral("expected-secret"));
    smbClient.failOperation(smb::tests::FakeSmbOperation::Remove,
                            smb::core::ErrorCode::PermissionDenied);

    smb::application::ConnectionOpenService service(*fixture->repository,
                                                    credentialStore, smbClient);
    const auto removed = service.remove(QStringLiteral("conn-1"),
                                        QStringLiteral("/reports/file.txt"));

    QVERIFY(!removed.ok());
    QVERIFY(removed.error().code == smb::core::ErrorCode::PermissionDenied);

    const auto loaded = fixture->repository->getById(QStringLiteral("conn-1"));
    QVERIFY(loaded.ok());
    QVERIFY(loaded.value().lastErrorCode ==
            smb::core::ErrorCode::PermissionDenied);
  }

  void transferOperationsDelegateToSmbBackend() {
    auto fixture = createFixture();
    QVERIFY(fixture->repository != nullptr);
    QVERIFY(fixture->repository->add(connection()).ok());

    FakeCredentialStore credentialStore;
    credentialStore.values.insert(QStringLiteral("credential-ref"),
                                  QByteArrayLiteral("expected-secret"));

    smb::tests::FakeSmbClient smbClient;
    smbClient.setRequirePassword(true);
    smbClient.setExpectedSecret(QByteArrayLiteral("expected-secret"));
    smbClient.addFile(QStringLiteral("/reports/remote.txt"),
                      QByteArrayLiteral("remote-data"));

    smb::application::ConnectionOpenService service(*fixture->repository,
                                                    credentialStore, smbClient);

    const auto downloadedPath =
        fixture->tempDir.filePath(QStringLiteral("downloaded.txt"));
    const auto downloaded = service.downloadFile(
        QStringLiteral("conn-1"), QStringLiteral("/reports/remote.txt"),
        downloadedPath);
    QVERIFY2(downloaded.ok(),
             qPrintable(downloaded.error().sanitizedTechnicalDetails));
    QFile downloadedFile(downloadedPath);
    QVERIFY(downloadedFile.open(QIODevice::ReadOnly));
    QCOMPARE(downloadedFile.readAll(), QByteArrayLiteral("remote-data"));

    const auto uploadPath =
        fixture->tempDir.filePath(QStringLiteral("upload.txt"));
    {
      QFile upload(uploadPath);
      QVERIFY(upload.open(QIODevice::WriteOnly | QIODevice::Truncate));
      upload.write("upload-data");
    }

    const auto uploaded =
        service.uploadFile(QStringLiteral("conn-1"), uploadPath,
                           QStringLiteral("/reports/upload.txt"));
    QVERIFY2(uploaded.ok(),
             qPrintable(uploaded.error().sanitizedTechnicalDetails));

    const auto listed = service.listDirectory(QStringLiteral("conn-1"),
                                              QStringLiteral("/reports"));
    QVERIFY(listed.ok());
    bool foundUpload = false;
    for (const auto &entry : listed.value().entries) {
      if (entry.name == QStringLiteral("upload.txt")) {
        foundUpload = true;
        break;
      }
    }
    QVERIFY(foundUpload);
  }
};

QTEST_MAIN(ConnectionOpenServiceTest)

#include "test_connection_open_service.moc"
