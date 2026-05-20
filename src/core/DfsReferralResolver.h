#pragma once

#include "core/Connection.h"
#include "core/CredentialStore.h"
#include "core/SmbClient.h"

#include <QVector>
#include <optional>
#include <utility>

namespace smb::core {

struct DfsResolvedConnection {
  Connection connection;
  int ttlSeconds = 300;
};

struct DfsResolvedPath {
  Connection connection;
  QString remotePath;
  QString originalPathPrefix;
  QString targetPathPrefix;
  int ttlSeconds = 300;
};

class DfsReferralResolver {
public:
  virtual ~DfsReferralResolver() = default;

  virtual Result<std::optional<Connection>>
  resolve(const Connection &connection, const CredentialSecret *secret,
          const OperationContext &context) = 0;

  virtual Result<QVector<DfsResolvedConnection>>
  resolveTargets(const Connection &connection, const CredentialSecret *secret,
                 const OperationContext &context) {
    const auto resolved = resolve(connection, secret, context);
    if (!resolved.ok()) {
      return Result<QVector<DfsResolvedConnection>>::failure(resolved.error());
    }

    QVector<DfsResolvedConnection> targets;
    if (resolved.value().has_value()) {
      DfsResolvedConnection target;
      target.connection = resolved.value().value();
      targets.push_back(std::move(target));
    }
    return Result<QVector<DfsResolvedConnection>>::success(std::move(targets));
  }

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

  virtual Result<QVector<DfsResolvedPath>>
  resolvePathTargets(const Connection &connection,
                     const CredentialSecret *secret,
                     const QString &remotePath,
                     const OperationContext &context) {
    const auto resolved = resolvePath(connection, secret, remotePath, context);
    if (!resolved.ok()) {
      return Result<QVector<DfsResolvedPath>>::failure(resolved.error());
    }

    QVector<DfsResolvedPath> targets;
    if (resolved.value().has_value()) {
      targets.push_back(resolved.value().value());
    }
    return Result<QVector<DfsResolvedPath>>::success(std::move(targets));
  }
};

} // namespace smb::core
