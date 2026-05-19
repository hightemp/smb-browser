#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace smb::native_smb {

enum class DialectFamily {
  Smb2AndSmb3,
};

enum class AuthMode {
  Password,
  Guest,
  Anonymous,
  CurrentUser,
};

enum class SecurityPolicy {
  Required,
  Preferred,
  Disabled,
};

enum class ErrorCode {
  None,
  Cancelled,
  Timeout,
  DnsError,
  ServerUnavailable,
  ShareUnavailable,
  AuthenticationFailed,
  PermissionDenied,
  ProtocolUnsupported,
  FileNotFound,
  AlreadyExists,
  DirectoryNotEmpty,
  InvalidPath,
  NetworkError,
  IoError,
  UnsupportedCapability,
  InternalError,
};

struct ConnectionConfig {
  std::string server;
  std::string share;
  std::string normalizedUri;
  std::string domain;
  std::string username;
  AuthMode authMode = AuthMode::Password;
  DialectFamily dialectFamily = DialectFamily::Smb2AndSmb3;
  SecurityPolicy signing = SecurityPolicy::Required;
  SecurityPolicy encryption = SecurityPolicy::Preferred;
  std::chrono::milliseconds timeout{30000};
};

class SecretBuffer {
public:
  SecretBuffer() = default;
  explicit SecretBuffer(std::vector<std::uint8_t> bytes);
  ~SecretBuffer();

  SecretBuffer(const SecretBuffer &) = delete;
  SecretBuffer &operator=(const SecretBuffer &) = delete;

  SecretBuffer(SecretBuffer &&other) noexcept;
  SecretBuffer &operator=(SecretBuffer &&other) noexcept;

  const std::vector<std::uint8_t> &bytes() const;
  bool empty() const;
  void clear();

private:
  std::vector<std::uint8_t> m_bytes;
};

class CancellationToken {
public:
  void cancel();
  bool isCancellationRequested() const;

private:
  std::atomic_bool m_cancelled = false;
};

struct TransferProgress {
  std::uint64_t bytesTransferred = 0;
  std::uint64_t totalBytes = 0;
};

struct OperationContext {
  std::chrono::milliseconds timeout{30000};
  CancellationToken *cancellationToken = nullptr;
  std::function<bool()> cancellationCallback;
  std::function<void(const TransferProgress &)> progressCallback;
  std::function<void(const std::string &)> sanitizedLogCallback;
};

bool isCancellationRequested(const OperationContext &context);

struct BuildPolicy {
  bool cleanRoomOnly = true;
  bool usesSambaCode = false;
  bool requiresExternalSmbRuntime = false;
  bool supportsSmb1 = false;
  DialectFamily dialectFamily = DialectFamily::Smb2AndSmb3;
};

const BuildPolicy &buildPolicy();
std::string version();

} // namespace smb::native_smb
