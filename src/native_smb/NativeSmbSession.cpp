#include "NativeSmbSession.h"

#include <utility>

namespace smb::native_smb {
namespace {

DecodeResult<NativeDirectoryListing> listingFailureFrom(
    const ProtocolError &error) {
  return DecodeResult<NativeDirectoryListing>::failure(error.code,
                                                       error.message);
}

DecodeResult<NativeReadResult> readFailureFrom(const ProtocolError &error) {
  return DecodeResult<NativeReadResult>::failure(error.code, error.message);
}

DecodeResult<NativeWriteResult> writeFailureFrom(const ProtocolError &error) {
  return DecodeResult<NativeWriteResult>::failure(error.code, error.message);
}

DecodeResult<NativeStatResult> statFailureFrom(const ProtocolError &error) {
  return DecodeResult<NativeStatResult>::failure(error.code, error.message);
}

DecodeResult<NativeObjectMutationResult> mutationFailureFrom(
    const ProtocolError &error) {
  return DecodeResult<NativeObjectMutationResult>::failure(error.code,
                                                           error.message);
}

NativeRemoteEntry toNativeEntry(const DirectoryEntry &entry) {
  NativeRemoteEntry result;
  result.name = entry.name;
  result.size = entry.endOfFile;
  result.allocationSize = entry.allocationSize;
  result.creationTime = entry.creationTime;
  result.lastAccessTime = entry.lastAccessTime;
  result.lastWriteTime = entry.lastWriteTime;
  result.changeTime = entry.changeTime;
  result.attributes = entry.fileAttributes;
  result.directory = entry.isDirectory;
  result.reparsePoint = entry.isReparsePoint;
  return result;
}

} // namespace

NativeSmbSession::NativeSmbSession(Transport &transport,
                                   NativeSmbSessionConfig config)
    : m_transport(transport), m_treeId(config.treeId),
      m_sessionId(config.sessionId),
      m_nextMessageId(config.firstMessageId == 0 ? 1
                                                 : config.firstMessageId) {}

DecodeResult<NativeDirectoryListing>
NativeSmbSession::listDirectory(const std::string &path,
                                const OperationContext &context) {
  const auto messageId = allocateMessageIds(3);
  const DirectoryLister lister;
  const auto result =
      lister.list(m_transport, path, messageId, m_treeId, m_sessionId,
                  context);
  if (!result.ok) {
    return listingFailureFrom(result.error);
  }

  NativeDirectoryListing listing;
  listing.entries.reserve(result.value.entries.size());
  for (const auto &entry : result.value.entries) {
    listing.entries.push_back(toNativeEntry(entry));
  }
  return DecodeResult<NativeDirectoryListing>::success(std::move(listing));
}

DecodeResult<NativeReadResult>
NativeSmbSession::readFileOnce(const std::string &path, std::uint32_t length,
                               std::uint64_t offset,
                               const OperationContext &context) {
  const auto messageId = allocateMessageIds(3);
  const FileReader reader;
  const auto result =
      reader.readOnce(m_transport, path, length, offset, messageId, m_treeId,
                      m_sessionId, context);
  if (!result.ok) {
    return readFailureFrom(result.error);
  }

  NativeReadResult read;
  read.data = result.value.data;
  read.dataRemaining = result.value.dataRemaining;
  return DecodeResult<NativeReadResult>::success(std::move(read));
}

DecodeResult<NativeWriteResult>
NativeSmbSession::writeFileOnce(const std::string &path,
                                const ByteVector &data, std::uint64_t offset,
                                const OperationContext &context) {
  const auto messageId = allocateMessageIds(3);
  const FileWriter writer;
  const auto result =
      writer.writeOnce(m_transport, path, data, offset, messageId, m_treeId,
                       m_sessionId, context);
  if (!result.ok) {
    return writeFailureFrom(result.error);
  }

  NativeWriteResult write;
  write.bytesWritten = result.value.bytesWritten;
  write.remaining = result.value.remaining;
  return DecodeResult<NativeWriteResult>::success(write);
}

DecodeResult<NativeStatResult>
NativeSmbSession::statObject(const std::string &path,
                             const OperationContext &context) {
  const auto messageId = allocateMessageIds(4);
  const RemoteStatReader statReader;
  const auto result = statReader.stat(m_transport, path, messageId, m_treeId,
                                      m_sessionId, context);
  if (!result.ok) {
    return statFailureFrom(result.error);
  }

  NativeStatResult stat;
  stat.size = result.value.endOfFile;
  stat.allocationSize = result.value.allocationSize;
  stat.creationTime = result.value.creationTime;
  stat.lastAccessTime = result.value.lastAccessTime;
  stat.lastWriteTime = result.value.lastWriteTime;
  stat.changeTime = result.value.changeTime;
  stat.attributes = result.value.fileAttributes;
  stat.numberOfLinks = result.value.numberOfLinks;
  stat.directory = result.value.directory;
  stat.reparsePoint = result.value.reparsePoint;
  stat.deletePending = result.value.deletePending;
  return DecodeResult<NativeStatResult>::success(stat);
}

DecodeResult<NativeObjectMutationResult>
NativeSmbSession::createDirectory(const std::string &path,
                                  const OperationContext &context) {
  const auto messageId = allocateMessageIds(2);
  const RemoteObjectOperator objects;
  const auto result =
      objects.createDirectory(m_transport, path, messageId, m_treeId,
                              m_sessionId, context);
  if (!result.ok) {
    return mutationFailureFrom(result.error);
  }

  NativeObjectMutationResult mutation;
  mutation.path = path;
  return DecodeResult<NativeObjectMutationResult>::success(std::move(mutation));
}

DecodeResult<NativeObjectMutationResult>
NativeSmbSession::deleteObject(const std::string &path, bool directory,
                               const OperationContext &context) {
  const auto messageId = allocateMessageIds(3);
  const RemoteObjectOperator objects;
  const auto result =
      objects.deleteObject(m_transport, path, directory, messageId, m_treeId,
                           m_sessionId, context);
  if (!result.ok) {
    return mutationFailureFrom(result.error);
  }

  NativeObjectMutationResult mutation;
  mutation.path = path;
  return DecodeResult<NativeObjectMutationResult>::success(std::move(mutation));
}

DecodeResult<NativeObjectMutationResult> NativeSmbSession::renameObject(
    const std::string &fromPath, const std::string &toPath,
    bool replaceIfExists, const OperationContext &context) {
  const auto messageId = allocateMessageIds(3);
  const RemoteObjectOperator objects;
  const auto result =
      objects.renameObject(m_transport, fromPath, toPath, replaceIfExists,
                           messageId, m_treeId, m_sessionId, context);
  if (!result.ok) {
    return mutationFailureFrom(result.error);
  }

  NativeObjectMutationResult mutation;
  mutation.path = toPath;
  return DecodeResult<NativeObjectMutationResult>::success(std::move(mutation));
}

std::uint64_t NativeSmbSession::nextMessageIdForTests() const {
  return m_nextMessageId;
}

std::uint64_t NativeSmbSession::allocateMessageIds(std::uint64_t count) {
  const auto messageId = m_nextMessageId;
  m_nextMessageId += count;
  return messageId;
}

} // namespace smb::native_smb
