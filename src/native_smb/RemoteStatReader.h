#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

struct RemoteStatResult {
  FileId fileId;
  std::uint64_t creationTime = 0;
  std::uint64_t lastAccessTime = 0;
  std::uint64_t lastWriteTime = 0;
  std::uint64_t changeTime = 0;
  std::uint64_t allocationSize = 0;
  std::uint64_t endOfFile = 0;
  std::uint32_t fileAttributes = 0;
  std::uint32_t numberOfLinks = 0;
  bool deletePending = false;
  bool directory = false;
  bool reparsePoint = false;
};

class RemoteStatReader {
public:
  DecodeResult<RemoteStatResult>
  stat(Transport &transport, const std::string &path,
       std::uint64_t messageId, std::uint32_t treeId,
       std::uint64_t sessionId, const OperationContext &context) const;
};

} // namespace smb::native_smb
