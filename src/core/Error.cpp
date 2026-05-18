#include "core/Error.h"

#include <utility>

namespace smb::core {

namespace {

constexpr const char *toStringLiteral(ErrorCategory category) {
  switch (category) {
  case ErrorCategory::General:
    return "general";
  case ErrorCategory::Validation:
    return "validation";
  case ErrorCategory::Storage:
    return "storage";
  case ErrorCategory::Credentials:
    return "credentials";
  case ErrorCategory::Smb:
    return "smb";
  case ErrorCategory::Transfer:
    return "transfer";
  }

  return "general";
}

constexpr const char *toStringLiteral(ErrorCode code) {
  switch (code) {
  case ErrorCode::None:
    return "none";
  case ErrorCode::InvalidPath:
    return "invalid_path";
  case ErrorCode::DnsError:
    return "dns_error";
  case ErrorCode::ServerUnavailable:
    return "server_unavailable";
  case ErrorCode::ShareUnavailable:
    return "share_unavailable";
  case ErrorCode::AuthenticationFailed:
    return "authentication_failed";
  case ErrorCode::PermissionDenied:
    return "permission_denied";
  case ErrorCode::Timeout:
    return "timeout";
  case ErrorCode::ProtocolUnsupported:
    return "protocol_unsupported";
  case ErrorCode::NetworkError:
    return "network_error";
  case ErrorCode::FileNotFound:
    return "file_not_found";
  case ErrorCode::AlreadyExists:
    return "already_exists";
  case ErrorCode::DirectoryNotEmpty:
    return "directory_not_empty";
  case ErrorCode::OperationCancelled:
    return "operation_cancelled";
  case ErrorCode::LocalIoError:
    return "local_io_error";
  case ErrorCode::StorageError:
    return "storage_error";
  case ErrorCode::CredentialStoreUnavailable:
    return "credential_store_unavailable";
  case ErrorCode::CredentialNotFound:
    return "credential_not_found";
  case ErrorCode::Unknown:
    return "unknown";
  }

  return "unknown";
}

} // namespace

QString toString(ErrorCategory category) {
  return QString::fromLatin1(toStringLiteral(category));
}

QString toString(ErrorCode code) {
  return QString::fromLatin1(toStringLiteral(code));
}

QString defaultUserMessage(ErrorCode code) {
  switch (code) {
  case ErrorCode::None:
    return QString();
  case ErrorCode::InvalidPath:
    return QStringLiteral("The path is not valid.");
  case ErrorCode::DnsError:
    return QStringLiteral("The server name could not be resolved.");
  case ErrorCode::ServerUnavailable:
    return QStringLiteral("The server is unavailable.");
  case ErrorCode::ShareUnavailable:
    return QStringLiteral("The shared folder is unavailable.");
  case ErrorCode::AuthenticationFailed:
    return QStringLiteral("The username or password is incorrect.");
  case ErrorCode::PermissionDenied:
    return QStringLiteral(
        "You do not have permission to perform this operation.");
  case ErrorCode::Timeout:
    return QStringLiteral("The operation timed out.");
  case ErrorCode::ProtocolUnsupported:
    return QStringLiteral("The SMB protocol version is not supported.");
  case ErrorCode::NetworkError:
    return QStringLiteral("A network error occurred.");
  case ErrorCode::FileNotFound:
    return QStringLiteral("The file or folder was not found.");
  case ErrorCode::AlreadyExists:
    return QStringLiteral("A file or folder with this name already exists.");
  case ErrorCode::DirectoryNotEmpty:
    return QStringLiteral("The folder is not empty.");
  case ErrorCode::OperationCancelled:
    return QStringLiteral("The operation was cancelled.");
  case ErrorCode::LocalIoError:
    return QStringLiteral("A local file system error occurred.");
  case ErrorCode::StorageError:
    return QStringLiteral("The local database operation failed.");
  case ErrorCode::CredentialStoreUnavailable:
    return QStringLiteral("The credential store is unavailable.");
  case ErrorCode::CredentialNotFound:
    return QStringLiteral("The saved credential was not found.");
  case ErrorCode::Unknown:
    return QStringLiteral("An unknown error occurred.");
  }

  return QStringLiteral("An unknown error occurred.");
}

AppError AppError::none() { return {}; }

AppError AppError::fromCode(ErrorCode code, ErrorCategory category,
                            QString sanitizedTechnicalDetails, bool retryable) {
  return AppError{
      code,
      category,
      defaultUserMessage(code),
      std::move(sanitizedTechnicalDetails),
      retryable,
  };
}

bool AppError::hasError() const { return code != ErrorCode::None; }

} // namespace smb::core
