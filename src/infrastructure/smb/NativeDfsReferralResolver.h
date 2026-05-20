#pragma once

#include "core/DfsReferralResolver.h"
#include "core/LogSanitizer.h"

#include <QString>
#include <optional>

namespace smb::infrastructure {

struct NativeDfsReferralTarget {
  QString server;
  QString share;
  QString targetPathPrefix;
};

std::optional<NativeDfsReferralTarget>
parseNativeDfsReferralTarget(const QString &networkAddress);

class NativeDfsReferralResolver final : public smb::core::DfsReferralResolver {
public:
  explicit NativeDfsReferralResolver(
      int timeoutSeconds = 15, smb::core::LogSanitizer sanitizer = {});

  smb::core::Result<std::optional<smb::core::Connection>>
  resolve(const smb::core::Connection &connection,
          const smb::core::CredentialSecret *secret,
          const smb::core::OperationContext &context) override;

  smb::core::Result<QVector<smb::core::DfsResolvedConnection>>
  resolveTargets(const smb::core::Connection &connection,
                 const smb::core::CredentialSecret *secret,
                 const smb::core::OperationContext &context) override;

  smb::core::Result<std::optional<smb::core::DfsResolvedPath>>
  resolvePath(const smb::core::Connection &connection,
              const smb::core::CredentialSecret *secret,
              const QString &remotePath,
              const smb::core::OperationContext &context) override;

  smb::core::Result<QVector<smb::core::DfsResolvedPath>>
  resolvePathTargets(const smb::core::Connection &connection,
                     const smb::core::CredentialSecret *secret,
                     const QString &remotePath,
                     const smb::core::OperationContext &context) override;

private:
  int m_timeoutSeconds = 15;
  smb::core::LogSanitizer m_sanitizer;
};

} // namespace smb::infrastructure
