#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

struct FileWriteResult {
  FileId fileId;
  std::uint32_t bytesWritten = 0;
  std::uint32_t remaining = 0;
};

class FileWriter {
public:
  DecodeResult<FileWriteResult>
  writeOnce(Transport &transport, const std::string &path,
            const ByteVector &data, std::uint64_t offset,
            std::uint64_t messageId, std::uint32_t treeId,
            std::uint64_t sessionId, const OperationContext &context) const;
};

} // namespace smb::native_smb
