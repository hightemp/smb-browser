#include "smb/NativeSmbErrorMapper.h"

#include <QString>

namespace smb::infrastructure {

smb::core::ErrorCode nativeToCoreError(smb::native_smb::ErrorCode code) {
  switch (code) {
  case smb::native_smb::ErrorCode::None:
    return smb::core::ErrorCode::None;
  case smb::native_smb::ErrorCode::Cancelled:
    return smb::core::ErrorCode::OperationCancelled;
  case smb::native_smb::ErrorCode::Timeout:
    return smb::core::ErrorCode::Timeout;
  case smb::native_smb::ErrorCode::DnsError:
    return smb::core::ErrorCode::DnsError;
  case smb::native_smb::ErrorCode::ServerUnavailable:
    return smb::core::ErrorCode::ServerUnavailable;
  case smb::native_smb::ErrorCode::ShareUnavailable:
    return smb::core::ErrorCode::ShareUnavailable;
  case smb::native_smb::ErrorCode::AuthenticationFailed:
    return smb::core::ErrorCode::AuthenticationFailed;
  case smb::native_smb::ErrorCode::PermissionDenied:
    return smb::core::ErrorCode::PermissionDenied;
  case smb::native_smb::ErrorCode::ProtocolUnsupported:
    return smb::core::ErrorCode::ProtocolUnsupported;
  case smb::native_smb::ErrorCode::FileNotFound:
    return smb::core::ErrorCode::FileNotFound;
  case smb::native_smb::ErrorCode::AlreadyExists:
    return smb::core::ErrorCode::AlreadyExists;
  case smb::native_smb::ErrorCode::DirectoryNotEmpty:
    return smb::core::ErrorCode::DirectoryNotEmpty;
  case smb::native_smb::ErrorCode::InvalidPath:
    return smb::core::ErrorCode::InvalidPath;
  case smb::native_smb::ErrorCode::NetworkError:
    return smb::core::ErrorCode::NetworkError;
  case smb::native_smb::ErrorCode::IoError:
    return smb::core::ErrorCode::LocalIoError;
  case smb::native_smb::ErrorCode::UnsupportedCapability:
    return smb::core::ErrorCode::ProtocolUnsupported;
  case smb::native_smb::ErrorCode::InternalError:
    return smb::core::ErrorCode::Unknown;
  }
  return smb::core::ErrorCode::Unknown;
}

bool isNativeErrorRetryable(smb::native_smb::ErrorCode code) {
  switch (code) {
  case smb::native_smb::ErrorCode::Timeout:
  case smb::native_smb::ErrorCode::NetworkError:
  case smb::native_smb::ErrorCode::ServerUnavailable:
    return true;
  default:
    return false;
  }
}

smb::core::AppError makeNativeSmbError(
    const smb::native_smb::ProtocolError &error,
    const smb::core::LogSanitizer &sanitizer) {
  const auto details =
      sanitizer.sanitize(QString::fromStdString(error.message));
  return smb::core::AppError::fromCode(
      nativeToCoreError(error.code), smb::core::ErrorCategory::Smb, details,
      isNativeErrorRetryable(error.code));
}

} // namespace smb::infrastructure
