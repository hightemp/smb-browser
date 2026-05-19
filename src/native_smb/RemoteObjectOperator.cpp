#include "RemoteObjectOperator.h"

#include "CloseExchanger.h"
#include "SetInfoExchanger.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<RemoteObjectResult> cancelledResult(const char *message) {
  return DecodeResult<RemoteObjectResult>::failure(ErrorCode::Cancelled,
                                                  message);
}

DecodeResult<RemoteObjectResult> failureFrom(const ProtocolError &error) {
  return DecodeResult<RemoteObjectResult>::failure(error.code, error.message);
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
  options.desiredAccess = kFileReadAttributes;
  options.fileAttributes = kFileAttributeDirectory;
  options.shareAccess = kFileShareRead | kFileShareWrite | kFileShareDelete;
  options.createDisposition = kFileCreate;
  options.createOptions = kFileDirectoryFile;
  return options;
}

CreateRequestOptions openForMutationOptions(const std::string &path,
                                            bool directory) {
  CreateRequestOptions options;
  options.path = path;
  options.desiredAccess = kDeleteAccess | kFileReadAttributes |
                          kFileWriteAttributes;
  options.fileAttributes = kFileAttributeNormal;
  options.shareAccess = kFileShareRead | kFileShareWrite | kFileShareDelete;
  options.createDisposition = kFileOpen;
  options.createOptions = directory ? kFileDirectoryFile : 0;
  return options;
}

DecodeResult<CreateResponse> openObject(Transport &transport,
                                        const CreateRequestOptions &options,
                                        std::uint64_t messageId,
                                        std::uint32_t treeId,
                                        std::uint64_t sessionId,
                                        const OperationContext &context) {
  const auto createRequest =
      buildCreateRequest(options, messageId, treeId, sessionId);
  const auto createPayload = exchangePayload(transport, createRequest, context);
  if (!createPayload.ok) {
    return DecodeResult<CreateResponse>::failure(createPayload.error.code,
                                                createPayload.error.message);
  }
  return decodeCreateResponse(createPayload.value);
}

DecodeResult<RemoteObjectResult>
closeAndReturn(Transport &transport, const FileId &fileId,
               std::uint64_t messageId, std::uint32_t treeId,
               std::uint64_t sessionId, const OperationContext &context) {
  CloseRequestOptions closeOptions;
  closeOptions.fileId = fileId;
  const CloseExchanger closer;
  const auto closeResponse =
      closer.close(transport, closeOptions, messageId, treeId, sessionId,
                   context);
  if (!closeResponse.ok) {
    return failureFrom(closeResponse.error);
  }

  RemoteObjectResult result;
  result.fileId = fileId;
  return DecodeResult<RemoteObjectResult>::success(result);
}

} // namespace

DecodeResult<RemoteObjectResult> RemoteObjectOperator::createDirectory(
    Transport &transport, const std::string &path, std::uint64_t messageId,
    std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult("SMB create directory was cancelled.");
  }

  const auto createResponse =
      openObject(transport, directoryCreateOptions(path), messageId, treeId,
                 sessionId, context);
  if (!createResponse.ok) {
    return failureFrom(createResponse.error);
  }

  if (isCancelled(context)) {
    return cancelledResult("SMB create directory was cancelled.");
  }

  return closeAndReturn(transport, createResponse.value.fileId, messageId + 1,
                        treeId, sessionId, context);
}

DecodeResult<RemoteObjectResult> RemoteObjectOperator::deleteObject(
    Transport &transport, const std::string &path, bool directory,
    std::uint64_t messageId, std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult("SMB delete was cancelled.");
  }

  const auto createResponse = openObject(
      transport, openForMutationOptions(path, directory), messageId, treeId,
      sessionId, context);
  if (!createResponse.ok) {
    return failureFrom(createResponse.error);
  }

  if (isCancelled(context)) {
    return cancelledResult("SMB delete was cancelled.");
  }

  SetInfoRequestOptions setInfoOptions;
  setInfoOptions.fileId = createResponse.value.fileId;
  setInfoOptions.infoType = kInfoTypeFile;
  setInfoOptions.fileInfoClass = kFileDispositionInformation;
  setInfoOptions.buffer = buildFileDispositionInformation(true);

  const SetInfoExchanger setInfo;
  const auto setInfoResponse =
      setInfo.setInfo(transport, setInfoOptions, messageId + 1, treeId,
                      sessionId, context);
  if (!setInfoResponse.ok) {
    return failureFrom(setInfoResponse.error);
  }

  return closeAndReturn(transport, createResponse.value.fileId, messageId + 2,
                        treeId, sessionId, context);
}

DecodeResult<RemoteObjectResult> RemoteObjectOperator::renameObject(
    Transport &transport, const std::string &fromPath,
    const std::string &toPath, bool replaceIfExists, std::uint64_t messageId,
    std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult("SMB rename was cancelled.");
  }

  const auto createResponse =
      openObject(transport, openForMutationOptions(fromPath, false), messageId,
                 treeId, sessionId, context);
  if (!createResponse.ok) {
    return failureFrom(createResponse.error);
  }

  if (isCancelled(context)) {
    return cancelledResult("SMB rename was cancelled.");
  }

  SetInfoRequestOptions setInfoOptions;
  setInfoOptions.fileId = createResponse.value.fileId;
  setInfoOptions.infoType = kInfoTypeFile;
  setInfoOptions.fileInfoClass = kFileRenameInformation;
  setInfoOptions.buffer =
      buildFileRenameInformation(toPath, replaceIfExists);

  const SetInfoExchanger setInfo;
  const auto setInfoResponse =
      setInfo.setInfo(transport, setInfoOptions, messageId + 1, treeId,
                      sessionId, context);
  if (!setInfoResponse.ok) {
    return failureFrom(setInfoResponse.error);
  }

  return closeAndReturn(transport, createResponse.value.fileId, messageId + 2,
                        treeId, sessionId, context);
}

} // namespace smb::native_smb
