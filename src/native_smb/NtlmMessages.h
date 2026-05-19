#pragma once

#include "Protocol.h"

#include <array>
#include <cstdint>
#include <string>

namespace smb::native_smb {

constexpr std::uint32_t kNtlmNegotiateUnicode = 0x00000001;
constexpr std::uint32_t kNtlmNegotiateOem = 0x00000002;
constexpr std::uint32_t kNtlmRequestTarget = 0x00000004;
constexpr std::uint32_t kNtlmNegotiateSign = 0x00000010;
constexpr std::uint32_t kNtlmNegotiateSeal = 0x00000020;
constexpr std::uint32_t kNtlmNegotiateNtlm = 0x00000200;
constexpr std::uint32_t kNtlmNegotiateAlwaysSign = 0x00008000;
constexpr std::uint32_t kNtlmNegotiateExtendedSessionSecurity = 0x00080000;
constexpr std::uint32_t kNtlmNegotiateTargetInfo = 0x00800000;
constexpr std::uint32_t kNtlmNegotiateVersion = 0x02000000;
constexpr std::uint32_t kNtlmNegotiate128 = 0x20000000;
constexpr std::uint32_t kNtlmNegotiateKeyExchange = 0x40000000;
constexpr std::uint32_t kNtlmNegotiate56 = 0x80000000;

struct NtlmNegotiateOptions {
  std::string domain;
  std::string workstation;
  std::uint32_t flags = kNtlmNegotiateUnicode | kNtlmRequestTarget |
                        kNtlmNegotiateSign | kNtlmNegotiateSeal |
                        kNtlmNegotiateNtlm | kNtlmNegotiateAlwaysSign |
                        kNtlmNegotiateExtendedSessionSecurity |
                        kNtlmNegotiateTargetInfo | kNtlmNegotiateVersion |
                        kNtlmNegotiate128 | kNtlmNegotiateKeyExchange |
                        kNtlmNegotiate56;
  std::array<std::uint8_t, 8> version{0x0A, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x0F};
};

struct NtlmChallengeMessage {
  std::string targetName;
  std::uint32_t flags = 0;
  std::array<std::uint8_t, 8> serverChallenge{};
  ByteVector targetInfo;
  bool hasTargetInfo = false;
};

struct NtlmAuthenticateOptions {
  std::string domain;
  std::string username;
  std::string workstation;
  ByteVector lmChallengeResponse;
  ByteVector ntChallengeResponse;
  ByteVector encryptedRandomSessionKey;
  std::uint32_t flags = 0;
  std::array<std::uint8_t, 8> version{0x0A, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x0F};
  bool includeVersion = true;
  bool includeMic = false;
  std::array<std::uint8_t, 16> mic{};
};

ByteVector buildNtlmNegotiateMessage(const NtlmNegotiateOptions &options);
ByteVector
buildNtlmAuthenticateMessage(const NtlmAuthenticateOptions &options);
DecodeResult<NtlmChallengeMessage>
decodeNtlmChallengeMessage(const ByteVector &bytes);

} // namespace smb::native_smb
