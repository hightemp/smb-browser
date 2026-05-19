#include "FileReader.h"

#include "CloseExchanger.h"
#include "ReadExchanger.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<FileReadResult> cancelledResult() {
  return DecodeResult<FileReadResult>::failure(ErrorCode::Cancelled,
                                              "SMB file read was cancelled.");
}

DecodeResult<FileReadResult> failureFrom(const ProtocolError &error) {
  return DecodeResult<FileReadResult>::failure(error.code, error.message);
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
  options.desiredAccess = kFileReadData | kFileReadEa | kFileReadAttributes;
  options.fileAttributes = kFileAttributeNormal;
  options.shareAccess = kFileShareRead | kFileShareWrite | kFileShareDelete;
  options.createDisposition = kFileOpen;
  options.createOptions = kFileNonDirectoryFile;
  return options;
}

} // namespace

DecodeResult<FileReadResult>
FileReader::readOnce(Transport &transport, const std::string &path,
                     std::uint32_t length, std::uint64_t offset,
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

  ReadRequestOptions readOptions;
  readOptions.fileId = createResponse.value.fileId;
  readOptions.length = length;
  readOptions.offset = offset;
  const ReadExchanger reader;
  const auto readResponse =
      reader.read(transport, readOptions, messageId + 1, treeId, sessionId,
                  context);
  if (!readResponse.ok) {
    return failureFrom(readResponse.error);
  }

  CloseRequestOptions closeOptions;
  closeOptions.fileId = createResponse.value.fileId;
  const CloseExchanger closer;
  const auto closeResponse = closer.close(transport, closeOptions, messageId + 2,
                                         treeId, sessionId, context);
  if (!closeResponse.ok) {
    return failureFrom(closeResponse.error);
  }

  FileReadResult result;
  result.fileId = createResponse.value.fileId;
  result.data = readResponse.value.data;
  result.dataRemaining = readResponse.value.dataRemaining;
  return DecodeResult<FileReadResult>::success(result);
}

} // namespace smb::native_smb
