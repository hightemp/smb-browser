#include "DirectoryLister.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return context.cancellationToken != nullptr &&
         context.cancellationToken->isCancellationRequested();
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

  const auto createRequest = buildCreateRequest(
      directoryCreateOptions(path), messageId, treeId, sessionId);
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
  const auto queryRequest = buildQueryDirectoryRequest(
      queryOptions, messageId + 1, treeId, sessionId);
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

  DirectoryListResult result;
  result.directoryFileId = createResponse.value.fileId;
  result.entries = queryResponse.value.entries;
  return DecodeResult<DirectoryListResult>::success(result);
}

} // namespace smb::native_smb
