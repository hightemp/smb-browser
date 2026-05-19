#include "Negotiator.h"

#include <algorithm>

namespace smb::native_smb {
namespace {

bool isCancelled(const OperationContext &context) {
  return isCancellationRequested(context);
}

DecodeResult<NegotiatedConnection> cancelledResult() {
  return DecodeResult<NegotiatedConnection>::failure(
      ErrorCode::Cancelled, "SMB negotiation was cancelled.");
}

DecodeResult<NegotiatedConnection> failureFrom(const ProtocolError &error) {
  return DecodeResult<NegotiatedConnection>::failure(error.code,
                                                    error.message);
}

bool hasCapability(std::uint32_t capabilities, GlobalCapability capability) {
  return (capabilities & static_cast<std::uint32_t>(capability)) != 0;
}

} // namespace

DecodeResult<NegotiatedConnection>
Negotiator::negotiate(Transport &transport,
                      const NegotiateRequestOptions &options,
                      const OperationContext &context) const {
  if (isCancelled(context)) {
    return cancelledResult();
  }

  const auto dialects =
      options.dialects.empty() ? defaultInitialDialects() : options.dialects;
  const auto requestFrame =
      encodeDirectTcpFrame(buildNegotiateRequest(options, 0));
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

  const auto negotiateResponse = decodeNegotiateResponse(payload.value, dialects);
  if (!negotiateResponse.ok) {
    return failureFrom(negotiateResponse.error);
  }

  NegotiatedConnection connection;
  connection.dialect = negotiateResponse.value.dialect;
  connection.signingRequired =
      (negotiateResponse.value.securityMode & 0x0002) != 0 ||
      options.signing == SecurityPolicy::Required;
  connection.encryptionSupported = hasCapability(
      negotiateResponse.value.capabilities, GlobalCapability::Encryption);
  connection.capabilities = negotiateResponse.value.capabilities;
  connection.maxReadSize = negotiateResponse.value.maxReadSize;
  connection.maxWriteSize = negotiateResponse.value.maxWriteSize;
  connection.securityBuffer = negotiateResponse.value.securityBuffer;
  return DecodeResult<NegotiatedConnection>::success(connection);
}

} // namespace smb::native_smb
