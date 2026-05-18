#include "application/ConnectivityCheckService.h"

#include <QDateTime>
#include <utility>

namespace smb::application {

namespace {

QString errorMessageForStorage(const smb::core::AppError &error) {
  if (!error.sanitizedTechnicalDetails.isEmpty()) {
    return error.sanitizedTechnicalDetails;
  }
  return error.userMessage;
}

} // namespace

ConnectivityCheckService::ConnectivityCheckService(
    smb::infrastructure::ConnectionRepository &repository,
    smb::core::CredentialStore &credentialStore,
    smb::core::SmbClient &smbClient)
    : m_repository(repository), m_credentialStore(credentialStore),
      m_smbClient(smbClient) {}

smb::core::Result<ConnectivityCheckResult>
ConnectivityCheckService::check(const QString &connectionId,
                                const smb::core::OperationContext &context) {
  const auto loadedConnection = m_repository.getById(connectionId);
  if (!loadedConnection.ok()) {
    return smb::core::Result<ConnectivityCheckResult>::failure(
        loadedConnection.error());
  }

  const auto &connection = loadedConnection.value();
  auto loadedSecret = loadSecret(connection);
  if (!loadedSecret.ok()) {
    rememberError(connection.id, loadedSecret.error());
    return smb::core::Result<ConnectivityCheckResult>::failure(
        loadedSecret.error());
  }

  auto secret = std::move(loadedSecret.value());
  const auto *secretPtr = secret.has_value() ? &secret.value() : nullptr;
  const auto checked =
      m_smbClient.checkConnection(connection, secretPtr, context);
  const auto checkedAt = QDateTime::currentDateTimeUtc();

  ConnectivityCheckResult result;
  result.checkedAtUtc = checkedAt;

  if (checked.ok() && checked.value()) {
    const auto updated =
        m_repository.updateLastSuccessfulCheck(connection.id, checkedAt);
    if (!updated.ok()) {
      return smb::core::Result<ConnectivityCheckResult>::failure(
          updated.error());
    }
    result.available = true;
    result.status = smb::core::ConnectionStatus::Available;
    return smb::core::Result<ConnectivityCheckResult>::success(
        std::move(result));
  }

  result.available = false;
  result.error = checked.error();
  result.status = smb::core::connectionStatusForSmbError(checked.error().code);
  rememberError(connection.id, checked.error());
  return smb::core::Result<ConnectivityCheckResult>::success(std::move(result));
}

smb::core::Result<std::optional<smb::core::CredentialSecret>>
ConnectivityCheckService::loadSecret(
    const smb::core::Connection &connection) const {
  if (!connection.usesStoredCredential()) {
    return smb::core::Result<
        std::optional<smb::core::CredentialSecret>>::success(std::nullopt);
  }

  auto secret = m_credentialStore.load(connection.credentialRef);
  if (!secret.ok()) {
    return smb::core::Result<
        std::optional<smb::core::CredentialSecret>>::failure(secret.error());
  }

  return smb::core::Result<std::optional<smb::core::CredentialSecret>>::success(
      std::move(secret.value()));
}

void ConnectivityCheckService::rememberError(const QString &connectionId,
                                             const smb::core::AppError &error) {
  m_repository.updateLastError(connectionId, error.code,
                               errorMessageForStorage(error));
}

} // namespace smb::application
