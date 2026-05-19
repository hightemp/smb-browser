#include "DirectoryWatcher.h"

#include "ChangeNotifyExchanger.h"
#include "CloseExchanger.h"

#include <utility>

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<DirectoryWatchResult> cancelledResult() {
  return DecodeResult<DirectoryWatchResult>::failure(
      ErrorCode::Cancelled, "SMB directory watch was cancelled.");
}

DecodeResult<DirectoryWatchResult> failureFrom(const ProtocolError &error) {
  return DecodeResult<DirectoryWatchResult>::failure(error.code,
                                                     error.message);
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

CreateRequestOptions directoryCreateOptions(const std::string &path) {
  CreateRequestOptions options;
  options.path = path;
  options.desiredAccess = kFileListDirectory | kFileReadAttributes;
  options.fileAttributes = kFileAttributeNormal;
  options.shareAccess = kFileShareRead | kFileShareWrite | kFileShareDelete;
  options.createDisposition = kFileOpen;
  options.createOptions = kFileDirectoryFile;
  return options;
}

DecodeResult<CreateResponse> openDirectory(Transport &transport,
                                           const std::string &path,
                                           std::uint64_t messageId,
                                           std::uint32_t treeId,
                                           std::uint64_t sessionId,
                                           const OperationContext &context) {
  const auto request =
      buildCreateRequest(directoryCreateOptions(path), messageId, treeId,
                         sessionId);
  const auto payload = exchangePayload(transport, request, context);
  if (!payload.ok) {
    return DecodeResult<CreateResponse>::failure(payload.error.code,
                                                payload.error.message);
  }
  return decodeCreateResponse(payload.value);
}

} // namespace

DecodeResult<DirectoryWatchResult> DirectoryWatcher::waitOnce(
    Transport &transport, const std::string &path,
    std::uint32_t completionFilter, bool watchTree,
    std::uint32_t outputBufferLength, std::uint64_t messageId,
    std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto createResponse =
      openDirectory(transport, path, messageId, treeId, sessionId, context);
  if (!createResponse.ok) {
    return failureFrom(createResponse.error);
  }

  if (isCancelled(context)) {
    return cancelledResult();
  }

  ChangeNotifyRequestOptions notifyOptions;
  notifyOptions.fileId = createResponse.value.fileId;
  notifyOptions.flags = watchTree ? kSmb2WatchTree : 0;
  notifyOptions.outputBufferLength = outputBufferLength;
  notifyOptions.completionFilter = completionFilter;

  const ChangeNotifyExchanger notifier;
  const auto notifyResponse =
      notifier.wait(transport, notifyOptions, messageId + 1, treeId,
                    sessionId, context);
  if (!notifyResponse.ok) {
    return failureFrom(notifyResponse.error);
  }

  CloseRequestOptions closeOptions;
  closeOptions.fileId = createResponse.value.fileId;
  const CloseExchanger closer;
  const auto closeResponse =
      closer.close(transport, closeOptions, messageId + 2, treeId, sessionId,
                   context);
  if (!closeResponse.ok) {
    return failureFrom(closeResponse.error);
  }

  DirectoryWatchResult result;
  result.directoryFileId = createResponse.value.fileId;
  result.status = notifyResponse.value.status;
  result.entries = std::move(notifyResponse.value.entries);
  return DecodeResult<DirectoryWatchResult>::success(std::move(result));
}

} // namespace smb::native_smb
