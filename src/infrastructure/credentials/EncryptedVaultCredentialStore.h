#pragma once

#include "core/CredentialStore.h"

#include <QByteArray>
#include <QString>

namespace smb::infrastructure {

class EncryptedVaultCredentialStore final : public smb::core::CredentialStore {
public:
  EncryptedVaultCredentialStore(QString vaultPath, QByteArray masterPassword);
  ~EncryptedVaultCredentialStore() override;

  EncryptedVaultCredentialStore(const EncryptedVaultCredentialStore &) = delete;
  EncryptedVaultCredentialStore &
  operator=(const EncryptedVaultCredentialStore &) = delete;

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
  QString m_vaultPath;
  mutable QByteArray m_masterPassword;
};

} // namespace smb::infrastructure
