#include "QueryInfoExchanger.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return context.cancellationToken != nullptr &&
         context.cancellationToken->isCancellationRequested();
}

DecodeResult<QueryInfoResponse> cancelledResult() {
  return DecodeResult<QueryInfoResponse>::failure(
      ErrorCode::Cancelled, "SMB query info was cancelled.");
}

DecodeResult<QueryInfoResponse> failureFrom(const ProtocolError &error) {
  return DecodeResult<QueryInfoResponse>::failure(error.code, error.message);
}

} // namespace

DecodeResult<QueryInfoResponse> QueryInfoExchanger::queryInfo(
    Transport &transport, const QueryInfoRequestOptions &options,
    std::uint64_t messageId, std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto request =
      buildQueryInfoRequest(options, messageId, treeId, sessionId);
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

  const auto response = decodeQueryInfoResponse(payload.value);
  if (!response.ok) {
    return failureFrom(response.error);
  }
  return response;
}

} // namespace smb::native_smb
