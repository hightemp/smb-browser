#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

class ChangeNotifyExchanger {
public:
  DecodeResult<ChangeNotifyResponse>
  wait(Transport &transport, const ChangeNotifyRequestOptions &options,
       std::uint64_t messageId, std::uint32_t treeId, std::uint64_t sessionId,
       const OperationContext &context) const;
};

} // namespace smb::native_smb
