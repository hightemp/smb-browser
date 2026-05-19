#pragma once

#include "DirectoryLister.h"
#include "FileReader.h"
#include "FileWriter.h"
#include "RemoteObjectOperator.h"
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

struct NativeObjectMutationResult {
  std::string path;
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

  DecodeResult<NativeWriteResult>
  writeFileOnce(const std::string &path, const ByteVector &data,
                std::uint64_t offset, const OperationContext &context);

  DecodeResult<NativeStatResult>
  statObject(const std::string &path, const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  createDirectory(const std::string &path, const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  deleteObject(const std::string &path, bool directory,
               const OperationContext &context);

  DecodeResult<NativeObjectMutationResult>
  renameObject(const std::string &fromPath, const std::string &toPath,
               bool replaceIfExists, const OperationContext &context);

  std::uint64_t nextMessageIdForTests() const;

private:
  std::uint64_t allocateMessageIds(std::uint64_t count);

  Transport &m_transport;
  std::uint32_t m_treeId = 0;
  std::uint64_t m_sessionId = 0;
  std::uint64_t m_nextMessageId = 1;
};

} // namespace smb::native_smb
