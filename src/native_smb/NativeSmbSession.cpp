#include "NativeSmbSession.h"

#include "CloseExchanger.h"
#include "ReadExchanger.h"

#include <algorithm>
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

DecodeResult<NativeExtendedAttributesResult> eaFailureFrom(
    const ProtocolError &error) {
  return DecodeResult<NativeExtendedAttributesResult>::failure(error.code,
                                                               error.message);
}

DecodeResult<NativeSecurityDescriptorResult> securityFailureFrom(
    const ProtocolError &error) {
  return DecodeResult<NativeSecurityDescriptorResult>::failure(error.code,
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

DecodeResult<ByteVector> exchangePayload(Transport &transport,
                                         const ByteVector &request,
                                         const OperationContext &context) {
  const auto frame = transport.exchange(encodeDirectTcpFrame(request), context);
  if (!frame.ok) {
    return DecodeResult<ByteVector>::failure(frame.error.code,
                                             frame.error.message);
  }
  return decodeDirectTcpPayload(frame.value);
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

CreateRequestOptions copySourceOptions(const std::string &path) {
  CreateRequestOptions options;
  options.path = path;
  options.desiredAccess = kFileReadData | kFileReadAttributes;
  options.fileAttributes = kFileAttributeNormal;
  options.shareAccess = kFileShareRead | kFileShareWrite | kFileShareDelete;
  options.createDisposition = kFileOpen;
  options.createOptions = kFileNonDirectoryFile;
  return options;
}

CreateRequestOptions readFileOptions(const std::string &path) {
  CreateRequestOptions options;
  options.path = path;
  options.desiredAccess = kFileReadData | kFileReadEa | kFileReadAttributes;
  options.fileAttributes = kFileAttributeNormal;
  options.shareAccess = kFileShareRead | kFileShareWrite | kFileShareDelete;
  options.createDisposition = kFileOpen;
  options.createOptions = kFileNonDirectoryFile;
  return options;
}

std::uint64_t creditChargeForPayload(std::uint64_t size) {
  if (size == 0) {
    return 1;
  }
  return 1 + ((size - 1) / 65536);
}

CreateRequestOptions copyTargetOptions(const std::string &path) {
  CreateRequestOptions options;
  options.path = path;
  options.desiredAccess = kFileWriteData | kFileWriteAttributes |
                          kFileReadAttributes;
  options.fileAttributes = kFileAttributeNormal;
  options.shareAccess = kFileShareRead | kFileShareWrite | kFileShareDelete;
  options.createDisposition = kFileOverwriteIf;
  options.createOptions = kFileNonDirectoryFile;
  return options;
}

DecodeResult<CreateResponse> openFile(Transport &transport,
                                      const CreateRequestOptions &options,
                                      std::uint64_t messageId,
                                      std::uint32_t treeId,
                                      std::uint64_t sessionId,
                                      const OperationContext &context) {
  const auto payload = exchangePayload(
      transport, buildCreateRequest(options, messageId, treeId, sessionId),
      context);
  if (!payload.ok) {
    return DecodeResult<CreateResponse>::failure(payload.error.code,
                                                payload.error.message);
  }
  return decodeCreateResponse(payload.value);
}

DecodeResult<IoctlResponse> ioctl(Transport &transport,
                                  const IoctlRequestOptions &options,
                                  std::uint64_t messageId,
                                  std::uint32_t treeId,
                                  std::uint64_t sessionId,
                                  const OperationContext &context) {
  const auto payload = exchangePayload(
      transport, buildIoctlRequest(options, messageId, treeId, sessionId),
      context);
  if (!payload.ok) {
    return DecodeResult<IoctlResponse>::failure(payload.error.code,
                                               payload.error.message);
  }
  return decodeIoctlResponse(payload.value);
}

DecodeResult<bool> closeFile(Transport &transport, const FileId &fileId,
                             std::uint64_t messageId, std::uint32_t treeId,
                             std::uint64_t sessionId,
                             const OperationContext &context) {
  CloseRequestOptions closeOptions;
  closeOptions.fileId = fileId;
  const CloseExchanger closer;
  const auto response =
      closer.close(transport, closeOptions, messageId, treeId, sessionId,
                   context);
  if (!response.ok) {
    return DecodeResult<bool>::failure(response.error.code,
                                       response.error.message);
  }
  return DecodeResult<bool>::success(true);
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
    if (isDotEntry(entry.name)) {
      continue;
    }
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

DecodeResult<NativeFileHandle>
NativeSmbSession::openFileForRead(const std::string &path,
                                  const OperationContext &context) {
  const auto messageId = allocateMessageIds(1);
  const auto response = openFile(m_transport, readFileOptions(path), messageId,
                                 m_treeId, m_sessionId, context);
  if (!response.ok) {
    return DecodeResult<NativeFileHandle>::failure(response.error.code,
                                                   response.error.message);
  }

  NativeFileHandle handle;
  handle.fileId = response.value.fileId;
  handle.size = response.value.endOfFile;
  return DecodeResult<NativeFileHandle>::success(handle);
}

DecodeResult<NativeReadResult>
NativeSmbSession::readFileChunk(const FileId &fileId, std::uint32_t length,
                                std::uint64_t offset,
                                const OperationContext &context) {
  const auto messageId = allocateMessageIds(creditChargeForPayload(length));
  ReadRequestOptions readOptions;
  readOptions.fileId = fileId;
  readOptions.length = length;
  readOptions.offset = offset;
  const ReadExchanger reader;
  const auto response = reader.read(m_transport, readOptions, messageId,
                                    m_treeId, m_sessionId, context);
  if (!response.ok) {
    return readFailureFrom(response.error);
  }

  NativeReadResult read;
  read.data = response.value.data;
  read.dataRemaining = response.value.dataRemaining;
  return DecodeResult<NativeReadResult>::success(std::move(read));
}

DecodeResult<bool>
NativeSmbSession::closeFileHandle(const FileId &fileId,
                                  const OperationContext &context) {
  const auto messageId = allocateMessageIds(1);
  return closeFile(m_transport, fileId, messageId, m_treeId, m_sessionId,
                   context);
}

DecodeResult<NativeWriteResult>
NativeSmbSession::writeFileOnce(const std::string &path,
                                const ByteVector &data, std::uint64_t offset,
                                const OperationContext &context) {
  const auto messageId = m_nextMessageId;
  const FileWriter writer;
  const auto result =
      writer.writeOnce(m_transport, path, data, offset, messageId, m_treeId,
                       m_sessionId, context);
  if (!result.ok) {
    m_nextMessageId += 3;
    return writeFailureFrom(result.error);
  }
  m_nextMessageId += result.value.messagesUsed;

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

NativeMetadataCapabilities NativeSmbSession::metadataCapabilities() const {
  return NativeMetadataCapabilities{};
}

DecodeResult<NativeObjectMutationResult> NativeSmbSession::setBasicInformation(
    const std::string &path, const FileBasicInformation &info,
    const OperationContext &context) {
  const auto messageId = m_nextMessageId;
  const RemoteMetadataOperator metadata;
  const auto result =
      metadata.setBasicInformation(m_transport, path, info, messageId,
                                   m_treeId, m_sessionId, context);
  if (!result.ok) {
    m_nextMessageId += result.error.messagesUsed;
    return mutationFailureFrom(result.error);
  }
  m_nextMessageId += result.value.messagesUsed;

  NativeObjectMutationResult mutation;
  mutation.path = path;
  return DecodeResult<NativeObjectMutationResult>::success(std::move(mutation));
}

DecodeResult<NativeExtendedAttributesResult>
NativeSmbSession::listExtendedAttributes(const std::string &path,
                                         const OperationContext &context) {
  const auto messageId = m_nextMessageId;
  const RemoteMetadataOperator metadata;
  const auto result = metadata.listExtendedAttributes(
      m_transport, path, messageId, m_treeId, m_sessionId, context);
  if (!result.ok) {
    m_nextMessageId += result.error.messagesUsed;
    return eaFailureFrom(result.error);
  }
  m_nextMessageId += result.value.messagesUsed;

  NativeExtendedAttributesResult attributes;
  attributes.entries = result.value.entries;
  return DecodeResult<NativeExtendedAttributesResult>::success(
      std::move(attributes));
}

DecodeResult<NativeObjectMutationResult>
NativeSmbSession::setExtendedAttributes(
    const std::string &path,
    const std::vector<FileFullEaInformation> &entries,
    const OperationContext &context) {
  const auto messageId = m_nextMessageId;
  const RemoteMetadataOperator metadata;
  const auto result = metadata.setExtendedAttributes(
      m_transport, path, entries, messageId, m_treeId, m_sessionId, context);
  if (!result.ok) {
    m_nextMessageId += result.error.messagesUsed;
    return mutationFailureFrom(result.error);
  }
  m_nextMessageId += result.value.messagesUsed;

  NativeObjectMutationResult mutation;
  mutation.path = path;
  return DecodeResult<NativeObjectMutationResult>::success(std::move(mutation));
}

DecodeResult<NativeObjectMutationResult>
NativeSmbSession::removeExtendedAttribute(const std::string &path,
                                          const std::string &name,
                                          const OperationContext &context) {
  const auto messageId = m_nextMessageId;
  const RemoteMetadataOperator metadata;
  const auto result = metadata.removeExtendedAttribute(
      m_transport, path, name, messageId, m_treeId, m_sessionId, context);
  if (!result.ok) {
    m_nextMessageId += result.error.messagesUsed;
    return mutationFailureFrom(result.error);
  }
  m_nextMessageId += result.value.messagesUsed;

  NativeObjectMutationResult mutation;
  mutation.path = path;
  return DecodeResult<NativeObjectMutationResult>::success(std::move(mutation));
}

DecodeResult<NativeSecurityDescriptorResult>
NativeSmbSession::querySecurityDescriptor(
    const std::string &path, std::uint32_t securityInformation,
    const OperationContext &context) {
  const auto messageId = m_nextMessageId;
  const RemoteMetadataOperator metadata;
  const auto result = metadata.querySecurityDescriptor(
      m_transport, path, securityInformation, messageId, m_treeId, m_sessionId,
      context);
  if (!result.ok) {
    m_nextMessageId += result.error.messagesUsed;
    return securityFailureFrom(result.error);
  }
  m_nextMessageId += result.value.messagesUsed;

  NativeSecurityDescriptorResult security;
  security.descriptor = result.value.descriptor;
  return DecodeResult<NativeSecurityDescriptorResult>::success(
      std::move(security));
}

DecodeResult<NativeObjectMutationResult>
NativeSmbSession::setSecurityDescriptor(const std::string &path,
                                        std::uint32_t securityInformation,
                                        const ByteVector &descriptor,
                                        const OperationContext &context) {
  const auto messageId = m_nextMessageId;
  const RemoteMetadataOperator metadata;
  const auto result = metadata.setSecurityDescriptor(
      m_transport, path, securityInformation, descriptor, messageId, m_treeId,
      m_sessionId, context);
  if (!result.ok) {
    m_nextMessageId += result.error.messagesUsed;
    return mutationFailureFrom(result.error);
  }
  m_nextMessageId += result.value.messagesUsed;

  NativeObjectMutationResult mutation;
  mutation.path = path;
  return DecodeResult<NativeObjectMutationResult>::success(std::move(mutation));
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

DecodeResult<NativeObjectMutationResult> NativeSmbSession::createHardLink(
    const std::string &existingPath, const std::string &linkPath,
    bool replaceIfExists, const OperationContext &context) {
  const auto messageId = allocateMessageIds(3);
  const RemoteObjectOperator objects;
  const auto result =
      objects.createHardLink(m_transport, existingPath, linkPath,
                             replaceIfExists, messageId, m_treeId,
                             m_sessionId, context);
  if (!result.ok) {
    return mutationFailureFrom(result.error);
  }

  NativeObjectMutationResult mutation;
  mutation.path = linkPath;
  return DecodeResult<NativeObjectMutationResult>::success(std::move(mutation));
}

DecodeResult<NativeObjectMutationResult> NativeSmbSession::createSymbolicLink(
    const std::string &linkPath, const std::string &targetPath,
    bool directory, bool relative, const OperationContext &context) {
  const auto messageId = allocateMessageIds(3);
  const RemoteObjectOperator objects;
  const auto result =
      objects.createSymbolicLink(m_transport, linkPath, targetPath, directory,
                                 relative, messageId, m_treeId, m_sessionId,
                                 context);
  if (!result.ok) {
    return mutationFailureFrom(result.error);
  }

  NativeObjectMutationResult mutation;
  mutation.path = linkPath;
  return DecodeResult<NativeObjectMutationResult>::success(std::move(mutation));
}

DecodeResult<NativeObjectMutationResult> NativeSmbSession::copyFileServerSide(
    const std::string &sourcePath, const std::string &targetPath,
    std::uint64_t size, const OperationContext &context) {
  if (isCancelled(context)) {
    return mutationFailure(ErrorCode::Cancelled,
                           "SMB server-side copy was cancelled.");
  }

  const auto sourceCreate = openFile(
      m_transport, copySourceOptions(sourcePath), allocateMessageIds(1),
      m_treeId, m_sessionId, context);
  if (!sourceCreate.ok) {
    return mutationFailureFrom(sourceCreate.error);
  }

  const auto closeSource = [this, &context](const FileId &fileId) {
    OperationContext cleanupContext;
    cleanupContext.timeout = context.timeout;
    return closeFile(m_transport, fileId, allocateMessageIds(1), m_treeId,
                     m_sessionId, cleanupContext);
  };
  const auto closeTarget = closeSource;

  if (isCancelled(context)) {
    (void)closeSource(sourceCreate.value.fileId);
    return mutationFailure(ErrorCode::Cancelled,
                           "SMB server-side copy was cancelled.");
  }

  const auto targetCreate = openFile(
      m_transport, copyTargetOptions(targetPath), allocateMessageIds(1),
      m_treeId, m_sessionId, context);
  if (!targetCreate.ok) {
    (void)closeSource(sourceCreate.value.fileId);
    return mutationFailureFrom(targetCreate.error);
  }

  const auto closeBoth = [&]() {
    (void)closeTarget(targetCreate.value.fileId);
    (void)closeSource(sourceCreate.value.fileId);
  };

  if (size == 0) {
    closeBoth();
    NativeObjectMutationResult mutation;
    mutation.path = targetPath;
    return DecodeResult<NativeObjectMutationResult>::success(
        std::move(mutation));
  }

  IoctlRequestOptions resumeOptions;
  resumeOptions.ctlCode = kFsctlSrvRequestResumeKey;
  resumeOptions.fileId = sourceCreate.value.fileId;
  resumeOptions.maxOutputResponse = 32;
  resumeOptions.flags = kIoctlIsFsctl;
  const auto resume =
      ioctl(m_transport, resumeOptions, allocateMessageIds(1), m_treeId,
            m_sessionId, context);
  if (!resume.ok) {
    closeBoth();
    return mutationFailureFrom(resume.error);
  }
  if (resume.value.output.size() < 24) {
    closeBoth();
    return mutationFailure(
        ErrorCode::UnsupportedCapability,
        "SMB server-side copy resume key response is too short.");
  }

  constexpr std::uint32_t kCopyChunkSize = 1024 * 1024;
  std::uint64_t offset = 0;
  while (offset < size) {
    if (isCancelled(context)) {
      closeBoth();
      return mutationFailure(ErrorCode::Cancelled,
                             "SMB server-side copy was cancelled.");
    }

    const auto length = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(kCopyChunkSize, size - offset));
    IoctlRequestOptions copyOptions;
    copyOptions.ctlCode = kFsctlSrvCopyChunk;
    copyOptions.fileId = targetCreate.value.fileId;
    copyOptions.input = buildSrvCopyChunkRequest(
        resume.value.output, {CopyChunk{offset, offset, length}});
    copyOptions.maxOutputResponse = 16;
    copyOptions.flags = kIoctlIsFsctl;

    const auto copied =
        ioctl(m_transport, copyOptions, allocateMessageIds(1), m_treeId,
              m_sessionId, context);
    if (!copied.ok) {
      closeBoth();
      return mutationFailureFrom(copied.error);
    }
    const auto parsed = decodeSrvCopyChunkResponse(copied.value.output);
    if (!parsed.ok) {
      closeBoth();
      return mutationFailureFrom(parsed.error);
    }
    if (parsed.value.totalBytesWritten == 0 ||
        parsed.value.totalBytesWritten > length) {
      closeBoth();
      return mutationFailure(
          ErrorCode::ProtocolUnsupported,
          "SMB server-side copy returned an invalid byte count.");
    }

    offset += parsed.value.totalBytesWritten;
    if (context.progressCallback) {
      context.progressCallback(TransferProgress{offset, size});
    }
  }

  closeBoth();
  NativeObjectMutationResult mutation;
  mutation.path = targetPath;
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

DecodeResult<NativeShareList>
NativeSmbSession::listShares(const std::string &serverName,
                             const OperationContext &context) {
  const auto messageId = m_nextMessageId;
  const RemoteShareEnumerator shares;
  const auto result = shares.listShares(m_transport, serverName, messageId,
                                        m_treeId, m_sessionId, context);
  if (!result.ok) {
    m_nextMessageId += result.error.messagesUsed;
    return DecodeResult<NativeShareList>::failure(result.error.code,
                                                  result.error.message);
  }
  m_nextMessageId += result.value.messagesUsed;
  return result;
}

DecodeResult<NativeDfsReferralResult>
NativeSmbSession::getDfsReferrals(const std::string &requestPath,
                                  const OperationContext &context) {
  const auto messageId = m_nextMessageId;
  const RemoteDfsReferralFetcher referrals;
  const auto result = referrals.getReferrals(
      m_transport, requestPath, messageId, m_treeId, m_sessionId, context);
  if (!result.ok) {
    m_nextMessageId += result.error.messagesUsed;
    return DecodeResult<NativeDfsReferralResult>::failure(result.error.code,
                                                          result.error.message);
  }
  m_nextMessageId += result.value.messagesUsed;
  return result;
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
