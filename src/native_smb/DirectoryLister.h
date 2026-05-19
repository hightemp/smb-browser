#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

struct DirectoryListResult {
  FileId directoryFileId;
  std::vector<DirectoryEntry> entries;
  std::uint64_t messagesUsed = 0;
};

class DirectoryLister {
public:
  DecodeResult<DirectoryListResult>
  list(Transport &transport, const std::string &path, std::uint64_t messageId,
       std::uint32_t treeId, std::uint64_t sessionId,
       const OperationContext &context) const;
};

} // namespace smb::native_smb
