#pragma once

#include "Protocol.h"
#include "SmbNative.h"

namespace smb::native_smb {

class Transport {
public:
  virtual ~Transport() = default;

  virtual DecodeResult<ByteVector>
  exchange(const ByteVector &requestFrame, const OperationContext &context) = 0;
};

} // namespace smb::native_smb
