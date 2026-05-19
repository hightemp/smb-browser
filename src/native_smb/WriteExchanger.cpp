#include "WriteExchanger.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return context.cancellationToken != nullptr &&
         context.cancellationToken->isCancellationRequested();
}

DecodeResult<WriteResponse> cancelledResult() {
  return DecodeResult<WriteResponse>::failure(ErrorCode::Cancelled,
                                             "SMB write was cancelled.");
}

DecodeResult<WriteResponse> failureFrom(const ProtocolError &error) {
  return DecodeResult<WriteResponse>::failure(error.code, error.message);
}

} // namespace

DecodeResult<WriteResponse>
WriteExchanger::write(Transport &transport, const WriteRequestOptions &options,
                      std::uint64_t messageId, std::uint32_t treeId,
                      std::uint64_t sessionId,
                      const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto request = buildWriteRequest(options, messageId, treeId, sessionId);
  const auto responseFrame =
      transport.exchange(encodeDirectTcpFrame(request), context);
  if (!responseFrame.ok) {
    return failureFrom(responseFrame.error);
  }

  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto payload = decodeDirectTcpPayload(responseFrame.value);
  if (!payload.ok) {
    return failureFrom(payload.error);
  }

  const auto response = decodeWriteResponse(payload.value);
  if (!response.ok) {
    return failureFrom(response.error);
  }
  return response;
}

} // namespace smb::native_smb
