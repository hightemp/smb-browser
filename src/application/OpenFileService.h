#pragma once

#include "application/LocalFileOpener.h"
#include "application/OperationQueue.h"
#include "application/TempFileCache.h"
#include "core/SmbClient.h"

#include <optional>

namespace smb::application {

class OpenFileService {
public:
  OpenFileService(OperationQueue &operationQueue,
                  smb::core::SmbClient &smbClient, TempFileCache &cache,
                  LocalFileOpener &fileOpener);

  QString openRemoteFile(smb::core::Connection connection,
                         std::optional<smb::core::CredentialSecret> secret,
                         QString remotePath);

private:
  OperationQueue &m_operationQueue;
  smb::core::SmbClient &m_smbClient;
  TempFileCache &m_cache;
  LocalFileOpener &m_fileOpener;
};

} // namespace smb::application
