#include "application/PreviewService.h"

#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

class FakeTransferUseCase final
    : public smb::application::RemoteFileTransferUseCase {
public:
  smb::core::Result<bool>
  downloadFile(const QString &, const QString &remotePath,
               const QString &localPath,
               const smb::core::OperationContext &) override {
    if (!files.contains(remotePath)) {
      return smb::core::Result<bool>::failure(
          smb::core::AppError::fromCode(smb::core::ErrorCode::FileNotFound,
                                        smb::core::ErrorCategory::Transfer,
                                        QStringLiteral("File was not found.")));
    }

    QFile file(localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
          smb::core::ErrorCode::LocalIoError,
          smb::core::ErrorCategory::Transfer,
          QStringLiteral("Unable to write local preview file.")));
    }
    file.write(files.value(remotePath));
    downloadedPaths.push_back(remotePath);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool>
  uploadFile(const QString &, const QString &, const QString &,
             const smb::core::OperationContext &) override {
    return unsupported();
  }

  smb::core::Result<bool> copy(const QString &, const QString &,
                               const QString &, const QString &,
                               const smb::core::OperationContext &) override {
    return unsupported();
  }

  smb::core::Result<bool> move(const QString &, const QString &,
                               const QString &, const QString &,
                               const smb::core::OperationContext &) override {
    return unsupported();
  }

  static smb::core::Result<bool> unsupported() {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::Unknown, smb::core::ErrorCategory::Transfer,
        QStringLiteral("Unsupported.")));
  }

  QHash<QString, QByteArray> files;
  QVector<QString> downloadedPaths;
};

smb::core::RemoteFileEntry entry(const QString &name, const QString &path) {
  smb::core::RemoteFileEntry value;
  value.name = name;
  value.remotePath = path;
  value.type = smb::core::RemoteFileType::File;
  return value;
}

QByteArray pngBytes() {
  QImage image(3, 2, QImage::Format_ARGB32);
  image.fill(Qt::red);

  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return bytes;
}

} // namespace

class PreviewServiceTest final : public QObject {
  Q_OBJECT

private slots:
  void detectsSupportedPreviewKinds() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    FakeTransferUseCase transfer;
    smb::application::TempFileCache cache(tempDir.path());
    smb::application::PreviewService service(cache, transfer);

    QCOMPARE(smb::application::toString(service.previewKindFor(entry(
                 QStringLiteral("readme.md"), QStringLiteral("/readme.md")))),
             QStringLiteral("text"));
    QCOMPARE(smb::application::toString(service.previewKindFor(entry(
                 QStringLiteral("image.png"), QStringLiteral("/image.png")))),
             QStringLiteral("image"));
    QCOMPARE(
        smb::application::toString(service.previewKindFor(entry(
            QStringLiteral("archive.zip"), QStringLiteral("/archive.zip")))),
        QStringLiteral("unsupported"));
  }

  void preparesTextPreviewThroughCache() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    FakeTransferUseCase transfer;
    transfer.files.insert(QStringLiteral("/notes.txt"),
                          QByteArrayLiteral("hello preview"));
    smb::application::TempFileCache cache(tempDir.path());
    smb::application::PreviewService service(cache, transfer);

    const auto preview = service.preparePreview(
        QStringLiteral("conn-1"),
        entry(QStringLiteral("notes.txt"), QStringLiteral("/notes.txt")));

    QVERIFY2(preview.ok(),
             qPrintable(preview.error().sanitizedTechnicalDetails));
    QCOMPARE(smb::application::toString(preview.value().kind),
             QStringLiteral("text"));
    QCOMPARE(preview.value().text, QStringLiteral("hello preview"));
    QVERIFY(QFile::exists(preview.value().localPath));
    QCOMPARE(transfer.downloadedPaths,
             QVector<QString>{QStringLiteral("/notes.txt")});
  }

  void preparesImagePreviewSizeThroughCache() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    FakeTransferUseCase transfer;
    transfer.files.insert(QStringLiteral("/image.png"), pngBytes());
    smb::application::TempFileCache cache(tempDir.path());
    smb::application::PreviewService service(cache, transfer);

    const auto preview = service.preparePreview(
        QStringLiteral("conn-1"),
        entry(QStringLiteral("image.png"), QStringLiteral("/image.png")));

    QVERIFY(preview.ok());
    QCOMPARE(smb::application::toString(preview.value().kind),
             QStringLiteral("image"));
    QCOMPARE(preview.value().imageSize, QSize(3, 2));
    QVERIFY(QFile::exists(preview.value().localPath));
  }

  void unsupportedPreviewDoesNotDownload() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    FakeTransferUseCase transfer;
    smb::application::TempFileCache cache(tempDir.path());
    smb::application::PreviewService service(cache, transfer);

    const auto preview = service.preparePreview(
        QStringLiteral("conn-1"),
        entry(QStringLiteral("archive.zip"), QStringLiteral("/archive.zip")));

    QVERIFY(preview.ok());
    QCOMPARE(smb::application::toString(preview.value().kind),
             QStringLiteral("unsupported"));
    QVERIFY(transfer.downloadedPaths.isEmpty());
  }
};

QTEST_MAIN(PreviewServiceTest)

#include "test_preview_service.moc"
