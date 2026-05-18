#pragma once

#include "core/CredentialStore.h"
#include "core/Error.h"
#include "storage/ConnectionRepository.h"

#include <optional>

namespace smb::application {

class ConnectionService {
public:
  ConnectionService(smb::infrastructure::ConnectionRepository &repository,
                    smb::core::CredentialStore &credentialStore);

  smb::core::Result<smb::core::Connection>
  create(smb::core::Connection connection,
         std::optional<smb::core::CredentialSecret> secret);
  smb::core::Result<smb::core::Connection>
  update(smb::core::Connection connection,
         std::optional<smb::core::CredentialSecret> secret);
  smb::core::Result<bool> remove(const QString &connectionId);

private:
  bool credentialIsShared(const QString &credentialRef,
                          const QString &excludingConnectionId) const;

  smb::infrastructure::ConnectionRepository &m_repository;
  smb::core::CredentialStore &m_credentialStore;
};

} // namespace smb::application
