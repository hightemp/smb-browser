#pragma once

#include "core/Error.h"

namespace smb::infrastructure {

enum class Libsmb2ErrorContext {
  Connection,
  Directory,
  FileOperation,
};

smb::core::ErrorCode mapLibsmb2Error(int status, const QString &details,
                                     Libsmb2ErrorContext context);

} // namespace smb::infrastructure
