#include "NtlmCrypto.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace smb::native_smb {
namespace {

std::uint32_t leftRotate(std::uint32_t value, std::uint32_t shift) {
  return (value << shift) | (value >> (32 - shift));
}

std::uint32_t readU32Le(const std::uint8_t *data) {
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) |
         (static_cast<std::uint32_t>(data[3]) << 24);
}

void appendU64Le(ByteVector &bytes, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

void writeDigestWord(Digest16 &digest, std::size_t offset,
                     std::uint32_t value) {
  digest[offset] = static_cast<std::uint8_t>(value & 0xFF);
  digest[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  digest[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
  digest[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

ByteVector paddedMessage(const ByteVector &data) {
  ByteVector bytes = data;
  const auto bitLength = static_cast<std::uint64_t>(bytes.size()) * 8;
  bytes.push_back(0x80);
  while ((bytes.size() % 64) != 56) {
    bytes.push_back(0);
  }
  appendU64Le(bytes, bitLength);
  return bytes;
}

ByteVector bytesFromDigest(const Digest16 &digest) {
  return ByteVector(digest.begin(), digest.end());
}

void wipe(ByteVector &bytes) {
  auto *ptr = reinterpret_cast<volatile std::uint8_t *>(bytes.data());
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    ptr[i] = 0;
  }
}

std::string uppercaseAscii(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::toupper(c));
                 });
  return result;
}

void appendU16Le(ByteVector &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void appendU32Le(ByteVector &bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

ByteVector secretBytesAsVector(const SecretBuffer &secret) {
  return ByteVector(secret.bytes().begin(), secret.bytes().end());
}

DecodeResult<NtlmV2ResponseParts>
computeNtlmV2ResponseFromKey(const Digest16 &responseKey,
                             const std::array<std::uint8_t, 8> &serverChallenge,
                             const std::array<std::uint8_t, 8> &clientChallenge,
                             std::uint64_t timestamp,
                             const ByteVector &targetInfo) {
  ByteVector temp;
  temp.reserve(28 + targetInfo.size() + 4);
  temp.push_back(0x01);
  temp.push_back(0x01);
  appendU16Le(temp, 0);
  appendU32Le(temp, 0);
  appendU64Le(temp, timestamp);
  temp.insert(temp.end(), clientChallenge.begin(), clientChallenge.end());
  appendU32Le(temp, 0);
  temp.insert(temp.end(), targetInfo.begin(), targetInfo.end());
  appendU32Le(temp, 0);

  ByteVector proofInput(serverChallenge.begin(), serverChallenge.end());
  proofInput.insert(proofInput.end(), temp.begin(), temp.end());

  const auto key = bytesFromDigest(responseKey);
  const auto ntProof = hmacMd5(key, proofInput);
  ByteVector ntChallengeResponse(ntProof.begin(), ntProof.end());
  ntChallengeResponse.insert(ntChallengeResponse.end(), temp.begin(),
                             temp.end());

  ByteVector lmInput(serverChallenge.begin(), serverChallenge.end());
  lmInput.insert(lmInput.end(), clientChallenge.begin(), clientChallenge.end());
  const auto lmProof = hmacMd5(key, lmInput);
  ByteVector lmChallengeResponse(lmProof.begin(), lmProof.end());
  lmChallengeResponse.insert(lmChallengeResponse.end(), clientChallenge.begin(),
                             clientChallenge.end());

  const auto sessionBaseKey = hmacMd5(key, ByteVector(ntProof.begin(),
                                                     ntProof.end()));

  NtlmV2ResponseParts parts;
  parts.ntChallengeResponse = std::move(ntChallengeResponse);
  parts.lmChallengeResponse = std::move(lmChallengeResponse);
  parts.ntProof = ntProof;
  parts.sessionBaseKey = sessionBaseKey;
  return DecodeResult<NtlmV2ResponseParts>::success(std::move(parts));
}

} // namespace

Digest16 md4(const ByteVector &data) {
  auto bytes = paddedMessage(data);
  std::uint32_t a = 0x67452301;
  std::uint32_t b = 0xEFCDAB89;
  std::uint32_t c = 0x98BADCFE;
  std::uint32_t d = 0x10325476;

  for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
    std::uint32_t x[16];
    for (std::size_t i = 0; i < 16; ++i) {
      x[i] = readU32Le(bytes.data() + offset + i * 4);
    }

    auto aa = a;
    auto bb = b;
    auto cc = c;
    auto dd = d;

    auto f = [](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
      return (x & y) | (~x & z);
    };
    auto g = [](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
      return (x & y) | (x & z) | (y & z);
    };
    auto h = [](std::uint32_t x, std::uint32_t y, std::uint32_t z) {
      return x ^ y ^ z;
    };

    auto round1 = [&](std::uint32_t &v1, std::uint32_t v2,
                      std::uint32_t v3, std::uint32_t v4,
                      std::uint32_t k, std::uint32_t s) {
      v1 = leftRotate(v1 + f(v2, v3, v4) + x[k], s);
    };
    auto round2 = [&](std::uint32_t &v1, std::uint32_t v2,
                      std::uint32_t v3, std::uint32_t v4,
                      std::uint32_t k, std::uint32_t s) {
      v1 = leftRotate(v1 + g(v2, v3, v4) + x[k] + 0x5A827999, s);
    };
    auto round3 = [&](std::uint32_t &v1, std::uint32_t v2,
                      std::uint32_t v3, std::uint32_t v4,
                      std::uint32_t k, std::uint32_t s) {
      v1 = leftRotate(v1 + h(v2, v3, v4) + x[k] + 0x6ED9EBA1, s);
    };

    for (std::uint32_t i = 0; i < 16; i += 4) {
      round1(a, b, c, d, i, 3);
      round1(d, a, b, c, i + 1, 7);
      round1(c, d, a, b, i + 2, 11);
      round1(b, c, d, a, i + 3, 19);
    }

    constexpr std::uint32_t r2[16] = {0, 4, 8, 12, 1, 5, 9, 13,
                                      2, 6, 10, 14, 3, 7, 11, 15};
    constexpr std::uint32_t s2[4] = {3, 5, 9, 13};
    for (std::uint32_t i = 0; i < 16; i += 4) {
      round2(a, b, c, d, r2[i], s2[0]);
      round2(d, a, b, c, r2[i + 1], s2[1]);
      round2(c, d, a, b, r2[i + 2], s2[2]);
      round2(b, c, d, a, r2[i + 3], s2[3]);
    }

    constexpr std::uint32_t r3[16] = {0, 8, 4, 12, 2, 10, 6, 14,
                                      1, 9, 5, 13, 3, 11, 7, 15};
    constexpr std::uint32_t s3[4] = {3, 9, 11, 15};
    for (std::uint32_t i = 0; i < 16; i += 4) {
      round3(a, b, c, d, r3[i], s3[0]);
      round3(d, a, b, c, r3[i + 1], s3[1]);
      round3(c, d, a, b, r3[i + 2], s3[2]);
      round3(b, c, d, a, r3[i + 3], s3[3]);
    }

    a += aa;
    b += bb;
    c += cc;
    d += dd;
  }

  Digest16 digest{};
  writeDigestWord(digest, 0, a);
  writeDigestWord(digest, 4, b);
  writeDigestWord(digest, 8, c);
  writeDigestWord(digest, 12, d);
  return digest;
}

Digest16 md5(const ByteVector &data) {
  auto bytes = paddedMessage(data);
  std::uint32_t a = 0x67452301;
  std::uint32_t b = 0xEFCDAB89;
  std::uint32_t c = 0x98BADCFE;
  std::uint32_t d = 0x10325476;

  constexpr std::uint32_t s[64] = {
      7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22,
      5,  9,  14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 5,  9,  14, 20,
      4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
      6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21};
  constexpr std::uint32_t k[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
      0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
      0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
      0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
      0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
      0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
      0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
      0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
      0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
      0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

  for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
    std::uint32_t m[16];
    for (std::size_t i = 0; i < 16; ++i) {
      m[i] = readU32Le(bytes.data() + offset + i * 4);
    }

    auto aa = a;
    auto bb = b;
    auto cc = c;
    auto dd = d;

    for (std::uint32_t i = 0; i < 64; ++i) {
      std::uint32_t f = 0;
      std::uint32_t g = 0;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }

      const auto next = d;
      d = c;
      c = b;
      b = b + leftRotate(a + f + k[i] + m[g], s[i]);
      a = next;
    }

    a += aa;
    b += bb;
    c += cc;
    d += dd;
  }

  Digest16 digest{};
  writeDigestWord(digest, 0, a);
  writeDigestWord(digest, 4, b);
  writeDigestWord(digest, 8, c);
  writeDigestWord(digest, 12, d);
  return digest;
}

Digest16 hmacMd5(const ByteVector &key, const ByteVector &data) {
  ByteVector normalizedKey = key;
  if (normalizedKey.size() > 64) {
    normalizedKey = bytesFromDigest(md5(normalizedKey));
  }
  normalizedKey.resize(64, 0);

  ByteVector innerPad(64);
  ByteVector outerPad(64);
  for (std::size_t i = 0; i < 64; ++i) {
    innerPad[i] = normalizedKey[i] ^ 0x36;
    outerPad[i] = normalizedKey[i] ^ 0x5C;
  }

  ByteVector inner = innerPad;
  inner.insert(inner.end(), data.begin(), data.end());
  const auto innerDigest = md5(inner);

  ByteVector outer = outerPad;
  const auto innerBytes = bytesFromDigest(innerDigest);
  outer.insert(outer.end(), innerBytes.begin(), innerBytes.end());
  return md5(outer);
}

Digest16 ntHash(std::string_view password) {
  auto utf16Password = encodeUtf16Le(password);
  const auto digest = md4(utf16Password);
  wipe(utf16Password);
  return digest;
}

Digest16 ntHash(const SecretBuffer &password) {
  auto bytes = secretBytesAsVector(password);
  const auto digest = ntHash(std::string_view(
      reinterpret_cast<const char *>(bytes.data()), bytes.size()));
  wipe(bytes);
  return digest;
}

Digest16 ntowfv2(std::string_view password, std::string_view user,
                 std::string_view domain) {
  const auto hash = ntHash(password);
  ByteVector key(hash.begin(), hash.end());
  auto identity = encodeUtf16Le(uppercaseAscii(user) + std::string(domain));
  const auto digest = hmacMd5(key, identity);
  wipe(key);
  wipe(identity);
  return digest;
}

Digest16 ntowfv2(const SecretBuffer &password, std::string_view user,
                 std::string_view domain) {
  auto bytes = secretBytesAsVector(password);
  const auto digest = ntowfv2(std::string_view(
                                  reinterpret_cast<const char *>(bytes.data()),
                                  bytes.size()),
                              user, domain);
  wipe(bytes);
  return digest;
}

DecodeResult<NtlmV2ResponseParts>
computeNtlmV2Response(std::string_view password, std::string_view user,
                      std::string_view domain,
                      const std::array<std::uint8_t, 8> &serverChallenge,
                      const std::array<std::uint8_t, 8> &clientChallenge,
                      std::uint64_t timestamp,
                      const ByteVector &targetInfo) {
  const auto responseKey = ntowfv2(password, user, domain);
  return computeNtlmV2ResponseFromKey(responseKey, serverChallenge,
                                      clientChallenge, timestamp, targetInfo);
}

DecodeResult<NtlmV2ResponseParts>
computeNtlmV2Response(const SecretBuffer &password, std::string_view user,
                      std::string_view domain,
                      const std::array<std::uint8_t, 8> &serverChallenge,
                      const std::array<std::uint8_t, 8> &clientChallenge,
                      std::uint64_t timestamp,
                      const ByteVector &targetInfo) {
  const auto responseKey = ntowfv2(password, user, domain);
  return computeNtlmV2ResponseFromKey(responseKey, serverChallenge,
                                      clientChallenge, timestamp, targetInfo);
}

std::string toHex(const Digest16 &digest) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const auto byte : digest) {
    stream << std::setw(2) << static_cast<int>(byte);
  }
  return stream.str();
}

} // namespace smb::native_smb
