#pragma once

#include <QMetaType>
#include <QString>
#include <utility>

namespace smb::core {

enum class ErrorCategory {
  General,
  Validation,
  Storage,
  Credentials,
  Smb,
  Transfer,
};

enum class ErrorCode {
  None,
  InvalidPath,
  DnsError,
  ServerUnavailable,
  ShareUnavailable,
  AuthenticationFailed,
  PermissionDenied,
  Timeout,
  ProtocolUnsupported,
  NetworkError,
  FileNotFound,
  AlreadyExists,
  DirectoryNotEmpty,
  OperationCancelled,
  LocalIoError,
  StorageError,
  CredentialStoreUnavailable,
  CredentialNotFound,
  Unknown,
};

struct AppError {
  ErrorCode code = ErrorCode::None;
  ErrorCategory category = ErrorCategory::General;
  QString userMessage;
  QString sanitizedTechnicalDetails;
  bool retryable = false;

  static AppError none();
  static AppError fromCode(ErrorCode code, ErrorCategory category,
                           QString sanitizedTechnicalDetails = QString(),
                           bool retryable = false);

  bool hasError() const;
};

template <typename T> class Result {
public:
  static Result success(T value) {
    return Result(std::move(value), AppError::none());
  }

  static Result failure(AppError error) {
    return Result(T{}, std::move(error));
  }

  bool ok() const { return !m_error.hasError(); }

  const T &value() const { return m_value; }

  T &value() { return m_value; }

  const AppError &error() const { return m_error; }

private:
  Result(T value, AppError error)
      : m_value(std::move(value)), m_error(std::move(error)) {}

  T m_value;
  AppError m_error;
};

QString toString(ErrorCategory category);
QString toString(ErrorCode code);
QString defaultUserMessage(ErrorCode code);

} // namespace smb::core

Q_DECLARE_METATYPE(smb::core::AppError)
