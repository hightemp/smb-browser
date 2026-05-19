#pragma once

#include "NativeSmbConnector.h"
#include "NtlmCrypto.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace smb::native_smb {

using NtlmClientChallengeGenerator =
    std::function<std::array<std::uint8_t, 8>()>;
using NtlmTimestampProvider = std::function<std::uint64_t()>;

struct NtlmV2TokenProviderOptions {
  std::string workstation;
  std::uint32_t negotiateFlags =
      kNtlmNegotiateUnicode | kNtlmRequestTarget | kNtlmNegotiateSign |
      kNtlmNegotiateSeal | kNtlmNegotiateNtlm |
      kNtlmNegotiateAlwaysSign | kNtlmNegotiateExtendedSessionSecurity |
      kNtlmNegotiateTargetInfo | kNtlmNegotiateVersion |
      kNtlmNegotiate128 | kNtlmNegotiate56;
  bool useServerTimestamp = true;
  bool omitLmResponse = false;
  bool useSpnego = true;
  std::optional<std::array<std::uint8_t, 8>> fixedClientChallenge;
  std::optional<std::uint64_t> fixedTimestamp;
  NtlmClientChallengeGenerator clientChallengeGenerator;
  NtlmTimestampProvider timestampProvider;
};

class NtlmV2TokenProvider final : public SessionSetupTokenProvider {
public:
  explicit NtlmV2TokenProvider(SecretBuffer password,
                               NtlmV2TokenProviderOptions options = {});
  ~NtlmV2TokenProvider() override;

  DecodeResult<ByteVector>
  initialToken(const NegotiatedConnection &negotiated,
               const ConnectionConfig &config) override;

  DecodeResult<ByteVector>
  nextToken(const SessionSetupResponse &challenge,
            const ConnectionConfig &config) override;

  DecodeResult<ByteVector> sessionBaseKey() const override;

  const ByteVector &lastNegotiateMessageForTests() const;
  const Digest16 &sessionBaseKeyForTests() const;

private:
  SecretBuffer m_password;
  NtlmV2TokenProviderOptions m_options;
  ByteVector m_lastNegotiateMessage;
  Digest16 m_sessionBaseKey{};
};

} // namespace smb::native_smb
