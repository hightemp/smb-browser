#include "application/ConnectionImportExportService.h"

namespace smb::application {

namespace {

template <typename T>
smb::core::Result<T> forwardFailure(const smb::core::AppError &error) {
  return smb::core::Result<T>::failure(error);
}

} // namespace

ConnectionImportExportService::ConnectionImportExportService(
    smb::infrastructure::ConnectionRepository &connectionRepository,
    smb::infrastructure::ConnectionGroupRepository &groupRepository,
    smb::core::CredentialStore &credentialStore)
    : m_connectionRepository(connectionRepository),
      m_groupRepository(groupRepository), m_credentialStore(credentialStore) {}

smb::core::Result<QByteArray>
ConnectionImportExportService::exportConnections(
    const ExportConnectionsRequest &request) const {
  const auto connections = m_connectionRepository.list();
  if (!connections.ok()) {
    return forwardFailure<QByteArray>(connections.error());
  }

  const auto groups = m_groupRepository.list();
  if (!groups.ok()) {
    return forwardFailure<QByteArray>(groups.error());
  }

  ExportOptions options;
  options.includePlainTextPasswords = request.includePlainTextPasswords;
  options.confirmPlainTextPasswordExport =
      request.plainTextPasswordExportConfirmed;
  options.credentialStore =
      request.includePlainTextPasswords ? &m_credentialStore : nullptr;

  return m_serializer.exportConnections(
      ExportPayload{connections.value(), groups.value()}, options);
}

smb::core::Result<ImportResult>
ConnectionImportExportService::importConnections(
    const QByteArray &bytes, DuplicatePolicy duplicatePolicy) {
  const auto existingConnections = m_connectionRepository.list();
  if (!existingConnections.ok()) {
    return forwardFailure<ImportResult>(existingConnections.error());
  }

  QSet<QString> existingConnectionIds;
  for (const auto &connection : existingConnections.value()) {
    existingConnectionIds.insert(connection.id);
  }

  ImportOptions options;
  options.duplicatePolicy = duplicatePolicy;
  options.existingConnectionIds = existingConnectionIds;

  auto imported = m_serializer.importConnections(bytes, options);
  if (!imported.ok()) {
    return imported;
  }

  const auto persistedGroups = persistGroups(imported.value().groups);
  if (!persistedGroups.ok()) {
    return forwardFailure<ImportResult>(persistedGroups.error());
  }

  const auto persistedConnections = persistConnections(
      imported.value().connections, existingConnectionIds, duplicatePolicy);
  if (!persistedConnections.ok()) {
    return forwardFailure<ImportResult>(persistedConnections.error());
  }

  return imported;
}

smb::core::Result<bool> ConnectionImportExportService::persistGroups(
    const QVector<smb::core::ConnectionGroup> &groups) {
  const auto existingGroups = m_groupRepository.list();
  if (!existingGroups.ok()) {
    return forwardFailure<bool>(existingGroups.error());
  }

  QSet<QString> existingGroupIds;
  for (const auto &group : existingGroups.value()) {
    existingGroupIds.insert(group.id);
  }

  for (const auto &group : groups) {
    auto saved = existingGroupIds.contains(group.id)
                     ? m_groupRepository.update(group)
                     : m_groupRepository.add(group);
    if (!saved.ok()) {
      return forwardFailure<bool>(saved.error());
    }
  }

  return smb::core::Result<bool>::success(true);
}

smb::core::Result<bool> ConnectionImportExportService::persistConnections(
    const QVector<smb::core::Connection> &connections,
    const QSet<QString> &existingConnectionIds,
    DuplicatePolicy duplicatePolicy) {
  for (const auto &connection : connections) {
    const auto shouldUpdate =
        duplicatePolicy == DuplicatePolicy::Replace &&
        existingConnectionIds.contains(connection.id);
    auto saved = shouldUpdate ? m_connectionRepository.update(connection)
                              : m_connectionRepository.add(connection);
    if (!saved.ok()) {
      return forwardFailure<bool>(saved.error());
    }
  }

  return smb::core::Result<bool>::success(true);
}

} // namespace smb::application
