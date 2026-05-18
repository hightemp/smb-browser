#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QString>

namespace smb::core {

struct CredentialSecret {
  QByteArray bytes;
};

class CredentialStore {
public:
  virtual ~CredentialStore() = default;

  virtual Result<QString> save(const QString &ownerId,
                               const CredentialSecret &secret) = 0;
  virtual Result<CredentialSecret> load(const QString &credentialRef) const = 0;
  virtual Result<bool> update(const QString &credentialRef,
                              const CredentialSecret &secret) = 0;
  virtual Result<bool> remove(const QString &credentialRef) = 0;
  virtual Result<bool> isAvailable() const = 0;
};

} // namespace smb::core
