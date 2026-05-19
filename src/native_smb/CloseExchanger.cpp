#include "CloseExchanger.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<CloseResponse> cancelledResult() {
  return DecodeResult<CloseResponse>::failure(ErrorCode::Cancelled,
                                             "SMB close was cancelled.");
}

DecodeResult<CloseResponse> failureFrom(const ProtocolError &error) {
  return DecodeResult<CloseResponse>::failure(error.code, error.message);
}

} // namespace

DecodeResult<CloseResponse>
CloseExchanger::close(Transport &transport, const CloseRequestOptions &options,
                      std::uint64_t messageId, std::uint32_t treeId,
                      std::uint64_t sessionId,
                      const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto request =
      buildCloseRequest(options, messageId, treeId, sessionId);
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

  const auto response = decodeCloseResponse(payload.value);
  if (!response.ok) {
    return failureFrom(response.error);
  }
  return response;
}

} // namespace smb::native_smb
