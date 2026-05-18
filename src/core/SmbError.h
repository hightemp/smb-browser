#pragma once

#include "core/Error.h"

namespace smb::core {

using SmbErrorCode = ErrorCode;

enum class ConnectionStatus {
  Unknown,
  NotChecked,
  Checking,
  Available,
  ServerUnavailable,
  ShareUnavailable,
  AuthenticationFailed,
  PermissionDenied,
  Timeout,
  DnsError,
  ProtocolUnsupported,
  Error,
};

ConnectionStatus connectionStatusForSmbError(SmbErrorCode code);
QString toString(ConnectionStatus status);

} // namespace smb::core
