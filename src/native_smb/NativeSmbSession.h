#pragma once

#include "DirectoryLister.h"
#include "DirectoryWatcher.h"
#include "FileReader.h"
#include "FileWriter.h"
#include "RemoteDfsReferralFetcher.h"
#include "RemoteMetadataOperator.h"
#include "RemoteObjectOperator.h"
#include "RemoteShareEnumerator.h"
#include "RemoteStatReader.h"
#include "Transport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace smb::native_smb {

struct NativeRemoteEntry {
  std::string name;
  std::uint64_t size = 0;
  std::uint64_t allocationSize = 0;
  std::uint64_t creationTime = 0;
  std::uint64_t lastAccessTime = 0;
  std::uint64_t lastWriteTime = 0;
  std::uint64_t changeTime = 0;
  std::uint32_t attributes = 0;
  bool directory = false;
  bool reparsePoint = false;
};

struct NativeDirectoryListing {
  std::vector<NativeRemoteEntry> entries;
};

struct NativeReadResult {
  ByteVector data;
  std::uint32_t dataRemaining = 0;
};

struct NativeFileHandle {
  FileId fileId;
  std::uint64_t size = 0;
};

struct NativeWriteResult {
  std::uint32_t bytesWritten = 0;
  std::uint32_t remaining = 0;
};

struct NativeStatResult {
  std::uint64_t size = 0;
  std::uint64_t allocationSize = 0;
  std::uint64_t creationTime = 0;
  std::uint64_t lastAccessTime = 0;
  std::uint64_t lastWriteTime = 0;
  std::uint64_t changeTime = 0;
  std::uint32_t attributes = 0;
  std::uint32_t numberOfLinks = 0;
  bool directory = false;
  bool reparsePoint = false;
  bool deletePending = false;
};

struct NativeMetadataCapabilities {
  bool canSetBasicInformation = true;
  bool canListExtendedAttributes = true;
  bool canSetExtendedAttributes = true;
  bool canQuerySecurityDescriptor = true;
  bool canSetSecurityDescriptor = true;
  bool canUsePosixMode = false;
  bool canUsePosixOwner = false;
  bool canUsePosixGroup = false;
};

struct NativeExtendedAttributesResult {
  std::vector<FileFullEaInformation> entries;
};

struct NativeSecurityDescriptorResult {
  ByteVector descriptor;
};

struct NativeObjectMutationResult {
  std::string path;
};

struct NativeNotifyEntry {
  std::uint32_t action = 0;
  std::string name;
};

struct NativeNotifyResult {
  std::vector<NativeNotifyEntry> entries;
  bool enumerationRequired = false;
};

struct NativeSmbSessionConfig {
  std::uint32_t treeId = 0;
  std::uint64_t sessionId = 0;
  std::uint64_t firstMessageId = 1;
};

class NativeSmbSession {
public:
  NativeSmbSession(Transport &transport, NativeSmbSessionConfig config);

  DecodeResult<NativeDirectoryListing>
  listDirectory(const std::string &path, const OperationContext &context);

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

  std::uint32_t treeId() const;
  std::uint64_t sessionId() const;
  std::uint64_t allocateMessageIds(std::uint64_t count);
  std::uint64_t nextMessageIdForTests() const;

private:
  DecodeResult<NativeObjectMutationResult>
  deleteTreeInternal(const std::string &path, const OperationContext &context);

  Transport &m_transport;
  std::uint32_t m_treeId = 0;
  std::uint64_t m_sessionId = 0;
  std::uint64_t m_nextMessageId = 1;
};

} // namespace smb::native_smb
