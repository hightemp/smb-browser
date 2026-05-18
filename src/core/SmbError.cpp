#include "core/SmbError.h"

namespace smb::core {

ConnectionStatus connectionStatusForSmbError(SmbErrorCode code) {
  switch (code) {
  case SmbErrorCode::None:
    return ConnectionStatus::Available;
  case SmbErrorCode::InvalidPath:
    return ConnectionStatus::Error;
  case SmbErrorCode::DnsError:
    return ConnectionStatus::DnsError;
  case SmbErrorCode::ServerUnavailable:
  case SmbErrorCode::NetworkError:
    return ConnectionStatus::ServerUnavailable;
  case SmbErrorCode::ShareUnavailable:
  case SmbErrorCode::FileNotFound:
    return ConnectionStatus::ShareUnavailable;
  case SmbErrorCode::AuthenticationFailed:
    return ConnectionStatus::AuthenticationFailed;
  case SmbErrorCode::PermissionDenied:
    return ConnectionStatus::PermissionDenied;
  case SmbErrorCode::Timeout:
    return ConnectionStatus::Timeout;
  case SmbErrorCode::ProtocolUnsupported:
    return ConnectionStatus::ProtocolUnsupported;
  case SmbErrorCode::AlreadyExists:
  case SmbErrorCode::DirectoryNotEmpty:
  case SmbErrorCode::OperationCancelled:
  case SmbErrorCode::LocalIoError:
  case SmbErrorCode::StorageError:
  case SmbErrorCode::CredentialStoreUnavailable:
  case SmbErrorCode::CredentialNotFound:
  case SmbErrorCode::Unknown:
    return ConnectionStatus::Error;
  }

  return ConnectionStatus::Error;
}

QString toString(ConnectionStatus status) {
  switch (status) {
  case ConnectionStatus::Unknown:
    return QStringLiteral("unknown");
  case ConnectionStatus::NotChecked:
    return QStringLiteral("not_checked");
  case ConnectionStatus::Checking:
    return QStringLiteral("checking");
  case ConnectionStatus::Available:
    return QStringLiteral("available");
  case ConnectionStatus::ServerUnavailable:
    return QStringLiteral("server_unavailable");
  case ConnectionStatus::ShareUnavailable:
    return QStringLiteral("share_unavailable");
  case ConnectionStatus::AuthenticationFailed:
    return QStringLiteral("authentication_failed");
  case ConnectionStatus::PermissionDenied:
    return QStringLiteral("permission_denied");
  case ConnectionStatus::Timeout:
    return QStringLiteral("timeout");
  case ConnectionStatus::DnsError:
    return QStringLiteral("dns_error");
  case ConnectionStatus::ProtocolUnsupported:
    return QStringLiteral("protocol_unsupported");
  case ConnectionStatus::Error:
    return QStringLiteral("error");
  }

  return QStringLiteral("error");
}

} // namespace smb::core
