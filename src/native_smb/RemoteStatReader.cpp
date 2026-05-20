#include "RemoteStatReader.h"

#include "CloseExchanger.h"
#include "QueryInfoExchanger.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<RemoteStatResult> cancelledResult() {
  return DecodeResult<RemoteStatResult>::failure(ErrorCode::Cancelled,
                                                "SMB stat was cancelled.");
}

DecodeResult<RemoteStatResult> failureFrom(const ProtocolError &error,
                                           std::uint64_t messagesUsed) {
  return DecodeResult<RemoteStatResult>::failure(error.code, error.message,
                                                messagesUsed);
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

CreateRequestOptions statCreateOptions(const std::string &path) {
  CreateRequestOptions options;
  options.path = path;
  options.desiredAccess = kFileReadAttributes;
  options.fileAttributes = kFileAttributeNormal;
  options.shareAccess = kFileShareRead | kFileShareWrite | kFileShareDelete;
  options.createDisposition = kFileOpen;
  options.createOptions = 0;
  return options;
}

QueryInfoRequestOptions queryOptions(const FileId &fileId,
                                     std::uint8_t fileInfoClass,
                                     std::uint32_t outputBufferLength) {
  QueryInfoRequestOptions options;
  options.fileId = fileId;
  options.infoType = kInfoTypeFile;
  options.fileInfoClass = fileInfoClass;
  options.outputBufferLength = outputBufferLength;
  return options;
}

} // namespace

DecodeResult<RemoteStatResult>
RemoteStatReader::stat(Transport &transport, const std::string &path,
                       std::uint64_t messageId, std::uint32_t treeId,
                       std::uint64_t sessionId,
                       const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto createRequest =
      buildCreateRequest(statCreateOptions(path), messageId, treeId,
                         sessionId);
  const auto createPayload = exchangePayload(transport, createRequest, context);
  if (!createPayload.ok) {
    return DecodeResult<RemoteStatResult>::failure(createPayload.error.code,
                                                  createPayload.error.message,
                                                  1);
  }

  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto createResponse = decodeCreateResponse(createPayload.value);
  if (!createResponse.ok) {
    return failureFrom(createResponse.error, 1);
  }

  const QueryInfoExchanger queryInfo;
  const auto basicResponse = queryInfo.queryInfo(
      transport,
      queryOptions(createResponse.value.fileId, kFileBasicInformation, 40),
      messageId + 1, treeId, sessionId, context);
  if (!basicResponse.ok) {
    return failureFrom(basicResponse.error, 2);
  }

  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto basicInfo = decodeFileBasicInformation(basicResponse.value.buffer);
  if (!basicInfo.ok) {
    return failureFrom(basicInfo.error, 2);
  }

  const auto standardResponse = queryInfo.queryInfo(
      transport,
      queryOptions(createResponse.value.fileId, kFileStandardInformation, 24),
      messageId + 2, treeId, sessionId, context);
  if (!standardResponse.ok) {
    return failureFrom(standardResponse.error, 3);
  }

  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto standardInfo =
      decodeFileStandardInformation(standardResponse.value.buffer);
  if (!standardInfo.ok) {
    return failureFrom(standardInfo.error, 3);
  }

  CloseRequestOptions closeOptions;
  closeOptions.fileId = createResponse.value.fileId;
  const CloseExchanger closer;
  const auto closeResponse = closer.close(transport, closeOptions,
                                         messageId + 3, treeId, sessionId,
                                         context);
  if (!closeResponse.ok) {
    return failureFrom(closeResponse.error, 4);
  }

  RemoteStatResult result;
  result.fileId = createResponse.value.fileId;
  result.creationTime = basicInfo.value.creationTime;
  result.lastAccessTime = basicInfo.value.lastAccessTime;
  result.lastWriteTime = basicInfo.value.lastWriteTime;
  result.changeTime = basicInfo.value.changeTime;
  result.fileAttributes = basicInfo.value.fileAttributes;
  result.allocationSize = standardInfo.value.allocationSize;
  result.endOfFile = standardInfo.value.endOfFile;
  result.numberOfLinks = standardInfo.value.numberOfLinks;
  result.messagesUsed = 4;
  result.deletePending = standardInfo.value.deletePending;
  result.directory = standardInfo.value.directory;
  result.reparsePoint =
      (basicInfo.value.fileAttributes & kFileAttributeReparsePoint) != 0 ||
      createResponse.value.isReparsePoint;
  return DecodeResult<RemoteStatResult>::success(result);
}

} // namespace smb::native_smb
