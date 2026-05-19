#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

struct TreeConnectResult {
  std::uint32_t treeId = 0;
  ShareType shareType = ShareType::Disk;
  bool isDfs = false;
  bool isDfsRoot = false;
  bool requiresEncryption = false;
  std::uint32_t maximalAccess = 0;
};

class TreeConnector {
public:
  DecodeResult<TreeConnectResult>
  connect(Transport &transport, const TreeConnectRequestOptions &options,
          std::uint64_t messageId, std::uint64_t sessionId,
          const OperationContext &context) const;
};

} // namespace smb::native_smb
