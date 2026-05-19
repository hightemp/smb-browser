#include "TreeConnector.h"

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<TreeConnectResult> cancelledResult() {
  return DecodeResult<TreeConnectResult>::failure(
      ErrorCode::Cancelled, "SMB tree connect was cancelled.");
}

DecodeResult<TreeConnectResult> failureFrom(const ProtocolError &error) {
  return DecodeResult<TreeConnectResult>::failure(error.code, error.message);
}

} // namespace

DecodeResult<TreeConnectResult>
TreeConnector::connect(Transport &transport,
                       const TreeConnectRequestOptions &options,
                       std::uint64_t messageId, std::uint64_t sessionId,
                       const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto requestFrame =
      encodeDirectTcpFrame(buildTreeConnectRequest(options, messageId,
                                                  sessionId));
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

  const auto response = decodeTreeConnectResponse(payload.value);
  if (!response.ok) {
    return failureFrom(response.error);
  }

  const auto header = decodeSmb2SyncHeader(payload.value);
  if (!header.ok) {
    return failureFrom(header.error);
  }

  TreeConnectResult result;
  result.treeId = header.value.treeId;
  result.shareType = response.value.shareType;
  result.isDfs = response.value.isDfs;
  result.isDfsRoot = response.value.isDfsRoot;
  result.requiresEncryption = response.value.requiresEncryption;
  result.maximalAccess = response.value.maximalAccess;
  return DecodeResult<TreeConnectResult>::success(result);
}

} // namespace smb::native_smb
