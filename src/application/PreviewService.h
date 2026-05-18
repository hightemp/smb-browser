#pragma once

#include "application/ConnectionOpenService.h"
#include "application/TempFileCache.h"
#include "core/RemoteFileEntry.h"

#include <QSize>

namespace smb::application {

enum class PreviewKind {
  Unsupported,
  Text,
  Image,
};

struct PreviewResult {
  PreviewKind kind = PreviewKind::Unsupported;
  QString localPath;
  QString text;
  QSize imageSize;
};

class PreviewService {
public:
  PreviewService(TempFileCache &cache,
                 RemoteFileTransferUseCase &transferUseCase,
                 qsizetype maxTextBytes = 1024 * 1024);

  PreviewKind previewKindFor(const smb::core::RemoteFileEntry &entry) const;
  smb::core::Result<PreviewResult>
  preparePreview(const QString &connectionId,
                 const smb::core::RemoteFileEntry &entry,
                 const smb::core::OperationContext &context = {});

private:
  smb::core::Result<QString> readTextPreview(const QString &localPath) const;

  TempFileCache &m_cache;
  RemoteFileTransferUseCase &m_transferUseCase;
  qsizetype m_maxTextBytes = 1024 * 1024;
};

QString toString(PreviewKind kind);

} // namespace smb::application
