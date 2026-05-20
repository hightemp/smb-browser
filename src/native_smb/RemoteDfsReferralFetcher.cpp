#include "RemoteDfsReferralFetcher.h"

#include <limits>

namespace smb::native_smb {
namespace {

constexpr std::uint32_t kDfsReferralOutputSize = 64 * 1024;

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

DecodeResult<NativeDfsReferralResult> failure(ErrorCode code,
                                              std::string message,
                                              std::uint64_t messagesUsed) {
  return DecodeResult<NativeDfsReferralResult>::failure(
      code, std::move(message), messagesUsed);
}

DecodeResult<NativeDfsReferralResult> failureFrom(const ProtocolError &error,
                                                  std::uint64_t messagesUsed) {
  return failure(error.code, error.message, messagesUsed);
}

} // namespace

DecodeResult<NativeDfsReferralResult> RemoteDfsReferralFetcher::getReferrals(
    Transport &transport, const std::string &requestPath,
    std::uint64_t messageId, std::uint32_t treeId, std::uint64_t sessionId,
    const OperationContext &context) const {
  if (isCancellationRequested(context)) {
    return failure(ErrorCode::Cancelled,
                   "SMB DFS referral request was cancelled.", 0);
  }

  IoctlRequestOptions options;
  options.ctlCode = kFsctlDfsGetReferrals;
  options.fileId.persistent = std::numeric_limits<std::uint64_t>::max();
  options.fileId.volatileId = std::numeric_limits<std::uint64_t>::max();
  options.input = buildDfsGetReferralRequest(requestPath);
  options.maxOutputResponse = kDfsReferralOutputSize;
  options.flags = kIoctlIsFsctl;

  const auto payload = exchangePayload(
      transport, buildIoctlRequest(options, messageId, treeId, sessionId),
      context);
  if (!payload.ok) {
    return failureFrom(payload.error, 1);
  }
  const auto ioctl = decodeIoctlResponse(payload.value);
  if (!ioctl.ok) {
    return failureFrom(ioctl.error, 1);
  }
  const auto referrals = decodeDfsReferralResponse(ioctl.value.output);
  if (!referrals.ok) {
    return failureFrom(referrals.error, 1);
  }

  NativeDfsReferralResult result;
  result.response = referrals.value;
  result.messagesUsed = 1;
  return DecodeResult<NativeDfsReferralResult>::success(std::move(result));
}

} // namespace smb::native_smb
