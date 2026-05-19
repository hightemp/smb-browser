#pragma once

#include "core/Connection.h"
#include "core/CredentialStore.h"
#include "core/SmbClient.h"

#include <optional>

namespace smb::core {

class DfsReferralResolver {
public:
  virtual ~DfsReferralResolver() = default;

  virtual Result<std::optional<Connection>>
  resolve(const Connection &connection, const CredentialSecret *secret,
          const OperationContext &context) = 0;
};

} // namespace smb::core
