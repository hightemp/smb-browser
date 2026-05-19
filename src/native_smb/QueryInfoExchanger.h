#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

class QueryInfoExchanger {
public:
  DecodeResult<QueryInfoResponse>
  queryInfo(Transport &transport, const QueryInfoRequestOptions &options,
            std::uint64_t messageId, std::uint32_t treeId,
            std::uint64_t sessionId, const OperationContext &context) const;
};

} // namespace smb::native_smb
