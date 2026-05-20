#include "SessionSetupExchanger.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<SessionSetupResponse> cancelledResult() {
  return DecodeResult<SessionSetupResponse>::failure(
      ErrorCode::Cancelled, "SMB session setup was cancelled.");
}

DecodeResult<SessionSetupResponse> failureFrom(const ProtocolError &error) {
  return DecodeResult<SessionSetupResponse>::failure(error.code,
                                                    error.message);
}

} // namespace

DecodeResult<SessionSetupResponse>
SessionSetupExchanger::exchange(Transport &transport,
                                const SessionSetupRequestOptions &options,
                                std::uint64_t messageId,
                                std::uint64_t sessionId,
                                const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto requestPayload =
      buildSessionSetupRequest(options, messageId, sessionId);
  const auto requestFrame = encodeDirectTcpFrame(requestPayload);
  const auto responseFrame = transport.exchange(requestFrame, context);
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

  auto response = decodeSessionSetupResponse(payload.value);
  if (!response.ok) {
    return failureFrom(response.error);
  }
  response.value.requestPayload = requestPayload;
  response.value.responsePayload = payload.value;
  return response;
}

} // namespace smb::native_smb
