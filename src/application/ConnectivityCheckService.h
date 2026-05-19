#pragma once

#include "core/Connection.h"
#include "core/CredentialStore.h"
#include "core/SmbClient.h"
#include "core/SmbError.h"
#include "storage/ConnectionRepository.h"

#include <QDateTime>
#include <QMetaType>
#include <optional>

namespace smb::application {

struct ConnectivityCheckResult {
  bool available = false;
  smb::core::ConnectionStatus status = smb::core::ConnectionStatus::NotChecked;
  smb::core::AppError error = smb::core::AppError::none();
  QDateTime checkedAtUtc;
};

class ConnectivityCheckUseCase {
public:
  virtual ~ConnectivityCheckUseCase() = default;

  virtual smb::core::Result<ConnectivityCheckResult>
  check(const QString &connectionId,
        const smb::core::OperationContext &context = {}) = 0;
};

class ConnectivityCheckService final : public ConnectivityCheckUseCase {
public:
  ConnectivityCheckService(
      smb::infrastructure::ConnectionRepository &repository,
      smb::core::CredentialStore &credentialStore,
      smb::core::SmbClient &smbClient);

  smb::core::Result<ConnectivityCheckResult>
  check(const QString &connectionId,
        const smb::core::OperationContext &context = {}) override;

private:
  smb::core::Result<std::optional<smb::core::CredentialSecret>>
  loadSecret(const smb::core::Connection &connection) const;
  void rememberError(const QString &connectionId,
                     const smb::core::AppError &error);

  smb::infrastructure::ConnectionRepository &m_repository;
  smb::core::CredentialStore &m_credentialStore;
  smb::core::SmbClient &m_smbClient;
};

} // namespace smb::application

Q_DECLARE_METATYPE(smb::application::ConnectivityCheckResult)
