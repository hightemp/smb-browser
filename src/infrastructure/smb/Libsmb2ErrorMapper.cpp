#include "smb/Libsmb2ErrorMapper.h"

#include <cerrno>

namespace smb::infrastructure {

namespace {

smb::core::ErrorCode mapConnectionError(int status, const QString &details) {
  const auto err = status < 0 ? -status : status;
  const auto lower = details.toLower();

  if (lower.contains(QStringLiteral("logon")) ||
      lower.contains(QStringLiteral("authentication")) ||
      lower.contains(QStringLiteral("bad password"))) {
    return smb::core::ErrorCode::AuthenticationFailed;
  }
  if (lower.contains(QStringLiteral("bad network name")) ||
      lower.contains(QStringLiteral("bad_network_name")) ||
      lower.contains(QStringLiteral("status_bad_network_name")) ||
      lower.contains(QStringLiteral("tree connect")) ||
      lower.contains(QStringLiteral("share"))) {
    return smb::core::ErrorCode::ShareUnavailable;
  }
  if (lower.contains(QStringLiteral("name")) ||
      lower.contains(QStringLiteral("resolve")) ||
      lower.contains(QStringLiteral("dns"))) {
    return smb::core::ErrorCode::DnsError;
  }
  if (lower.contains(QStringLiteral("protocol")) ||
      lower.contains(QStringLiteral("dialect")) ||
      lower.contains(QStringLiteral("negotiate"))) {
    return smb::core::ErrorCode::ProtocolUnsupported;
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
  if (lower.contains(QStringLiteral("bad network name")) ||
      lower.contains(QStringLiteral("bad_network_name")) ||
      lower.contains(QStringLiteral("tree connect")) ||
      lower.contains(QStringLiteral("share"))) {
    return smb::core::ErrorCode::ShareUnavailable;
  }
  if (lower.contains(QStringLiteral("protocol")) ||
      lower.contains(QStringLiteral("dialect")) ||
      lower.contains(QStringLiteral("negotiate"))) {
    return smb::core::ErrorCode::ProtocolUnsupported;
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
  if (lower.contains(QStringLiteral("protocol")) ||
      lower.contains(QStringLiteral("dialect")) ||
      lower.contains(QStringLiteral("negotiate"))) {
    return smb::core::ErrorCode::ProtocolUnsupported;
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

bool isRetryable(smb::core::ErrorCode code) {
  return code == smb::core::ErrorCode::Timeout ||
         code == smb::core::ErrorCode::ServerUnavailable ||
         code == smb::core::ErrorCode::NetworkError;
}

QString diagnosticHint(smb::core::ErrorCode code, Libsmb2ErrorContext context) {
  switch (code) {
  case smb::core::ErrorCode::DnsError:
    return QStringLiteral("Check the server name and DNS resolution.");
  case smb::core::ErrorCode::ServerUnavailable:
    return QStringLiteral(
        "Check network connectivity, firewall rules, and whether the SMB "
        "server is reachable.");
  case smb::core::ErrorCode::ShareUnavailable:
    return context == Libsmb2ErrorContext::Connection
               ? QStringLiteral(
                     "Check that the share name exists and is exported by the "
                     "server. If this path is a DFS namespace, use the "
                     "resolved "
                     "target or install smbclient so the DFS resolver can find "
                     "it.")
               : QStringLiteral(
                     "Check that the share name exists and is exported by the "
                     "server.");
  case smb::core::ErrorCode::AuthenticationFailed:
    return QStringLiteral(
        "Check username, domain or workgroup, password, and guest access "
        "settings.");
  case smb::core::ErrorCode::PermissionDenied:
    return context == Libsmb2ErrorContext::Connection
               ? QStringLiteral(
                     "Credentials were rejected or do not allow access to this "
                     "share.")
               : QStringLiteral(
                     "The credentials do not allow this operation on the "
                     "selected path.");
  case smb::core::ErrorCode::Timeout:
    return QStringLiteral(
        "Check server responsiveness and consider increasing the operation "
        "timeout.");
  case smb::core::ErrorCode::ProtocolUnsupported:
    return QStringLiteral(
        "Check SMB dialect/version requirements and server capabilities.");
  case smb::core::ErrorCode::FileNotFound:
    return QStringLiteral("Check that the remote path still exists.");
  case smb::core::ErrorCode::AlreadyExists:
    return QStringLiteral(
        "Choose another target name or overwrite explicitly.");
  case smb::core::ErrorCode::DirectoryNotEmpty:
    return QStringLiteral("Delete directory contents before removing it.");
  default:
    return QString();
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

smb::core::AppError makeLibsmb2Error(int status, const QString &details,
                                     Libsmb2ErrorContext context,
                                     const smb::core::LogSanitizer &sanitizer) {
  const auto code = mapLibsmb2Error(status, details, context);
  const auto hint = diagnosticHint(code, context);
  auto sanitizedDetails = sanitizer.sanitize(details);
  if (!hint.isEmpty()) {
    if (!sanitizedDetails.isEmpty()) {
      sanitizedDetails += QStringLiteral(" ");
    }
    sanitizedDetails += QStringLiteral("hint=\"%1\"").arg(hint);
  }

  auto error = smb::core::AppError::fromCode(
      code, smb::core::ErrorCategory::Smb, sanitizedDetails, isRetryable(code));
  if (!hint.isEmpty()) {
    error.userMessage = QStringLiteral("%1 %2").arg(error.userMessage, hint);
  }
  return error;
}

} // namespace smb::infrastructure
