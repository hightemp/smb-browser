#pragma once

#include "NtlmMessages.h"

#include <array>
#include <string>
#include <string_view>

namespace smb::native_smb {

using Digest16 = std::array<std::uint8_t, 16>;

struct NtlmV2ResponseParts {
  ByteVector ntChallengeResponse;
  ByteVector lmChallengeResponse;
  Digest16 ntProof{};
  Digest16 sessionBaseKey{};
};

Digest16 md4(const ByteVector &data);
Digest16 md5(const ByteVector &data);
Digest16 hmacMd5(const ByteVector &key, const ByteVector &data);
Digest16 ntHash(std::string_view password);
Digest16 ntHash(const SecretBuffer &password);
Digest16 ntowfv2(std::string_view password, std::string_view user,
                 std::string_view domain);
Digest16 ntowfv2(const SecretBuffer &password, std::string_view user,
                 std::string_view domain);
DecodeResult<NtlmV2ResponseParts>
computeNtlmV2Response(std::string_view password, std::string_view user,
                      std::string_view domain,
                      const std::array<std::uint8_t, 8> &serverChallenge,
                      const std::array<std::uint8_t, 8> &clientChallenge,
                      std::uint64_t timestamp,
                      const ByteVector &targetInfo);
DecodeResult<NtlmV2ResponseParts>
computeNtlmV2Response(const SecretBuffer &password, std::string_view user,
                      std::string_view domain,
                      const std::array<std::uint8_t, 8> &serverChallenge,
                      const std::array<std::uint8_t, 8> &clientChallenge,
                      std::uint64_t timestamp,
                      const ByteVector &targetInfo);
std::string toHex(const Digest16 &digest);

} // namespace smb::native_smb
