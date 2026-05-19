#include "NtlmV2TokenProvider.h"

#include "SpnegoToken.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <utility>

namespace smb::native_smb {
namespace {

constexpr std::uint64_t kUnixEpochAsFiletime = 116444736000000000ULL;

std::uint16_t readU16Le(const std::uint8_t *data) {
  return static_cast<std::uint16_t>(data[0]) |
         static_cast<std::uint16_t>(data[1] << 8);
}

std::uint64_t readU64Le(const std::uint8_t *data) {
  std::uint64_t value = 0;
  for (int shift = 0; shift < 64; shift += 8) {
    value |= static_cast<std::uint64_t>(*data++) << shift;
  }
  return value;
}

std::uint64_t currentFiletime() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto ticks =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count() / 100;
  return kUnixEpochAsFiletime + static_cast<std::uint64_t>(ticks);
}

std::array<std::uint8_t, 8> randomClientChallenge() {
  std::array<std::uint8_t, 8> bytes{};
  std::random_device device;
  for (auto &byte : bytes) {
    byte = static_cast<std::uint8_t>(device());
  }
  return bytes;
}

bool startsWithNtlmSignature(const ByteVector &bytes) {
  constexpr std::array<std::uint8_t, 8> kSignature{
      'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};
  return bytes.size() >= kSignature.size() &&
         std::equal(kSignature.begin(), kSignature.end(), bytes.begin());
}

DecodeResult<std::optional<std::uint64_t>>
timestampFromTargetInfo(const ByteVector &targetInfo) {
  std::size_t offset = 0;
  while (offset + 4 <= targetInfo.size()) {
    const auto avId = readU16Le(targetInfo.data() + offset);
    const auto avLength = readU16Le(targetInfo.data() + offset + 2);
    offset += 4;
    if (offset + avLength > targetInfo.size()) {
      return DecodeResult<std::optional<std::uint64_t>>::failure(
          ErrorCode::ProtocolUnsupported,
          "NTLM target info AV_PAIR is truncated.");
    }
    if (avId == 0) {
      return DecodeResult<std::optional<std::uint64_t>>::success(std::nullopt);
    }
    if (avId == 7) {
      if (avLength != 8) {
        return DecodeResult<std::optional<std::uint64_t>>::failure(
            ErrorCode::ProtocolUnsupported,
            "NTLM timestamp AV_PAIR has invalid length.");
      }
      return DecodeResult<std::optional<std::uint64_t>>::success(
          readU64Le(targetInfo.data() + offset));
    }
    offset += avLength;
  }

  if (targetInfo.empty()) {
    return DecodeResult<std::optional<std::uint64_t>>::success(std::nullopt);
  }
  return DecodeResult<std::optional<std::uint64_t>>::failure(
      ErrorCode::ProtocolUnsupported, "NTLM target info is not terminated.");
}

std::array<std::uint8_t, 8>
clientChallengeFromOptions(const NtlmV2TokenProviderOptions &options) {
  if (options.fixedClientChallenge.has_value()) {
    return *options.fixedClientChallenge;
  }
  if (options.clientChallengeGenerator) {
    return options.clientChallengeGenerator();
  }
  return randomClientChallenge();
}

std::uint64_t timestampFromOptions(const NtlmV2TokenProviderOptions &options) {
  if (options.fixedTimestamp.has_value()) {
    return *options.fixedTimestamp;
  }
  if (options.timestampProvider) {
    return options.timestampProvider();
  }
  return currentFiletime();
}

std::uint32_t authFlagsForChallenge(std::uint32_t challengeFlags,
                                    std::uint32_t requestedFlags) {
  const auto flags = challengeFlags & requestedFlags;
  return (flags | kNtlmNegotiateUnicode | kNtlmNegotiateNtlm |
          kNtlmNegotiateExtendedSessionSecurity) &
         ~kNtlmNegotiateKeyExchange;
}

DecodeResult<ByteVector> failureFrom(const ProtocolError &error) {
  return DecodeResult<ByteVector>::failure(error.code, error.message);
}

struct AuthIdentity {
  std::string domain;
  std::string username;
  bool anonymous = false;
};

DecodeResult<AuthIdentity> authIdentityFromConfig(
    const ConnectionConfig &config) {
  switch (config.authMode) {
  case AuthMode::Password:
    return DecodeResult<AuthIdentity>::success(
        AuthIdentity{config.domain, config.username, false});
  case AuthMode::Guest:
    return DecodeResult<AuthIdentity>::success(
        AuthIdentity{config.domain,
                     config.username.empty() ? "Guest" : config.username,
                     false});
  case AuthMode::Anonymous:
    return DecodeResult<AuthIdentity>::success(AuthIdentity{"", "", true});
  case AuthMode::CurrentUser:
    return DecodeResult<AuthIdentity>::failure(
        ErrorCode::UnsupportedCapability,
        "Current-user/Kerberos authentication is not implemented by the "
        "clean-room native SMB engine yet.");
  }
  return DecodeResult<AuthIdentity>::failure(
      ErrorCode::UnsupportedCapability,
      "Unsupported native SMB authentication mode.");
}

} // namespace

NtlmV2TokenProvider::NtlmV2TokenProvider(
    SecretBuffer password, NtlmV2TokenProviderOptions options)
    : m_password(std::move(password)), m_options(std::move(options)) {}

NtlmV2TokenProvider::~NtlmV2TokenProvider() = default;

DecodeResult<ByteVector>
NtlmV2TokenProvider::initialToken(const NegotiatedConnection &,
                                  const ConnectionConfig &config) {
  const auto identity = authIdentityFromConfig(config);
  if (!identity.ok) {
    return failureFrom(identity.error);
  }

  NtlmNegotiateOptions options;
  options.domain = identity.value.domain;
  options.workstation = m_options.workstation;
  options.flags = m_options.negotiateFlags;
  m_lastNegotiateMessage = buildNtlmNegotiateMessage(options);
  if (m_options.useSpnego) {
    return DecodeResult<ByteVector>::success(
        buildSpnegoNegTokenInit(m_lastNegotiateMessage));
  }
  return DecodeResult<ByteVector>::success(m_lastNegotiateMessage);
}

DecodeResult<ByteVector>
NtlmV2TokenProvider::nextToken(const SessionSetupResponse &challenge,
                               const ConnectionConfig &config) {
  auto challengeToken =
      m_options.useSpnego ? unwrapSpnegoNtlmToken(challenge.securityBuffer)
                          : DecodeResult<ByteVector>::success(
                                challenge.securityBuffer);
  if (!challengeToken.ok) {
    return failureFrom(challengeToken.error);
  }

  if (!startsWithNtlmSignature(challengeToken.value)) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::ProtocolUnsupported,
        "NTLMv2 token provider received a non-NTLMSSP challenge token.");
  }

  const auto decoded = decodeNtlmChallengeMessage(challengeToken.value);
  if (!decoded.ok) {
    return failureFrom(decoded.error);
  }

  const auto targetTimestamp = timestampFromTargetInfo(decoded.value.targetInfo);
  if (!targetTimestamp.ok) {
    return failureFrom(targetTimestamp.error);
  }
  const auto timestamp =
      (m_options.useServerTimestamp && targetTimestamp.value.has_value())
          ? *targetTimestamp.value
          : timestampFromOptions(m_options);

  NtlmAuthenticateOptions auth;
  const auto identity = authIdentityFromConfig(config);
  if (!identity.ok) {
    return failureFrom(identity.error);
  }

  auth.domain = identity.value.domain;
  auth.username = identity.value.username;
  auth.workstation = m_options.workstation;
  auth.flags =
      authFlagsForChallenge(decoded.value.flags, m_options.negotiateFlags);
  auth.includeVersion = (auth.flags & kNtlmNegotiateVersion) != 0;

  if (identity.value.anonymous) {
    auth.lmChallengeResponse = {0};
  } else {
    const auto clientChallenge = clientChallengeFromOptions(m_options);
    const auto response = computeNtlmV2Response(
        m_password, identity.value.username, identity.value.domain,
        decoded.value.serverChallenge, clientChallenge, timestamp,
        decoded.value.targetInfo);
    if (!response.ok) {
      return failureFrom(response.error);
    }

    auth.ntChallengeResponse = response.value.ntChallengeResponse;
    if (!m_options.omitLmResponse) {
      auth.lmChallengeResponse = response.value.lmChallengeResponse;
    }
    m_sessionBaseKey = response.value.sessionBaseKey;
  }

  auto authenticateToken = buildNtlmAuthenticateMessage(auth);
  if (m_options.useSpnego) {
    authenticateToken = buildSpnegoNegTokenResp(authenticateToken);
  }
  return DecodeResult<ByteVector>::success(std::move(authenticateToken));
}

const ByteVector &NtlmV2TokenProvider::lastNegotiateMessageForTests() const {
  return m_lastNegotiateMessage;
}

DecodeResult<ByteVector> NtlmV2TokenProvider::sessionBaseKey() const {
  const bool hasKey = std::any_of(m_sessionBaseKey.begin(),
                                  m_sessionBaseKey.end(),
                                  [](std::uint8_t byte) { return byte != 0; });
  if (!hasKey) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::AuthenticationFailed,
        "NTLMv2 session base key is not available before authentication.");
  }
  return DecodeResult<ByteVector>::success(
      ByteVector(m_sessionBaseKey.begin(), m_sessionBaseKey.end()));
}

const Digest16 &NtlmV2TokenProvider::sessionBaseKeyForTests() const {
  return m_sessionBaseKey;
}

} // namespace smb::native_smb
