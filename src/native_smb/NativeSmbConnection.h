#pragma once

#include "NativeSmbSession.h"

#include <memory>

namespace smb::native_smb {

class NativeSmbConnection {
public:
  NativeSmbConnection(std::unique_ptr<Transport> transport,
                      NativeSmbSessionConfig sessionConfig);
  ~NativeSmbConnection();

  NativeSmbConnection(const NativeSmbConnection &) = delete;
  NativeSmbConnection &operator=(const NativeSmbConnection &) = delete;

  NativeSmbSession &session();
  const NativeSmbSession &session() const;

  DecodeResult<NativeDirectoryListing>
  listDirectory(const std::string &path, const OperationContext &context);

  DecodeResult<NativeStatResult>
  statObject(const std::string &path, const OperationContext &context);

  NativeMetadataCapabilities metadataCapabilities() const;

  DecodeResult<NativeObjectMutationResult>
  setBasicInformation(const std::string &path,
                      const FileBasicInformation &info,
                      const OperationContext &context);

  DecodeResult<NativeExtendedAttributesResult>
  listExtendedAttributes(const std::string &path,
                         const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  setExtendedAttributes(const std::string &path,
                        const std::vector<FileFullEaInformation> &entries,
                        const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  removeExtendedAttribute(const std::string &path, const std::string &name,
                          const OperationContext &context);

  DecodeResult<NativeSecurityDescriptorResult>
  querySecurityDescriptor(const std::string &path,
                          std::uint32_t securityInformation,
                          const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  setSecurityDescriptor(const std::string &path,
                        std::uint32_t securityInformation,
                        const ByteVector &descriptor,
                        const OperationContext &context);

  DecodeResult<NativeReadResult>
  readFileOnce(const std::string &path, std::uint32_t length,
               std::uint64_t offset, const OperationContext &context);
  DecodeResult<NativeFileHandle>
  openFileForRead(const std::string &path, const OperationContext &context);
  DecodeResult<NativeReadResult>
  readFileChunk(const FileId &fileId, std::uint32_t length,
                std::uint64_t offset, const OperationContext &context);
  DecodeResult<bool> closeFileHandle(const FileId &fileId,
                                     const OperationContext &context);

  DecodeResult<NativeWriteResult>
  writeFileOnce(const std::string &path, const ByteVector &data,
                std::uint64_t offset, const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  createDirectory(const std::string &path, const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  deleteObject(const std::string &path, bool directory,
               const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  deleteTree(const std::string &path, const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  deleteWildcard(const std::string &parentPath, const std::string &pattern,
                 const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  renameObject(const std::string &fromPath, const std::string &toPath,
               bool replaceIfExists, const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  createHardLink(const std::string &existingPath, const std::string &linkPath,
                 bool replaceIfExists, const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  createSymbolicLink(const std::string &linkPath,
                     const std::string &targetPath, bool directory,
                     bool relative, const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  copyFileServerSide(const std::string &sourcePath,
                     const std::string &targetPath, std::uint64_t size,
                     const OperationContext &context);

  DecodeResult<NativeNotifyResult>
  watchDirectoryOnce(const std::string &path, std::uint32_t completionFilter,
                     bool watchTree, const OperationContext &context);

  DecodeResult<NativeShareList>
  listShares(const std::string &serverName, const OperationContext &context);

  DecodeResult<NativeDfsReferralResult>
  getDfsReferrals(const std::string &requestPath,
                  const OperationContext &context);

  DecodeResult<bool> disconnect(const OperationContext &context);

private:
  std::unique_ptr<Transport> m_transport;
  NativeSmbSession m_session;
  bool m_disconnected = false;
};

} // namespace smb::native_smb
