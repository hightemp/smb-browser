#include "NativeSmbSession.h"

#include <cctype>
#include <string_view>
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

DecodeResult<NativeNotifyResult> notifyFailureFrom(
    const ProtocolError &error) {
  return DecodeResult<NativeNotifyResult>::failure(error.code, error.message);
}

DecodeResult<NativeObjectMutationResult> mutationFailure(
    ErrorCode code, std::string message) {
  return DecodeResult<NativeObjectMutationResult>::failure(code,
                                                           std::move(message));
}

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

bool isDotEntry(std::string_view name) {
  return name == "." || name == "..";
}

std::string joinRemotePath(const std::string &parent,
                           const std::string &child) {
  if (parent.empty()) {
    return child;
  }
  if (parent.back() == '\\' || parent.back() == '/') {
    return parent + child;
  }
  return parent + "\\" + child;
}

char lowerAscii(char ch) {
  return static_cast<char>(
      std::tolower(static_cast<unsigned char>(ch)));
}

bool wildcardMatches(std::string_view pattern, std::string_view text) {
  std::size_t patternIndex = 0;
  std::size_t textIndex = 0;
  std::size_t starIndex = std::string_view::npos;
  std::size_t retryTextIndex = 0;

  while (textIndex < text.size()) {
    if (patternIndex < pattern.size() &&
        (pattern[patternIndex] == '?' ||
         lowerAscii(pattern[patternIndex]) == lowerAscii(text[textIndex]))) {
      ++patternIndex;
      ++textIndex;
      continue;
    }
    if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
      starIndex = patternIndex++;
      retryTextIndex = textIndex;
      continue;
    }
    if (starIndex != std::string_view::npos) {
      patternIndex = starIndex + 1;
      textIndex = ++retryTextIndex;
      continue;
    }
    return false;
  }

  while (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
    ++patternIndex;
  }
  return patternIndex == pattern.size();
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

NativeNotifyEntry toNativeNotifyEntry(const ChangeNotifyEntry &entry) {
  NativeNotifyEntry result;
  result.action = entry.action;
  result.name = entry.name;
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
  const auto messageId = m_nextMessageId;
  const DirectoryLister lister;
  const auto result =
      lister.list(m_transport, path, messageId, m_treeId, m_sessionId,
                  context);
  if (!result.ok) {
    m_nextMessageId += 3;
    return listingFailureFrom(result.error);
  }
  m_nextMessageId += result.value.messagesUsed;

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
  const auto messageId = m_nextMessageId;
  const RemoteStatReader statReader;
  const auto result = statReader.stat(m_transport, path, messageId, m_treeId,
                                      m_sessionId, context);
  if (!result.ok) {
    m_nextMessageId += result.error.messagesUsed;
    return statFailureFrom(result.error);
  }
  m_nextMessageId += result.value.messagesUsed;

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

DecodeResult<NativeObjectMutationResult>
NativeSmbSession::deleteTree(const std::string &path,
                             const OperationContext &context) {
  return deleteTreeInternal(path, context);
}

DecodeResult<NativeObjectMutationResult>
NativeSmbSession::deleteWildcard(const std::string &parentPath,
                                 const std::string &pattern,
                                 const OperationContext &context) {
  if (pattern.empty()) {
    return mutationFailure(ErrorCode::InvalidPath,
                           "SMB wildcard delete pattern is empty.");
  }
  if (isCancelled(context)) {
    return mutationFailure(ErrorCode::Cancelled,
                           "SMB wildcard delete was cancelled.");
  }

  const auto listing = listDirectory(parentPath, context);
  if (!listing.ok) {
    return mutationFailureFrom(listing.error);
  }

  for (const auto &entry : listing.value.entries) {
    if (isDotEntry(entry.name) || !wildcardMatches(pattern, entry.name)) {
      continue;
    }
    if (isCancelled(context)) {
      return mutationFailure(ErrorCode::Cancelled,
                             "SMB wildcard delete was cancelled.");
    }

    const auto child = joinRemotePath(parentPath, entry.name);
    const auto result = entry.directory && !entry.reparsePoint
                            ? deleteTreeInternal(child, context)
                            : deleteObject(child, entry.directory, context);
    if (!result.ok) {
      return result;
    }
  }

  NativeObjectMutationResult mutation;
  mutation.path = joinRemotePath(parentPath, pattern);
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

DecodeResult<NativeNotifyResult> NativeSmbSession::watchDirectoryOnce(
    const std::string &path, std::uint32_t completionFilter, bool watchTree,
    const OperationContext &context) {
  const auto messageId = allocateMessageIds(3);
  const DirectoryWatcher watcher;
  const auto result =
      watcher.waitOnce(m_transport, path, completionFilter, watchTree, 65536,
                       messageId, m_treeId, m_sessionId, context);
  if (!result.ok) {
    return notifyFailureFrom(result.error);
  }

  NativeNotifyResult notify;
  notify.enumerationRequired = result.value.status == kStatusNotifyEnumDir;
  notify.entries.reserve(result.value.entries.size());
  for (const auto &entry : result.value.entries) {
    notify.entries.push_back(toNativeNotifyEntry(entry));
  }
  return DecodeResult<NativeNotifyResult>::success(std::move(notify));
}

DecodeResult<NativeObjectMutationResult>
NativeSmbSession::deleteTreeInternal(const std::string &path,
                                     const OperationContext &context) {
  if (path.empty()) {
    return mutationFailure(ErrorCode::InvalidPath,
                           "Refusing to delete the SMB share root.");
  }
  if (isCancelled(context)) {
    return mutationFailure(ErrorCode::Cancelled,
                           "SMB recursive delete was cancelled.");
  }

  const auto stat = statObject(path, context);
  if (!stat.ok) {
    return mutationFailureFrom(stat.error);
  }
  if (!stat.value.directory || stat.value.reparsePoint) {
    return deleteObject(path, stat.value.directory, context);
  }

  const auto listing = listDirectory(path, context);
  if (!listing.ok) {
    return mutationFailureFrom(listing.error);
  }

  for (const auto &entry : listing.value.entries) {
    if (isDotEntry(entry.name)) {
      continue;
    }
    if (isCancelled(context)) {
      return mutationFailure(ErrorCode::Cancelled,
                             "SMB recursive delete was cancelled.");
    }

    const auto child = joinRemotePath(path, entry.name);
    const auto result = entry.directory && !entry.reparsePoint
                            ? deleteTreeInternal(child, context)
                            : deleteObject(child, entry.directory, context);
    if (!result.ok) {
      return result;
    }
  }

  return deleteObject(path, true, context);
}

std::uint64_t NativeSmbSession::nextMessageIdForTests() const {
  return m_nextMessageId;
}

std::uint32_t NativeSmbSession::treeId() const { return m_treeId; }

std::uint64_t NativeSmbSession::sessionId() const { return m_sessionId; }

std::uint64_t NativeSmbSession::allocateMessageIds(std::uint64_t count) {
  const auto messageId = m_nextMessageId;
  m_nextMessageId += count;
  return messageId;
}

} // namespace smb::native_smb
