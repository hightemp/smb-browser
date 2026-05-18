#include "application/OpenFileService.h"
#include "fakes/FakeSmbClient.h"

#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

class FakeLocalFileOpener final : public smb::application::LocalFileOpener {
public:
  smb::core::Result<bool> openLocalFile(const QString &localPath) override {
    openedPaths.push_back(localPath);
    if (fail) {
      return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
          smb::core::ErrorCode::LocalIoError,
          smb::core::ErrorCategory::Transfer, QStringLiteral("Open failed.")));
    }
    return smb::core::Result<bool>::success(true);
  }

  bool fail = false;
  QVector<QString> openedPaths;
};

smb::core::Connection connection() {
  auto value = smb::core::Connection::createEmpty();
  value.id = QStringLiteral("connection-1");
  value.name = QStringLiteral("Test Share");
  value.normalizedUri = QStringLiteral("smb://server/share");
  value.server = QStringLiteral("server");
  value.share = QStringLiteral("share");
  return value;
}

} // namespace

class OpenFileServiceTest final : public QObject {
  Q_OBJECT

private slots:
  void downloadsToCacheAndOpensLocalFile() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::tests::FakeSmbClient smbClient;
    smbClient.addFile(QStringLiteral("/docs/readme.txt"),
                      QByteArrayLiteral("text"));

    smb::application::OperationQueue queue(1);
    smb::application::TempFileCache cache(tempDir.path());
    FakeLocalFileOpener opener;
    smb::application::OpenFileService service(queue, smbClient, cache, opener);

    const auto id = service.openRemoteFile(connection(), std::nullopt,
                                           QStringLiteral("/docs/readme.txt"));

    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Completed);
    QCOMPARE(opener.openedPaths.size(), 1);
    QVERIFY(QFileInfo::exists(opener.openedPaths.first()));
    QVERIFY(!opener.openedPaths.first().contains(QStringLiteral("readme")));
    QVERIFY(cache.isProtectedPath(opener.openedPaths.first()));
  }

  void openerFailureFailsOperation() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    smb::tests::FakeSmbClient smbClient;
    smbClient.addFile(QStringLiteral("/docs/readme.txt"),
                      QByteArrayLiteral("text"));

    smb::application::OperationQueue queue(1);
    smb::application::TempFileCache cache(tempDir.path());
    FakeLocalFileOpener opener;
    opener.fail = true;
    smb::application::OpenFileService service(queue, smbClient, cache, opener);

    const auto id = service.openRemoteFile(connection(), std::nullopt,
                                           QStringLiteral("/docs/readme.txt"));

    QTRY_VERIFY(queue.snapshot(id).state ==
                smb::application::OperationState::Failed);
    QVERIFY(queue.snapshot(id).error.code ==
            smb::core::ErrorCode::LocalIoError);
  }
};

QTEST_MAIN(OpenFileServiceTest)

#include "test_open_file_service.moc"
