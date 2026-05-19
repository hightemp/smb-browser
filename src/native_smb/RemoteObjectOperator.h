#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

struct RemoteObjectResult {
  FileId fileId;
};

class RemoteObjectOperator {
public:
  DecodeResult<RemoteObjectResult>
  createDirectory(Transport &transport, const std::string &path,
                  std::uint64_t messageId, std::uint32_t treeId,
                  std::uint64_t sessionId,
                  const OperationContext &context) const;

  DecodeResult<RemoteObjectResult>
  deleteObject(Transport &transport, const std::string &path,
               bool directory, std::uint64_t messageId, std::uint32_t treeId,
               std::uint64_t sessionId,
               const OperationContext &context) const;

  DecodeResult<RemoteObjectResult>
  renameObject(Transport &transport, const std::string &fromPath,
               const std::string &toPath, bool replaceIfExists,
               std::uint64_t messageId, std::uint32_t treeId,
               std::uint64_t sessionId,
               const OperationContext &context) const;
};

} // namespace smb::native_smb
