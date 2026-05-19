#include "ChangeNotifyExchanger.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<ChangeNotifyResponse> cancelledResult() {
  return DecodeResult<ChangeNotifyResponse>::failure(
      ErrorCode::Cancelled, "SMB change notify was cancelled.");
}

DecodeResult<ChangeNotifyResponse> failureFrom(const ProtocolError &error) {
  return DecodeResult<ChangeNotifyResponse>::failure(error.code,
                                                     error.message);
}

} // namespace

DecodeResult<ChangeNotifyResponse> ChangeNotifyExchanger::wait(
    Transport &transport, const ChangeNotifyRequestOptions &options,
    std::uint64_t messageId, std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto request =
      buildChangeNotifyRequest(options, messageId, treeId, sessionId);
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

  const auto response = decodeChangeNotifyResponse(payload.value);
  if (!response.ok) {
    return failureFrom(response.error);
  }
  return response;
}

} // namespace smb::native_smb
