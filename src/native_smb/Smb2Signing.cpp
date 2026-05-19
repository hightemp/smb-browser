#include "Smb2Signing.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace smb::native_smb {
namespace {

constexpr std::size_t kSha256BlockSize = 64;
constexpr std::size_t kSmb2SignatureOffset = 48;
constexpr std::size_t kSmb2FlagsOffset = 16;
constexpr std::uint8_t kAesBlockSize = 16;

std::uint32_t rotr(std::uint32_t value, std::uint32_t shift) {
  return (value >> shift) | (value << (32 - shift));
}

void appendU64Be(ByteVector &bytes, std::uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

void appendU32Be(ByteVector &bytes, std::uint32_t value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

std::uint32_t readU32Be(const std::uint8_t *data) {
  return (static_cast<std::uint32_t>(data[0]) << 24) |
         (static_cast<std::uint32_t>(data[1]) << 16) |
         (static_cast<std::uint32_t>(data[2]) << 8) |
         static_cast<std::uint32_t>(data[3]);
}

void writeU32Be(Digest32 &digest, std::size_t offset, std::uint32_t value) {
  digest[offset] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
  digest[offset + 1] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
  digest[offset + 2] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  digest[offset + 3] = static_cast<std::uint8_t>(value & 0xFF);
}

void writeU32Le(ByteVector &bytes, std::size_t offset, std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
  bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

std::uint32_t readU32Le(const ByteVector &bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

ByteVector paddedSha256Message(const ByteVector &data) {
  ByteVector bytes = data;
  const auto bitLength = static_cast<std::uint64_t>(bytes.size()) * 8;
  bytes.push_back(0x80);
  while ((bytes.size() % kSha256BlockSize) != 56) {
    bytes.push_back(0);
  }
  appendU64Be(bytes, bitLength);
  return bytes;
}

ByteVector bytesFromDigest(const Digest32 &digest) {
  return ByteVector(digest.begin(), digest.end());
}

ByteVector bytesFromBlock(const Block16 &block) {
  return ByteVector(block.begin(), block.end());
}

DecodeResult<ByteVector> unsupportedSigning(Dialect dialect) {
  std::ostringstream stream;
  stream << "SMB signing for dialect 0x" << std::hex << std::uppercase
         << static_cast<std::uint16_t>(dialect)
         << " is not implemented in the clean-room engine yet.";
  return DecodeResult<ByteVector>::failure(ErrorCode::UnsupportedCapability,
                                           stream.str());
}

DecodeResult<bool> unsupportedSigningVerification(Dialect dialect) {
  const auto error = unsupportedSigning(dialect);
  return DecodeResult<bool>::failure(error.error.code, error.error.message);
}

void zeroSmb2Signature(ByteVector &payload) {
  std::fill(payload.begin() + kSmb2SignatureOffset,
            payload.begin() + kSmb2SignatureOffset + 16, 0);
}

std::uint8_t xtime(std::uint8_t value) {
  return static_cast<std::uint8_t>((value << 1) ^
                                   ((value & 0x80) != 0 ? 0x1B : 0));
}

std::uint8_t gfMultiply(std::uint8_t left, std::uint8_t right) {
  std::uint8_t result = 0;
  while (right != 0) {
    if ((right & 1) != 0) {
      result ^= left;
    }
    left = xtime(left);
    right >>= 1;
  }
  return result;
}

std::uint8_t gfPow(std::uint8_t value, std::uint8_t power) {
  std::uint8_t result = 1;
  while (power != 0) {
    if ((power & 1) != 0) {
      result = gfMultiply(result, value);
    }
    value = gfMultiply(value, value);
    power >>= 1;
  }
  return result;
}

std::uint8_t aesSbox(std::uint8_t value) {
  const auto inverse = value == 0 ? 0 : gfPow(value, 254);
  auto result = inverse;
  for (int shift = 1; shift <= 4; ++shift) {
    result ^= static_cast<std::uint8_t>((inverse << shift) |
                                        (inverse >> (8 - shift)));
  }
  return static_cast<std::uint8_t>(result ^ 0x63);
}

void addRoundKey(Block16 &state, const std::uint8_t *roundKey) {
  for (std::size_t i = 0; i < state.size(); ++i) {
    state[i] ^= roundKey[i];
  }
}

void subBytes(Block16 &state) {
  for (auto &byte : state) {
    byte = aesSbox(byte);
  }
}

void shiftRows(Block16 &state) {
  Block16 original = state;
  state[0] = original[0];
  state[4] = original[4];
  state[8] = original[8];
  state[12] = original[12];

  state[1] = original[5];
  state[5] = original[9];
  state[9] = original[13];
  state[13] = original[1];

  state[2] = original[10];
  state[6] = original[14];
  state[10] = original[2];
  state[14] = original[6];

  state[3] = original[15];
  state[7] = original[3];
  state[11] = original[7];
  state[15] = original[11];
}

void mixColumns(Block16 &state) {
  for (std::size_t column = 0; column < 4; ++column) {
    const auto offset = column * 4;
    const auto a0 = state[offset];
    const auto a1 = state[offset + 1];
    const auto a2 = state[offset + 2];
    const auto a3 = state[offset + 3];
    state[offset] = static_cast<std::uint8_t>(gfMultiply(a0, 2) ^
                                              gfMultiply(a1, 3) ^ a2 ^ a3);
    state[offset + 1] = static_cast<std::uint8_t>(
        a0 ^ gfMultiply(a1, 2) ^ gfMultiply(a2, 3) ^ a3);
    state[offset + 2] = static_cast<std::uint8_t>(
        a0 ^ a1 ^ gfMultiply(a2, 2) ^ gfMultiply(a3, 3));
    state[offset + 3] = static_cast<std::uint8_t>(
        gfMultiply(a0, 3) ^ a1 ^ a2 ^ gfMultiply(a3, 2));
  }
}

std::array<std::uint8_t, 176> expandAes128Key(const Block16 &key) {
  std::array<std::uint8_t, 176> expanded{};
  std::copy(key.begin(), key.end(), expanded.begin());

  std::uint8_t rcon = 1;
  std::size_t bytesGenerated = 16;
  std::array<std::uint8_t, 4> temp{};
  while (bytesGenerated < expanded.size()) {
    std::copy_n(expanded.begin() + bytesGenerated - 4, 4, temp.begin());
    if ((bytesGenerated % 16) == 0) {
      const auto first = temp[0];
      temp[0] = aesSbox(temp[1]) ^ rcon;
      temp[1] = aesSbox(temp[2]);
      temp[2] = aesSbox(temp[3]);
      temp[3] = aesSbox(first);
      rcon = xtime(rcon);
    }

    for (std::size_t i = 0; i < temp.size(); ++i) {
      expanded[bytesGenerated] =
          expanded[bytesGenerated - 16] ^ temp[i];
      ++bytesGenerated;
    }
  }
  return expanded;
}

Block16 xorBlocks(const Block16 &left, const Block16 &right) {
  Block16 result{};
  for (std::size_t i = 0; i < result.size(); ++i) {
    result[i] = left[i] ^ right[i];
  }
  return result;
}

Block16 leftShiftOneBit(const Block16 &input) {
  Block16 result{};
  std::uint8_t carry = 0;
  for (std::size_t i = input.size(); i > 0; --i) {
    const auto byte = input[i - 1];
    result[i - 1] = static_cast<std::uint8_t>((byte << 1) | carry);
    carry = (byte & 0x80) != 0 ? 1 : 0;
  }
  return result;
}

Block16 cmacSubkeyDouble(const Block16 &input) {
  auto result = leftShiftOneBit(input);
  if ((input[0] & 0x80) != 0) {
    result[15] ^= 0x87;
  }
  return result;
}

Block16 blockFromBytes(const ByteVector &bytes, std::size_t offset) {
  Block16 block{};
  std::copy_n(bytes.begin() + offset, block.size(), block.begin());
  return block;
}

Block16 paddedFinalBlock(const ByteVector &bytes, std::size_t offset) {
  Block16 block{};
  const auto remaining = bytes.size() - offset;
  std::copy_n(bytes.begin() + offset, remaining, block.begin());
  block[remaining] = 0x80;
  return block;
}

ByteVector asciiWithNull(const char *text) {
  ByteVector bytes;
  while (*text != '\0') {
    bytes.push_back(static_cast<std::uint8_t>(*text++));
  }
  bytes.push_back(0);
  return bytes;
}

} // namespace

Digest32 sha256(const ByteVector &data) {
  static constexpr std::uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
      0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
      0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
      0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
      0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

  auto bytes = paddedSha256Message(data);
  std::uint32_t h0 = 0x6a09e667;
  std::uint32_t h1 = 0xbb67ae85;
  std::uint32_t h2 = 0x3c6ef372;
  std::uint32_t h3 = 0xa54ff53a;
  std::uint32_t h4 = 0x510e527f;
  std::uint32_t h5 = 0x9b05688c;
  std::uint32_t h6 = 0x1f83d9ab;
  std::uint32_t h7 = 0x5be0cd19;

  for (std::size_t offset = 0; offset < bytes.size();
       offset += kSha256BlockSize) {
    std::uint32_t w[64]{};
    for (std::size_t i = 0; i < 16; ++i) {
      w[i] = readU32Be(bytes.data() + offset + i * 4);
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
      const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                      (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    auto a = h0;
    auto b = h1;
    auto c = h2;
    auto d = h3;
    auto e = h4;
    auto f = h5;
    auto g = h6;
    auto h = h7;

    for (std::size_t i = 0; i < 64; ++i) {
      const auto s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const auto ch = (e & f) ^ (~e & g);
      const auto temp1 = h + s1 + ch + k[i] + w[i];
      const auto s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const auto maj = (a & b) ^ (a & c) ^ (b & c);
      const auto temp2 = s0 + maj;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
    h5 += f;
    h6 += g;
    h7 += h;
  }

  Digest32 digest{};
  writeU32Be(digest, 0, h0);
  writeU32Be(digest, 4, h1);
  writeU32Be(digest, 8, h2);
  writeU32Be(digest, 12, h3);
  writeU32Be(digest, 16, h4);
  writeU32Be(digest, 20, h5);
  writeU32Be(digest, 24, h6);
  writeU32Be(digest, 28, h7);
  return digest;
}

Digest32 hmacSha256(const ByteVector &key, const ByteVector &data) {
  ByteVector normalizedKey = key;
  if (normalizedKey.size() > kSha256BlockSize) {
    normalizedKey = bytesFromDigest(sha256(normalizedKey));
  }
  normalizedKey.resize(kSha256BlockSize, 0);

  ByteVector innerPad(kSha256BlockSize);
  ByteVector outerPad(kSha256BlockSize);
  for (std::size_t i = 0; i < kSha256BlockSize; ++i) {
    innerPad[i] = normalizedKey[i] ^ 0x36;
    outerPad[i] = normalizedKey[i] ^ 0x5C;
  }

  ByteVector inner = innerPad;
  inner.insert(inner.end(), data.begin(), data.end());
  const auto innerDigest = sha256(inner);

  ByteVector outer = outerPad;
  const auto innerBytes = bytesFromDigest(innerDigest);
  outer.insert(outer.end(), innerBytes.begin(), innerBytes.end());
  return sha256(outer);
}

Block16 aes128EncryptBlock(const Block16 &key, const Block16 &plaintext) {
  auto state = plaintext;
  const auto expandedKey = expandAes128Key(key);
  addRoundKey(state, expandedKey.data());

  for (int round = 1; round <= 9; ++round) {
    subBytes(state);
    shiftRows(state);
    mixColumns(state);
    addRoundKey(state, expandedKey.data() + round * 16);
  }

  subBytes(state);
  shiftRows(state);
  addRoundKey(state, expandedKey.data() + 160);
  return state;
}

DecodeResult<Block16> aes128Cmac(const ByteVector &key,
                                 const ByteVector &data) {
  if (key.size() != kAesBlockSize) {
    return DecodeResult<Block16>::failure(
        ErrorCode::UnsupportedCapability,
        "AES-128-CMAC requires a 16-byte signing key.");
  }

  Block16 keyBlock{};
  std::copy_n(key.begin(), keyBlock.size(), keyBlock.begin());
  const auto zero = Block16{};
  const auto l = aes128EncryptBlock(keyBlock, zero);
  const auto k1 = cmacSubkeyDouble(l);
  const auto k2 = cmacSubkeyDouble(k1);

  const bool completeFinalBlock =
      !data.empty() && (data.size() % kAesBlockSize) == 0;
  const auto blockCount =
      completeFinalBlock ? data.size() / kAesBlockSize
                         : (data.size() / kAesBlockSize) + 1;

  Block16 finalBlock =
      completeFinalBlock
          ? blockFromBytes(data, (blockCount - 1) * kAesBlockSize)
          : paddedFinalBlock(data, (blockCount - 1) * kAesBlockSize);
  finalBlock = xorBlocks(finalBlock, completeFinalBlock ? k1 : k2);

  Block16 state{};
  for (std::size_t block = 0; block + 1 < blockCount; ++block) {
    state = aes128EncryptBlock(
        keyBlock, xorBlocks(state, blockFromBytes(data, block * kAesBlockSize)));
  }
  return DecodeResult<Block16>::success(
      aes128EncryptBlock(keyBlock, xorBlocks(state, finalBlock)));
}

DecodeResult<ByteVector> deriveSmb3SigningKey(const ByteVector &sessionKey,
                                              Dialect dialect) {
  if (dialect != Dialect::Smb300 && dialect != Dialect::Smb302) {
    return unsupportedSigning(dialect);
  }
  if (sessionKey.empty()) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::AuthenticationFailed,
        "Cannot derive SMB3 signing key without a session key.");
  }

  auto label = asciiWithNull("SMB2AESCMAC");
  auto context = asciiWithNull("SmbSign");
  ByteVector input;
  appendU32Be(input, 1);
  input.insert(input.end(), label.begin(), label.end());
  input.push_back(0);
  input.insert(input.end(), context.begin(), context.end());
  appendU32Be(input, 128);

  const auto digest = hmacSha256(sessionKey, input);
  return DecodeResult<ByteVector>::success(
      ByteVector(digest.begin(), digest.begin() + kAesBlockSize));
}

bool supportsHmacSha256Signing(Dialect dialect) {
  return dialect == Dialect::Smb202 || dialect == Dialect::Smb210;
}

bool supportsAesCmacSigning(Dialect dialect) {
  return dialect == Dialect::Smb300 || dialect == Dialect::Smb302;
}

bool supportsSigning(Dialect dialect) {
  return supportsHmacSha256Signing(dialect) ||
         supportsAesCmacSigning(dialect);
}

DecodeResult<ByteVector> signSmb2DirectTcpFrame(const ByteVector &requestFrame,
                                                const ByteVector &sessionKey,
                                                Dialect dialect) {
  if (!supportsSigning(dialect)) {
    return unsupportedSigning(dialect);
  }

  auto payload = decodeDirectTcpPayload(requestFrame);
  if (!payload.ok) {
    return DecodeResult<ByteVector>::failure(payload.error.code,
                                             payload.error.message);
  }
  if (payload.value.size() < kSmb2HeaderSize) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::ProtocolUnsupported, "SMB2 message is shorter than header.");
  }

  const auto flags = readU32Le(payload.value, kSmb2FlagsOffset) | kFlagSigned;
  writeU32Le(payload.value, kSmb2FlagsOffset, flags);
  zeroSmb2Signature(payload.value);
  if (supportsHmacSha256Signing(dialect)) {
    const auto digest = hmacSha256(sessionKey, payload.value);
    std::copy_n(digest.begin(), 16,
                payload.value.begin() + kSmb2SignatureOffset);
  } else {
    const auto digest = aes128Cmac(sessionKey, payload.value);
    if (!digest.ok) {
      return DecodeResult<ByteVector>::failure(digest.error.code,
                                               digest.error.message);
    }
    std::copy_n(digest.value.begin(), 16,
                payload.value.begin() + kSmb2SignatureOffset);
  }
  return DecodeResult<ByteVector>::success(encodeDirectTcpFrame(payload.value));
}

DecodeResult<bool> verifySmb2DirectTcpFrameSignature(
    const ByteVector &responseFrame, const ByteVector &sessionKey,
    Dialect dialect) {
  if (!supportsSigning(dialect)) {
    return unsupportedSigningVerification(dialect);
  }

  auto payload = decodeDirectTcpPayload(responseFrame);
  if (!payload.ok) {
    return DecodeResult<bool>::failure(payload.error.code,
                                       payload.error.message);
  }
  if (payload.value.size() < kSmb2HeaderSize) {
    return DecodeResult<bool>::failure(
        ErrorCode::ProtocolUnsupported, "SMB2 response is shorter than header.");
  }

  const auto flags = readU32Le(payload.value, kSmb2FlagsOffset);
  if ((flags & kFlagSigned) == 0) {
    return DecodeResult<bool>::success(false);
  }

  std::array<std::uint8_t, 16> expected{};
  std::copy_n(payload.value.begin() + kSmb2SignatureOffset, 16,
              expected.begin());
  zeroSmb2Signature(payload.value);
  bool valid = false;
  if (supportsHmacSha256Signing(dialect)) {
    const auto digest = hmacSha256(sessionKey, payload.value);
    valid = std::equal(expected.begin(), expected.end(), digest.begin());
  } else {
    const auto digest = aes128Cmac(sessionKey, payload.value);
    if (!digest.ok) {
      return DecodeResult<bool>::failure(digest.error.code,
                                         digest.error.message);
    }
    valid = std::equal(expected.begin(), expected.end(), digest.value.begin());
  }
  return DecodeResult<bool>::success(valid);
}

std::string toHex(const Digest32 &digest) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const auto byte : digest) {
    stream << std::setw(2) << static_cast<int>(byte);
  }
  return stream.str();
}

std::string toHex16(const Block16 &digest) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const auto byte : digest) {
    stream << std::setw(2) << static_cast<int>(byte);
  }
  return stream.str();
}

SigningTransport::SigningTransport(std::unique_ptr<Transport> inner,
                                   ByteVector sessionKey, Dialect dialect,
                                   bool verifyResponses)
    : m_inner(std::move(inner)), m_sessionKey(std::move(sessionKey)),
      m_dialect(dialect), m_verifyResponses(verifyResponses) {}

DecodeResult<ByteVector>
SigningTransport::exchange(const ByteVector &requestFrame,
                           const OperationContext &context) {
  if (!m_inner) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::InternalError, "Signing transport has no inner transport.");
  }

  const auto signedRequest =
      signSmb2DirectTcpFrame(requestFrame, m_sessionKey, m_dialect);
  if (!signedRequest.ok) {
    return signedRequest;
  }

  auto response = m_inner->exchange(signedRequest.value, context);
  if (!response.ok || !m_verifyResponses) {
    return response;
  }

  const auto verified =
      verifySmb2DirectTcpFrameSignature(response.value, m_sessionKey, m_dialect);
  if (!verified.ok) {
    return DecodeResult<ByteVector>::failure(verified.error.code,
                                             verified.error.message);
  }
  if (!verified.value) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB2 response is not signed or has an invalid signature.");
  }
  return response;
}

} // namespace smb::native_smb
