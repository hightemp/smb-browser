#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

struct DirectoryWatchResult {
  FileId directoryFileId;
  std::vector<ChangeNotifyEntry> entries;
  std::uint32_t status = kStatusSuccess;
};

class DirectoryWatcher {
public:
  DecodeResult<DirectoryWatchResult>
  waitOnce(Transport &transport, const std::string &path,
           std::uint32_t completionFilter, bool watchTree,
           std::uint32_t outputBufferLength, std::uint64_t messageId,
           std::uint32_t treeId, std::uint64_t sessionId,
           const OperationContext &context) const;
};

} // namespace smb::native_smb
