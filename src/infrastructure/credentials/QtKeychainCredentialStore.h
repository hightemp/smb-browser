#pragma once

#include "core/CredentialStore.h"

#include <QString>

namespace smb::infrastructure {

class QtKeychainCredentialStore final : public smb::core::CredentialStore {
public:
  explicit QtKeychainCredentialStore(
      QString serviceName = QStringLiteral("SMB Browser"));

  smb::core::Result<QString>
  save(const QString &ownerId,
       const smb::core::CredentialSecret &secret) override;
  smb::core::Result<smb::core::CredentialSecret>
  load(const QString &credentialRef) const override;
  smb::core::Result<bool>
  update(const QString &credentialRef,
         const smb::core::CredentialSecret &secret) override;
  smb::core::Result<bool> remove(const QString &credentialRef) override;
  smb::core::Result<bool> isAvailable() const override;

private:
  QString m_serviceName;
};

} // namespace smb::infrastructure
