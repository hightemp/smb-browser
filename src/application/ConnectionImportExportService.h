#pragma once

#include "application/ImportExportService.h"
#include "core/CredentialStore.h"
#include "storage/ConnectionGroupRepository.h"
#include "storage/ConnectionRepository.h"

namespace smb::application {

struct ExportConnectionsRequest {
  bool includePlainTextPasswords = false;
  bool plainTextPasswordExportConfirmed = false;
};

class ImportExportUseCase {
public:
  virtual ~ImportExportUseCase() = default;

  virtual smb::core::Result<QByteArray>
  exportConnections(const ExportConnectionsRequest &request) const = 0;
  virtual smb::core::Result<ImportResult>
  importConnections(const QByteArray &bytes,
                    DuplicatePolicy duplicatePolicy) = 0;
};

class ConnectionImportExportService final : public ImportExportUseCase {
public:
  ConnectionImportExportService(
      smb::infrastructure::ConnectionRepository &connectionRepository,
      smb::infrastructure::ConnectionGroupRepository &groupRepository,
      smb::core::CredentialStore &credentialStore);

  smb::core::Result<QByteArray>
  exportConnections(const ExportConnectionsRequest &request) const override;
  smb::core::Result<ImportResult>
  importConnections(const QByteArray &bytes,
                    DuplicatePolicy duplicatePolicy) override;

private:
  smb::core::Result<bool>
  persistGroups(const QVector<smb::core::ConnectionGroup> &groups);
  smb::core::Result<bool>
  persistConnections(const QVector<smb::core::Connection> &connections,
                     const QSet<QString> &existingConnectionIds,
                     DuplicatePolicy duplicatePolicy);

  smb::infrastructure::ConnectionRepository &m_connectionRepository;
  smb::infrastructure::ConnectionGroupRepository &m_groupRepository;
  smb::core::CredentialStore &m_credentialStore;
  ImportExportService m_serializer;
};

} // namespace smb::application
