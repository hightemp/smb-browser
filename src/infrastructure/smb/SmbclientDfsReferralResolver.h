#pragma once

#include "core/DfsReferralResolver.h"

#include <QString>
#include <optional>

namespace smb::infrastructure {

struct SmbclientDfsTarget {
  QString server;
  QString share;
};

std::optional<SmbclientDfsTarget>
parseSmbclientShowconnectTarget(const QString &output);

class SmbclientDfsReferralResolver final
    : public smb::core::DfsReferralResolver {
public:
  explicit SmbclientDfsReferralResolver(int timeoutSeconds = 15);

  smb::core::Result<std::optional<smb::core::Connection>>
  resolve(const smb::core::Connection &connection,
          const smb::core::CredentialSecret *secret,
          const smb::core::OperationContext &context) override;

private:
  int m_timeoutMs = 15000;
};

} // namespace smb::infrastructure
