#pragma once

#include "Protocol.h"

namespace smb::native_smb {

ByteVector buildSpnegoNegTokenInit(const ByteVector &ntlmToken);
ByteVector buildSpnegoNegTokenResp(const ByteVector &ntlmToken);
DecodeResult<ByteVector> unwrapSpnegoNtlmToken(const ByteVector &token);

} // namespace smb::native_smb
