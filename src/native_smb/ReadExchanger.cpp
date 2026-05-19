#include "ReadExchanger.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return context.cancellationToken != nullptr &&
         context.cancellationToken->isCancellationRequested();
}

DecodeResult<ReadResponse> cancelledResult() {
  return DecodeResult<ReadResponse>::failure(ErrorCode::Cancelled,
                                            "SMB read was cancelled.");
}

DecodeResult<ReadResponse> failureFrom(const ProtocolError &error) {
  return DecodeResult<ReadResponse>::failure(error.code, error.message);
}

} // namespace

DecodeResult<ReadResponse>
ReadExchanger::read(Transport &transport, const ReadRequestOptions &options,
                    std::uint64_t messageId, std::uint32_t treeId,
                    std::uint64_t sessionId,
                    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto request = buildReadRequest(options, messageId, treeId, sessionId);
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

  const auto response = decodeReadResponse(payload.value);
  if (!response.ok) {
    return failureFrom(response.error);
  }
  return response;
}

} // namespace smb::native_smb
