#include "smb/Libsmb2ErrorMapper.h"

#include <cerrno>

namespace smb::infrastructure {

namespace {

smb::core::ErrorCode mapConnectionError(int status, const QString &details) {
  const auto err = status < 0 ? -status : status;
  const auto lower = details.toLower();

  if (lower.contains(QStringLiteral("name")) ||
      lower.contains(QStringLiteral("resolve")) ||
      lower.contains(QStringLiteral("dns"))) {
    return smb::core::ErrorCode::DnsError;
  }

  switch (err) {
  case EACCES:
  case EPERM:
    return smb::core::ErrorCode::AuthenticationFailed;
  case ETIMEDOUT:
    return smb::core::ErrorCode::Timeout;
  case ENOENT:
  case ENODEV:
    return smb::core::ErrorCode::ShareUnavailable;
  case ECONNREFUSED:
  case ECONNRESET:
  case EHOSTUNREACH:
  case ENETUNREACH:
    return smb::core::ErrorCode::ServerUnavailable;
  case EPROTONOSUPPORT:
  case EPROTO:
  case EOPNOTSUPP:
    return smb::core::ErrorCode::ProtocolUnsupported;
  default:
    return smb::core::ErrorCode::NetworkError;
  }
}

smb::core::ErrorCode mapDirectoryError(int status, const QString &details) {
  const auto err = status < 0 ? -status : status;
  const auto lower = details.toLower();

  if (lower.contains(QStringLiteral("permission")) ||
      lower.contains(QStringLiteral("access denied"))) {
    return smb::core::ErrorCode::PermissionDenied;
  }

  switch (err) {
  case EACCES:
  case EPERM:
    return smb::core::ErrorCode::PermissionDenied;
  case ETIMEDOUT:
    return smb::core::ErrorCode::Timeout;
  case ENOENT:
    return smb::core::ErrorCode::FileNotFound;
  case EPROTONOSUPPORT:
  case EPROTO:
  case EOPNOTSUPP:
    return smb::core::ErrorCode::ProtocolUnsupported;
  default:
    return smb::core::ErrorCode::NetworkError;
  }
}

smb::core::ErrorCode mapFileOperationError(int status, const QString &details) {
  const auto err = status < 0 ? -status : status;
  const auto lower = details.toLower();

  if (lower.contains(QStringLiteral("permission")) ||
      lower.contains(QStringLiteral("access denied"))) {
    return smb::core::ErrorCode::PermissionDenied;
  }
  if (lower.contains(QStringLiteral("already exists"))) {
    return smb::core::ErrorCode::AlreadyExists;
  }
  if (lower.contains(QStringLiteral("not empty"))) {
    return smb::core::ErrorCode::DirectoryNotEmpty;
  }

  switch (err) {
  case EACCES:
  case EPERM:
    return smb::core::ErrorCode::PermissionDenied;
  case ETIMEDOUT:
    return smb::core::ErrorCode::Timeout;
  case ENOENT:
  case ENOTDIR:
    return smb::core::ErrorCode::FileNotFound;
  case EEXIST:
    return smb::core::ErrorCode::AlreadyExists;
  case ENOTEMPTY:
    return smb::core::ErrorCode::DirectoryNotEmpty;
  case EPROTONOSUPPORT:
  case EPROTO:
  case EOPNOTSUPP:
    return smb::core::ErrorCode::ProtocolUnsupported;
  default:
    return smb::core::ErrorCode::NetworkError;
  }
}

} // namespace

smb::core::ErrorCode mapLibsmb2Error(int status, const QString &details,
                                     Libsmb2ErrorContext context) {
  switch (context) {
  case Libsmb2ErrorContext::Connection:
    return mapConnectionError(status, details);
  case Libsmb2ErrorContext::Directory:
    return mapDirectoryError(status, details);
  case Libsmb2ErrorContext::FileOperation:
    return mapFileOperationError(status, details);
  }

  return smb::core::ErrorCode::NetworkError;
}

} // namespace smb::infrastructure
