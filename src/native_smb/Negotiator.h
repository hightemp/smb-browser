#pragma once

#include "Protocol.h"
#include "Transport.h"

namespace smb::native_smb {

struct NegotiatedConnection {
  Dialect dialect = Dialect::Smb202;
  bool signingRequired = false;
  bool encryptionSupported = false;
  std::uint32_t capabilities = 0;
  std::uint32_t maxReadSize = 0;
  std::uint32_t maxWriteSize = 0;
  ByteVector securityBuffer;
};

class Negotiator {
public:
  DecodeResult<NegotiatedConnection>
  negotiate(Transport &transport, const NegotiateRequestOptions &options,
            const OperationContext &context) const;
};

} // namespace smb::native_smb
