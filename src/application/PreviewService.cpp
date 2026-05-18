#include "application/PreviewService.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QStringList>

namespace smb::application {

namespace {

const QStringList &textSuffixes() {
  static const QStringList values{
      QStringLiteral("txt"),  QStringLiteral("md"),   QStringLiteral("log"),
      QStringLiteral("csv"),  QStringLiteral("json"), QStringLiteral("xml"),
      QStringLiteral("yml"),  QStringLiteral("yaml"), QStringLiteral("ini"),
      QStringLiteral("conf"), QStringLiteral("cpp"),  QStringLiteral("h"),
      QStringLiteral("hpp"),  QStringLiteral("c"),    QStringLiteral("py"),
      QStringLiteral("js"),   QStringLiteral("ts"),   QStringLiteral("css"),
      QStringLiteral("html")};
  return values;
}

const QStringList &imageSuffixes() {
  static const QStringList values{
      QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
      QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp")};
  return values;
}

smb::core::AppError previewError(const QString &details) {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::LocalIoError,
                                       smb::core::ErrorCategory::Transfer,
                                       details, false);
}

} // namespace

PreviewService::PreviewService(TempFileCache &cache,
                               RemoteFileTransferUseCase &transferUseCase,
                               qsizetype maxTextBytes)
    : m_cache(cache), m_transferUseCase(transferUseCase),
      m_maxTextBytes(maxTextBytes) {}

PreviewKind
PreviewService::previewKindFor(const smb::core::RemoteFileEntry &entry) const {
  if (!entry.isFile()) {
    return PreviewKind::Unsupported;
  }

  const auto suffix = QFileInfo(entry.name).suffix().toLower();
  if (textSuffixes().contains(suffix)) {
    return PreviewKind::Text;
  }
  if (imageSuffixes().contains(suffix)) {
    return PreviewKind::Image;
  }
  return PreviewKind::Unsupported;
}

smb::core::Result<PreviewResult>
PreviewService::preparePreview(const QString &connectionId,
                               const smb::core::RemoteFileEntry &entry,
                               const smb::core::OperationContext &context) {
  PreviewResult result;
  result.kind = previewKindFor(entry);
  if (result.kind == PreviewKind::Unsupported) {
    return smb::core::Result<PreviewResult>::success(std::move(result));
  }

  auto localPath = m_cache.localPathFor(connectionId, entry.remotePath);
  if (!localPath.ok()) {
    return smb::core::Result<PreviewResult>::failure(localPath.error());
  }

  auto downloaded = m_transferUseCase.downloadFile(
      connectionId, entry.remotePath, localPath.value(), context);
  if (!downloaded.ok()) {
    return smb::core::Result<PreviewResult>::failure(downloaded.error());
  }

  result.localPath = localPath.value();
  if (result.kind == PreviewKind::Text) {
    auto text = readTextPreview(result.localPath);
    if (!text.ok()) {
      return smb::core::Result<PreviewResult>::failure(text.error());
    }
    result.text = text.value();
    return smb::core::Result<PreviewResult>::success(std::move(result));
  }

  QImageReader reader(result.localPath);
  const auto size = reader.size();
  if (!size.isValid()) {
    return smb::core::Result<PreviewResult>::failure(
        previewError(QStringLiteral("Unable to read image preview.")));
  }
  result.imageSize = size;
  return smb::core::Result<PreviewResult>::success(std::move(result));
}

smb::core::Result<QString>
PreviewService::readTextPreview(const QString &localPath) const {
  QFile file(localPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return smb::core::Result<QString>::failure(
        previewError(QStringLiteral("Unable to read text preview.")));
  }

  const auto bytes = file.read(m_maxTextBytes);
  return smb::core::Result<QString>::success(QString::fromUtf8(bytes));
}

QString toString(PreviewKind kind) {
  switch (kind) {
  case PreviewKind::Unsupported:
    return QStringLiteral("unsupported");
  case PreviewKind::Text:
    return QStringLiteral("text");
  case PreviewKind::Image:
    return QStringLiteral("image");
  }

  return QStringLiteral("unsupported");
}

} // namespace smb::application
