#include "application/OpenFileService.h"

#include <utility>

namespace smb::application {

namespace {

const smb::core::CredentialSecret *
secretPtr(const std::optional<smb::core::CredentialSecret> &secret) {
  return secret.has_value() ? &secret.value() : nullptr;
}

QString cacheConnectionKey(const smb::core::Connection &connection) {
  if (!connection.id.isEmpty()) {
    return connection.id;
  }
  return connection.normalizedUri;
}

} // namespace

OpenFileService::OpenFileService(OperationQueue &operationQueue,
                                 smb::core::SmbClient &smbClient,
                                 TempFileCache &cache,
                                 LocalFileOpener &fileOpener)
    : m_operationQueue(operationQueue), m_smbClient(smbClient), m_cache(cache),
      m_fileOpener(fileOpener) {}

QString OpenFileService::openRemoteFile(
    smb::core::Connection connection,
    std::optional<smb::core::CredentialSecret> secret, QString remotePath) {
  return m_operationQueue.enqueue(
      QStringLiteral("open:%1").arg(remotePath),
      [this, connection = std::move(connection), secret = std::move(secret),
       remotePath =
           std::move(remotePath)](const smb::core::OperationContext &context) {
        const auto localPath =
            m_cache.localPathFor(cacheConnectionKey(connection), remotePath);
        if (!localPath.ok()) {
          return smb::core::Result<bool>::failure(localPath.error());
        }

        auto downloaded =
            m_smbClient.downloadFile(connection, secretPtr(secret), remotePath,
                                     localPath.value(), context);
        if (!downloaded.ok()) {
          return downloaded;
        }

        m_cache.protectPath(localPath.value());
        return m_fileOpener.openLocalFile(localPath.value());
      });
}

} // namespace smb::application
