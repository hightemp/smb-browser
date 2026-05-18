#pragma once

#include "core/Error.h"
#include "core/LogSanitizer.h"

namespace smb::infrastructure {

enum class Libsmb2ErrorContext {
  Connection,
  Directory,
  FileOperation,
};

smb::core::ErrorCode mapLibsmb2Error(int status, const QString &details,
                                     Libsmb2ErrorContext context);

smb::core::AppError makeLibsmb2Error(
    int status, const QString &details, Libsmb2ErrorContext context,
    const smb::core::LogSanitizer &sanitizer = smb::core::LogSanitizer());

} // namespace smb::infrastructure
