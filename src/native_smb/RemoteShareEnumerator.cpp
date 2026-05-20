#include "RemoteShareEnumerator.h"

#include "CloseExchanger.h"
#include "Dcerpc.h"
#include "ReadExchanger.h"
#include "SrvsRpc.h"
#include "WriteExchanger.h"

#include <sstream>
#include <utility>

namespace smb::native_smb {
namespace {

constexpr std::uint32_t kPipeReadSize = 64 * 1024;

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

DecodeResult<NativeShareList> failure(ErrorCode code, std::string message,
                                      std::uint64_t messagesUsed) {
  return DecodeResult<NativeShareList>::failure(
      code, std::move(message), messagesUsed);
}

DecodeResult<NativeShareList> failureFrom(const ProtocolError &error,
                                          std::uint64_t messagesUsed) {
  return failure(error.code, error.message, messagesUsed);
}

CreateRequestOptions srvsvcPipeOpenOptions() {
  CreateRequestOptions options;
  options.path = "srvsvc";
  options.desiredAccess = kFileReadData | kFileWriteData |
                          kFileReadAttributes | kFileWriteAttributes |
                          kSynchronizeAccess;
  options.fileAttributes = kFileAttributeNormal;
  options.shareAccess = kFileShareRead | kFileShareWrite;
  options.createDisposition = kFileOpen;
  options.createOptions = 0;
  return options;
}

DecodeResult<bool> writePipe(Transport &transport, const FileId &fileId,
                             const ByteVector &data, std::uint64_t messageId,
                             std::uint32_t treeId, std::uint64_t sessionId,
                             const OperationContext &context) {
  WriteRequestOptions options;
  options.fileId = fileId;
  options.data = data;
  const WriteExchanger writer;
  const auto written =
      writer.write(transport, options, messageId, treeId, sessionId, context);
  if (!written.ok) {
    return DecodeResult<bool>::failure(written.error.code,
                                       written.error.message);
  }
  if (written.value.count != data.size()) {
    return DecodeResult<bool>::failure(
        ErrorCode::IoError, "SMB named pipe write was shorter than expected.");
  }
  return DecodeResult<bool>::success(true);
}

DecodeResult<ByteVector> readPipe(Transport &transport, const FileId &fileId,
                                  std::uint64_t messageId,
                                  std::uint32_t treeId,
                                  std::uint64_t sessionId,
                                  const OperationContext &context) {
  ReadRequestOptions options;
  options.fileId = fileId;
  options.length = kPipeReadSize;
  const ReadExchanger reader;
  const auto read =
      reader.read(transport, options, messageId, treeId, sessionId, context);
  if (!read.ok) {
    return DecodeResult<ByteVector>::failure(read.error.code,
                                             read.error.message);
  }
  return DecodeResult<ByteVector>::success(read.value.data);
}

DecodeResult<bool> closePipe(Transport &transport, const FileId &fileId,
                             std::uint64_t messageId, std::uint32_t treeId,
                             std::uint64_t sessionId,
                             const OperationContext &context) {
  CloseRequestOptions options;
  options.fileId = fileId;
  const CloseExchanger closer;
  const auto closed =
      closer.close(transport, options, messageId, treeId, sessionId, context);
  if (!closed.ok) {
    return DecodeResult<bool>::failure(closed.error.code,
                                       closed.error.message);
  }
  return DecodeResult<bool>::success(true);
}

std::string typeName(SrvsShareKind kind) {
  switch (kind) {
  case SrvsShareKind::Disk:
    return "disk";
  case SrvsShareKind::Printer:
    return "printer";
  case SrvsShareKind::Device:
    return "device";
  case SrvsShareKind::Ipc:
    return "ipc";
  case SrvsShareKind::Unknown:
    return "unknown";
  }
  return "unknown";
}

NativeShareInfo toNativeShare(const SrvsShareInfo &share) {
  NativeShareInfo result;
  result.name = share.name;
  result.rawType = share.rawType;
  result.type = typeName(share.kind);
  result.comment = share.comment;
  result.hidden = share.hidden;
  result.special = share.special;
  result.temporary = share.temporary;
  return result;
}

} // namespace

DecodeResult<NativeShareList> RemoteShareEnumerator::listShares(
    Transport &transport, const std::string &serverName,
    std::uint64_t messageId, std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancellationRequested(context)) {
    return failure(ErrorCode::Cancelled,
                   "SMB share enumeration was cancelled.", 0);
  }

  std::uint64_t messagesUsed = 0;
  const auto createPayload = exchangePayload(
      transport,
      buildCreateRequest(srvsvcPipeOpenOptions(), messageId + messagesUsed++,
                         treeId, sessionId),
      context);
  if (!createPayload.ok) {
    return failureFrom(createPayload.error, messagesUsed);
  }

  const auto create = decodeCreateResponse(createPayload.value);
  if (!create.ok) {
    return failureFrom(create.error, messagesUsed);
  }

  const auto closeOpenedPipe = [&]() {
    OperationContext cleanupContext;
    cleanupContext.timeout = context.timeout;
    (void)closePipe(transport, create.value.fileId,
                    messageId + messagesUsed++, treeId, sessionId,
                    cleanupContext);
  };

  const auto bindPdu = buildDcerpcBindPdu(srvsRpcSyntax(), 1);
  const auto bindWrite =
      writePipe(transport, create.value.fileId, bindPdu,
                messageId + messagesUsed++, treeId, sessionId, context);
  if (!bindWrite.ok) {
    closeOpenedPipe();
    return failureFrom(bindWrite.error, messagesUsed);
  }

  const auto bindBytes =
      readPipe(transport, create.value.fileId, messageId + messagesUsed++,
               treeId, sessionId, context);
  if (!bindBytes.ok) {
    closeOpenedPipe();
    return failureFrom(bindBytes.error, messagesUsed);
  }
  const auto bindAck = decodeDcerpcBindAck(bindBytes.value);
  if (!bindAck.ok) {
    closeOpenedPipe();
    return failureFrom(bindAck.error, messagesUsed);
  }

  const auto requestStub = buildNetrShareEnumRequestStub(serverName);
  const auto requestPdu =
      buildDcerpcRequestPdu(kSrvsNetrShareEnumOpnum, requestStub, 2);
  const auto requestWrite =
      writePipe(transport, create.value.fileId, requestPdu,
                messageId + messagesUsed++, treeId, sessionId, context);
  if (!requestWrite.ok) {
    closeOpenedPipe();
    return failureFrom(requestWrite.error, messagesUsed);
  }

  const auto responseBytes =
      readPipe(transport, create.value.fileId, messageId + messagesUsed++,
               treeId, sessionId, context);
  if (!responseBytes.ok) {
    closeOpenedPipe();
    return failureFrom(responseBytes.error, messagesUsed);
  }
  const auto rpcResponse = decodeDcerpcResponse(responseBytes.value);
  if (!rpcResponse.ok) {
    closeOpenedPipe();
    return failureFrom(rpcResponse.error, messagesUsed);
  }
  const auto shareEnum =
      decodeNetrShareEnumResponseStub(rpcResponse.value.stubData);
  if (!shareEnum.ok) {
    closeOpenedPipe();
    return failureFrom(shareEnum.error, messagesUsed);
  }

  const auto close =
      closePipe(transport, create.value.fileId, messageId + messagesUsed++,
                treeId, sessionId, context);
  if (!close.ok) {
    return failureFrom(close.error, messagesUsed);
  }

  NativeShareList result;
  result.totalEntries = shareEnum.value.totalEntries;
  result.resumeHandle = shareEnum.value.resumeHandle;
  result.moreData = shareEnum.value.moreData;
  result.messagesUsed = messagesUsed;
  result.shares.reserve(shareEnum.value.shares.size());
  for (const auto &share : shareEnum.value.shares) {
    result.shares.push_back(toNativeShare(share));
  }
  return DecodeResult<NativeShareList>::success(std::move(result));
}

} // namespace smb::native_smb
