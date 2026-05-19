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

  DecodeResult<NativeReadResult>
  readFileOnce(const std::string &path, std::uint32_t length,
               std::uint64_t offset, const OperationContext &context);

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

  DecodeResult<bool> disconnect(const OperationContext &context);

private:
  std::unique_ptr<Transport> m_transport;
  NativeSmbSession m_session;
  bool m_disconnected = false;
};

} // namespace smb::native_smb
