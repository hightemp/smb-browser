#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

struct FileReadResult {
  FileId fileId;
  ByteVector data;
  std::uint32_t dataRemaining = 0;
};

class FileReader {
public:
  DecodeResult<FileReadResult>
  readOnce(Transport &transport, const std::string &path,
           std::uint32_t length, std::uint64_t offset,
           std::uint64_t messageId, std::uint32_t treeId,
           std::uint64_t sessionId, const OperationContext &context) const;
};

} // namespace smb::native_smb
