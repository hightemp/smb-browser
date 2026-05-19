#pragma once

#include "core/Connection.h"
#include "core/CredentialStore.h"
#include "core/SmbClient.h"

#include <optional>
#include <utility>

namespace smb::core {

struct DfsResolvedPath {
  Connection connection;
  QString remotePath;
  QString originalPathPrefix;
  QString targetPathPrefix;
};

class DfsReferralResolver {
public:
  virtual ~DfsReferralResolver() = default;

  virtual Result<std::optional<Connection>>
  resolve(const Connection &connection, const CredentialSecret *secret,
          const OperationContext &context) = 0;

  virtual Result<std::optional<DfsResolvedPath>>
  resolvePath(const Connection &connection, const CredentialSecret *secret,
              const QString &remotePath, const OperationContext &context) {
    const auto resolved = resolve(connection, secret, context);
    if (!resolved.ok()) {
      return Result<std::optional<DfsResolvedPath>>::failure(resolved.error());
    }
    if (!resolved.value().has_value()) {
      return Result<std::optional<DfsResolvedPath>>::success(std::nullopt);
    }

    DfsResolvedPath path;
    path.connection = resolved.value().value();
    path.remotePath = remotePath;
    path.originalPathPrefix = QStringLiteral("/");
    path.targetPathPrefix = QStringLiteral("/");
    return Result<std::optional<DfsResolvedPath>>::success(std::move(path));
  }
};

} // namespace smb::core
