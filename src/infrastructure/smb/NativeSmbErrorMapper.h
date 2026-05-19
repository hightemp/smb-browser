#pragma once

#include "Protocol.h"
#include "core/Error.h"
#include "core/LogSanitizer.h"

namespace smb::infrastructure {

smb::core::ErrorCode
nativeToCoreError(smb::native_smb::ErrorCode code);

bool isNativeErrorRetryable(smb::native_smb::ErrorCode code);

smb::core::AppError makeNativeSmbError(
    const smb::native_smb::ProtocolError &error,
    const smb::core::LogSanitizer &sanitizer = smb::core::LogSanitizer());

} // namespace smb::infrastructure
