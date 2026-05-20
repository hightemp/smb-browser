#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

struct RemoteMetadataMutationResult {
  FileId fileId;
  std::uint64_t messagesUsed = 0;
};

struct RemoteExtendedAttributesResult {
  FileId fileId;
  std::vector<FileFullEaInformation> entries;
  std::uint64_t messagesUsed = 0;
};

struct RemoteSecurityDescriptorResult {
  FileId fileId;
  ByteVector descriptor;
  std::uint64_t messagesUsed = 0;
};

class RemoteMetadataOperator {
public:
  DecodeResult<RemoteMetadataMutationResult>
  setBasicInformation(Transport &transport, const std::string &path,
                      const FileBasicInformation &info,
                      std::uint64_t messageId, std::uint32_t treeId,
                      std::uint64_t sessionId,
                      const OperationContext &context) const;

  DecodeResult<RemoteExtendedAttributesResult>
  listExtendedAttributes(Transport &transport, const std::string &path,
                         std::uint64_t messageId, std::uint32_t treeId,
                         std::uint64_t sessionId,
                         const OperationContext &context) const;

  DecodeResult<RemoteMetadataMutationResult>
  setExtendedAttributes(Transport &transport, const std::string &path,
                        const std::vector<FileFullEaInformation> &entries,
                        std::uint64_t messageId, std::uint32_t treeId,
                        std::uint64_t sessionId,
                        const OperationContext &context) const;

  DecodeResult<RemoteMetadataMutationResult>
  removeExtendedAttribute(Transport &transport, const std::string &path,
                          const std::string &name, std::uint64_t messageId,
                          std::uint32_t treeId, std::uint64_t sessionId,
                          const OperationContext &context) const;

  DecodeResult<RemoteSecurityDescriptorResult>
  querySecurityDescriptor(Transport &transport, const std::string &path,
                          std::uint32_t securityInformation,
                          std::uint64_t messageId, std::uint32_t treeId,
                          std::uint64_t sessionId,
                          const OperationContext &context) const;

  DecodeResult<RemoteMetadataMutationResult>
  setSecurityDescriptor(Transport &transport, const std::string &path,
                        std::uint32_t securityInformation,
                        const ByteVector &descriptor,
                        std::uint64_t messageId, std::uint32_t treeId,
                        std::uint64_t sessionId,
                        const OperationContext &context) const;
};

} // namespace smb::native_smb
