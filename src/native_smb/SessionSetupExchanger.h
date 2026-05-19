#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

class SessionSetupExchanger {
public:
  DecodeResult<SessionSetupResponse>
  exchange(Transport &transport, const SessionSetupRequestOptions &options,
           std::uint64_t messageId, std::uint64_t sessionId,
           const OperationContext &context) const;
};

} // namespace smb::native_smb
