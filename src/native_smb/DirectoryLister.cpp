#include "DirectoryLister.h"

#include "CloseExchanger.h"

#include <utility>

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<DirectoryListResult> cancelledResult() {
  return DecodeResult<DirectoryListResult>::failure(
      ErrorCode::Cancelled, "SMB directory listing was cancelled.");
}

DecodeResult<DirectoryListResult> failureFrom(const ProtocolError &error) {
  return DecodeResult<DirectoryListResult>::failure(error.code,
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
  options.desiredAccess = kFileListDirectory | kFileReadEa |
                          kFileReadAttributes;
  options.fileAttributes = kFileAttributeNormal;
  options.shareAccess = kFileShareRead | kFileShareWrite | kFileShareDelete;
  options.createDisposition = kFileOpen;
  options.createOptions = kFileDirectoryFile;
  return options;
}

} // namespace

DecodeResult<DirectoryListResult>
DirectoryLister::list(Transport &transport, const std::string &path,
                      std::uint64_t messageId, std::uint32_t treeId,
                      std::uint64_t sessionId,
                      const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  std::uint64_t messagesUsed = 0;
  const auto createRequest = buildCreateRequest(
      directoryCreateOptions(path), messageId + messagesUsed++, treeId,
      sessionId);
  const auto createPayload = exchangePayload(transport, createRequest, context);
  if (!createPayload.ok) {
    return failureFrom(createPayload.error);
  }

  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto createResponse = decodeCreateResponse(createPayload.value);
  if (!createResponse.ok) {
    return failureFrom(createResponse.error);
  }

  QueryDirectoryRequestOptions queryOptions;
  queryOptions.fileId = createResponse.value.fileId;
  std::vector<DirectoryEntry> entries;
  bool firstQuery = true;
  while (true) {
    queryOptions.flags = firstQuery ? kQueryDirectoryRestartScans : 0;
    const auto queryRequest = buildQueryDirectoryRequest(
        queryOptions, messageId + messagesUsed++, treeId, sessionId);
    const auto queryPayload = exchangePayload(transport, queryRequest, context);
    if (!queryPayload.ok) {
      return failureFrom(queryPayload.error);
    }

    if (isCancelled(context)) {
      return cancelledResult();
    }

    const auto queryResponse = decodeQueryDirectoryResponse(queryPayload.value);
    if (!queryResponse.ok) {
      return failureFrom(queryResponse.error);
    }
    if (queryResponse.value.status == kStatusNoMoreFiles) {
      break;
    }

    entries.insert(entries.end(), queryResponse.value.entries.begin(),
                   queryResponse.value.entries.end());
    if (queryResponse.value.entries.empty()) {
      break;
    }
    firstQuery = false;
  }

  CloseRequestOptions closeOptions;
  closeOptions.fileId = createResponse.value.fileId;
  const CloseExchanger closer;
  const auto closeResponse =
      closer.close(transport, closeOptions, messageId + messagesUsed++, treeId,
                   sessionId, context);
  if (!closeResponse.ok) {
    return failureFrom(closeResponse.error);
  }

  DirectoryListResult result;
  result.directoryFileId = createResponse.value.fileId;
  result.entries = std::move(entries);
  result.messagesUsed = messagesUsed;
  return DecodeResult<DirectoryListResult>::success(result);
}

} // namespace smb::native_smb
