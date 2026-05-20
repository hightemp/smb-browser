#pragma once

#include "Transport.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace smb::native_smb {

struct NativeShareInfo {
  std::string name;
  std::uint32_t rawType = 0;
  std::string type;
  std::string comment;
  bool hidden = false;
  bool special = false;
  bool temporary = false;
};

struct NativeShareList {
  std::vector<NativeShareInfo> shares;
  std::uint32_t totalEntries = 0;
  std::optional<std::uint32_t> resumeHandle;
  bool moreData = false;
  std::uint64_t messagesUsed = 0;
};

class RemoteShareEnumerator {
public:
  DecodeResult<NativeShareList>
  listShares(Transport &transport, const std::string &serverName,
             std::uint64_t messageId, std::uint32_t treeId,
             std::uint64_t sessionId, const OperationContext &context) const;
};

} // namespace smb::native_smb
