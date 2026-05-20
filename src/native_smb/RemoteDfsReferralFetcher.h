#pragma once

#include "DfsReferral.h"
#include "Transport.h"

#include <cstdint>
#include <string>

namespace smb::native_smb {

struct NativeDfsReferralResult {
  DfsReferralResponse response;
  std::uint64_t messagesUsed = 0;
};

class RemoteDfsReferralFetcher {
public:
  DecodeResult<NativeDfsReferralResult>
  getReferrals(Transport &transport, const std::string &requestPath,
               std::uint64_t messageId, std::uint32_t treeId,
               std::uint64_t sessionId, const OperationContext &context) const;
};

} // namespace smb::native_smb
