#include "SetInfoExchanger.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<SetInfoResponse> cancelledResult() {
  return DecodeResult<SetInfoResponse>::failure(ErrorCode::Cancelled,
                                                "SMB set info was cancelled.");
}

DecodeResult<SetInfoResponse> failureFrom(const ProtocolError &error) {
  return DecodeResult<SetInfoResponse>::failure(error.code, error.message);
}

} // namespace

DecodeResult<SetInfoResponse>
SetInfoExchanger::setInfo(Transport &transport,
                          const SetInfoRequestOptions &options,
                          std::uint64_t messageId, std::uint32_t treeId,
                          std::uint64_t sessionId,
                          const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto request =
      buildSetInfoRequest(options, messageId, treeId, sessionId);
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

  const auto response = decodeSetInfoResponse(payload.value);
  if (!response.ok) {
    return failureFrom(response.error);
  }
  return response;
}

} // namespace smb::native_smb
