#include "FileWriter.h"

#include "CloseExchanger.h"
#include "WriteExchanger.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<FileWriteResult> cancelledResult() {
  return DecodeResult<FileWriteResult>::failure(ErrorCode::Cancelled,
                                               "SMB file write was cancelled.");
}

DecodeResult<FileWriteResult> failureFrom(const ProtocolError &error) {
  return DecodeResult<FileWriteResult>::failure(error.code, error.message);
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

CreateRequestOptions fileCreateOptions(const std::string &path) {
  CreateRequestOptions options;
  options.path = path;
  options.desiredAccess = kFileWriteData | kFileWriteEa |
                          kFileWriteAttributes | kFileReadAttributes;
  options.fileAttributes = kFileAttributeNormal;
  options.shareAccess = kFileShareRead | kFileShareWrite | kFileShareDelete;
  options.createDisposition = kFileOpenIf;
  options.createOptions = kFileNonDirectoryFile;
  return options;
}

} // namespace

DecodeResult<FileWriteResult>
FileWriter::writeOnce(Transport &transport, const std::string &path,
                      const ByteVector &data, std::uint64_t offset,
                      std::uint64_t messageId, std::uint32_t treeId,
                      std::uint64_t sessionId,
                      const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto createRequest =
      buildCreateRequest(fileCreateOptions(path), messageId, treeId, sessionId);
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

  WriteRequestOptions writeOptions;
  writeOptions.fileId = createResponse.value.fileId;
  writeOptions.data = data;
  writeOptions.offset = offset;
  const WriteExchanger writer;
  const auto writeResponse =
      writer.write(transport, writeOptions, messageId + 1, treeId, sessionId,
                   context);
  if (!writeResponse.ok) {
    return failureFrom(writeResponse.error);
  }

  CloseRequestOptions closeOptions;
  closeOptions.fileId = createResponse.value.fileId;
  const CloseExchanger closer;
  const auto closeResponse = closer.close(transport, closeOptions, messageId + 2,
                                         treeId, sessionId, context);
  if (!closeResponse.ok) {
    return failureFrom(closeResponse.error);
  }

  FileWriteResult result;
  result.fileId = createResponse.value.fileId;
  result.bytesWritten = writeResponse.value.count;
  result.remaining = writeResponse.value.remaining;
  return DecodeResult<FileWriteResult>::success(result);
}

} // namespace smb::native_smb
