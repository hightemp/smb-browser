#include "Smb3Encryption.h"

#include "Smb2Signing.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace smb::native_smb {
namespace {

constexpr std::uint32_t kSmb3TransformProtocolId = 0x424D53FD;
constexpr std::size_t kSmb3TransformHeaderSize = 52;
constexpr std::size_t kSmb3TransformSignatureOffset = 4;
constexpr std::size_t kSmb3TransformNonceOffset = 20;
constexpr std::size_t kSmb3TransformAadOffset = 20;
constexpr std::size_t kAesBlockSize = 16;
constexpr std::size_t kSmb3AesCcmNonceSize = 11;
constexpr std::size_t kSmb3AesCcmTagSize = 16;
constexpr std::uint16_t kSmb3Aes128CcmAlgorithm = 0x0001;

void appendU16Be(ByteVector &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void appendU32Be(ByteVector &bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void appendU16Le(ByteVector &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void appendU32Le(ByteVector &bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

void appendU64Le(ByteVector &bytes, std::uint64_t value) {
  for (int shift = 0; shift <= 56; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

std::uint16_t readU16Le(const ByteVector &bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[offset]) |
      (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

std::uint32_t readU32Le(const ByteVector &bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint64_t readU64Le(const ByteVector &bytes, std::size_t offset) {
  std::uint64_t value = 0;
  for (int shift = 0; shift <= 56; shift += 8) {
    value |= static_cast<std::uint64_t>(bytes[offset++]) << shift;
  }
  return value;
}

ByteVector asciiWithNull(const char *text) {
  ByteVector bytes;
  while (*text != '\0') {
    bytes.push_back(static_cast<std::uint8_t>(*text++));
  }
  bytes.push_back(0);
  return bytes;
}

ByteVector xorWithBlock(const ByteVector &left, const Block16 &right,
                        std::size_t offset, std::size_t count) {
  ByteVector result(count);
  for (std::size_t i = 0; i < count; ++i) {
    result[i] = static_cast<std::uint8_t>(left[offset + i] ^ right[i]);
  }
  return result;
}

Block16 xorBlocks(const Block16 &left, const Block16 &right) {
  Block16 result{};
  for (std::size_t i = 0; i < result.size(); ++i) {
    result[i] = left[i] ^ right[i];
  }
  return result;
}

Block16 blockFromBytes(const ByteVector &bytes, std::size_t offset) {
  Block16 block{};
  const auto remaining = std::min(block.size(), bytes.size() - offset);
  std::copy_n(bytes.begin() + offset, remaining, block.begin());
  return block;
}

Block16 keyBlockFrom(const ByteVector &key) {
  Block16 block{};
  std::copy_n(key.begin(), block.size(), block.begin());
  return block;
}

ByteVector formatAad(const ByteVector &aad) {
  ByteVector formatted;
  if (aad.empty()) {
    return formatted;
  }
  if (aad.size() >= 0xFF00) {
    return {};
  }

  appendU16Be(formatted, static_cast<std::uint16_t>(aad.size()));
  formatted.insert(formatted.end(), aad.begin(), aad.end());
  while ((formatted.size() % kAesBlockSize) != 0) {
    formatted.push_back(0);
  }
  return formatted;
}

DecodeResult<Block16> cbcMac(const ByteVector &key, const ByteVector &nonce,
                             const ByteVector &plaintext,
                             const ByteVector &aad,
                             std::size_t tagLength) {
  if (key.size() != kAesBlockSize) {
    return DecodeResult<Block16>::failure(
        ErrorCode::UnsupportedCapability,
        "AES-128-CCM requires a 16-byte encryption key.");
  }
  if (nonce.empty() || nonce.size() > 13) {
    return DecodeResult<Block16>::failure(
        ErrorCode::ProtocolUnsupported,
        "AES-CCM nonce length must be between 1 and 13 bytes.");
  }
  if (tagLength < 4 || tagLength > 16 || (tagLength % 2) != 0) {
    return DecodeResult<Block16>::failure(
        ErrorCode::ProtocolUnsupported,
        "AES-CCM tag length must be an even value between 4 and 16 bytes.");
  }

  const auto lengthFieldSize = 15 - nonce.size();
  std::uint64_t maxLength = 1;
  for (std::size_t i = 0; i < lengthFieldSize; ++i) {
    maxLength <<= 8;
  }
  if (plaintext.size() >= maxLength) {
    return DecodeResult<Block16>::failure(
        ErrorCode::ProtocolUnsupported,
        "AES-CCM plaintext is too large for the selected nonce length.");
  }

  Block16 block{};
  block[0] = static_cast<std::uint8_t>(
      (aad.empty() ? 0 : 0x40) |
      ((((tagLength - 2) / 2) & 0x07) << 3) |
      ((lengthFieldSize - 1) & 0x07));
  std::copy(nonce.begin(), nonce.end(), block.begin() + 1);
  auto messageLength = static_cast<std::uint64_t>(plaintext.size());
  for (std::size_t i = 0; i < lengthFieldSize; ++i) {
    block[15 - i] = static_cast<std::uint8_t>(messageLength & 0xFF);
    messageLength >>= 8;
  }

  const auto keyBlock = keyBlockFrom(key);
  auto state = aes128EncryptBlock(keyBlock, block);

  const auto formattedAad = formatAad(aad);
  if (!aad.empty() && formattedAad.empty()) {
    return DecodeResult<Block16>::failure(
        ErrorCode::ProtocolUnsupported,
        "AES-CCM AAD is too large for the implemented encoder.");
  }

  for (std::size_t offset = 0; offset < formattedAad.size();
       offset += kAesBlockSize) {
    state = aes128EncryptBlock(
        keyBlock, xorBlocks(state, blockFromBytes(formattedAad, offset)));
  }

  for (std::size_t offset = 0; offset < plaintext.size();
       offset += kAesBlockSize) {
    state = aes128EncryptBlock(
        keyBlock, xorBlocks(state, blockFromBytes(plaintext, offset)));
  }

  return DecodeResult<Block16>::success(state);
}

Block16 counterBlock(const ByteVector &nonce, std::uint64_t counter) {
  const auto lengthFieldSize = 15 - nonce.size();
  Block16 block{};
  block[0] = static_cast<std::uint8_t>((lengthFieldSize - 1) & 0x07);
  std::copy(nonce.begin(), nonce.end(), block.begin() + 1);
  for (std::size_t i = 0; i < lengthFieldSize; ++i) {
    block[15 - i] = static_cast<std::uint8_t>(counter & 0xFF);
    counter >>= 8;
  }
  return block;
}

ByteVector ccmCrypt(const ByteVector &key, const ByteVector &nonce,
                    const ByteVector &input) {
  const auto keyBlock = keyBlockFrom(key);
  ByteVector output;
  output.reserve(input.size());

  std::uint64_t counter = 1;
  for (std::size_t offset = 0; offset < input.size();
       offset += kAesBlockSize, ++counter) {
    const auto stream =
        aes128EncryptBlock(keyBlock, counterBlock(nonce, counter));
    const auto count = std::min(kAesBlockSize, input.size() - offset);
    auto block = xorWithBlock(input, stream, offset, count);
    output.insert(output.end(), block.begin(), block.end());
  }
  return output;
}

ByteVector ccmTagMask(const ByteVector &key, const ByteVector &nonce) {
  const auto keyBlock = keyBlockFrom(key);
  const auto mask = aes128EncryptBlock(keyBlock, counterBlock(nonce, 0));
  return ByteVector(mask.begin(), mask.end());
}

DecodeResult<ByteVector> deriveKey(const ByteVector &sessionKey,
                                   const char *label,
                                   const char *context) {
  if (sessionKey.empty()) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::AuthenticationFailed,
        "Cannot derive SMB3 encryption key without a session key.");
  }

  auto labelBytes = asciiWithNull(label);
  auto contextBytes = asciiWithNull(context);
  ByteVector input;
  appendU32Be(input, 1);
  input.insert(input.end(), labelBytes.begin(), labelBytes.end());
  input.push_back(0);
  input.insert(input.end(), contextBytes.begin(), contextBytes.end());
  appendU32Be(input, 128);

  const auto digest = hmacSha256(sessionKey, input);
  return DecodeResult<ByteVector>::success(
      ByteVector(digest.begin(), digest.begin() + kAesBlockSize));
}

DecodeResult<ByteVector> unsupportedDialect(Dialect dialect) {
  std::ostringstream stream;
  stream << "SMB3 AES-CCM encryption for dialect 0x" << std::hex
         << std::uppercase << static_cast<std::uint16_t>(dialect)
         << " is not implemented in the clean-room engine.";
  return DecodeResult<ByteVector>::failure(ErrorCode::UnsupportedCapability,
                                           stream.str());
}

ByteVector buildTransformHeader(std::uint32_t originalMessageSize,
                                std::uint64_t sessionId,
                                const Nonce16 &nonce, Dialect dialect) {
  ByteVector bytes;
  bytes.reserve(kSmb3TransformHeaderSize);
  appendU32Le(bytes, kSmb3TransformProtocolId);
  bytes.insert(bytes.end(), kSmb3AesCcmTagSize, 0);
  bytes.insert(bytes.end(), nonce.begin(), nonce.end());
  appendU32Le(bytes, originalMessageSize);
  appendU16Le(bytes, 0);
  appendU16Le(bytes, dialect == Dialect::Smb311 ? 0x0001
                                                : kSmb3Aes128CcmAlgorithm);
  appendU64Le(bytes, sessionId);
  return bytes;
}

ByteVector smb3AadFromHeader(const ByteVector &transformHeader) {
  return ByteVector(transformHeader.begin() + kSmb3TransformAadOffset,
                    transformHeader.begin() + kSmb3TransformHeaderSize);
}

ByteVector ccmNonceFromTransformNonce(const ByteVector &transformPayload) {
  return ByteVector(transformPayload.begin() + kSmb3TransformNonceOffset,
                    transformPayload.begin() + kSmb3TransformNonceOffset +
                        kSmb3AesCcmNonceSize);
}

} // namespace

DecodeResult<AesCcmEncryptedMessage>
aes128CcmEncrypt(const ByteVector &key, const ByteVector &nonce,
                 const ByteVector &plaintext, const ByteVector &aad,
                 std::size_t tagLength) {
  const auto mac = cbcMac(key, nonce, plaintext, aad, tagLength);
  if (!mac.ok) {
    return DecodeResult<AesCcmEncryptedMessage>::failure(mac.error.code,
                                                         mac.error.message);
  }

  const auto mask = ccmTagMask(key, nonce);
  AesCcmEncryptedMessage result;
  result.ciphertext = ccmCrypt(key, nonce, plaintext);
  result.tag.resize(tagLength);
  for (std::size_t i = 0; i < tagLength; ++i) {
    result.tag[i] = mac.value[i] ^ mask[i];
  }
  return DecodeResult<AesCcmEncryptedMessage>::success(std::move(result));
}

DecodeResult<ByteVector> aes128CcmDecrypt(const ByteVector &key,
                                          const ByteVector &nonce,
                                          const ByteVector &ciphertext,
                                          const ByteVector &aad,
                                          const ByteVector &tag) {
  const auto plaintext = ccmCrypt(key, nonce, ciphertext);
  const auto encrypted =
      aes128CcmEncrypt(key, nonce, plaintext, aad, tag.size());
  if (!encrypted.ok) {
    return DecodeResult<ByteVector>::failure(encrypted.error.code,
                                             encrypted.error.message);
  }
  if (encrypted.value.tag.size() != tag.size() ||
      !std::equal(tag.begin(), tag.end(), encrypted.value.tag.begin())) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB3 encrypted message authentication failed.");
  }
  return DecodeResult<ByteVector>::success(std::move(plaintext));
}

DecodeResult<ByteVector> deriveSmb3EncryptionKey(
    const ByteVector &sessionKey, Dialect dialect,
    Smb3KeyDirection direction) {
  if (dialect != Dialect::Smb300 && dialect != Dialect::Smb302) {
    return unsupportedDialect(dialect);
  }
  return deriveKey(sessionKey, "SMB2AESCCM",
                   direction == Smb3KeyDirection::ClientToServer
                       ? "ServerIn "
                       : "ServerOut");
}

DecodeResult<ByteVector> encryptSmb3DirectTcpFrame(
    const ByteVector &requestFrame, const ByteVector &encryptionKey,
    std::uint64_t sessionId, const Nonce16 &nonce, Dialect dialect) {
  if (dialect != Dialect::Smb300 && dialect != Dialect::Smb302) {
    return unsupportedDialect(dialect);
  }

  const auto payload = decodeDirectTcpPayload(requestFrame);
  if (!payload.ok) {
    return DecodeResult<ByteVector>::failure(payload.error.code,
                                             payload.error.message);
  }
  if (payload.value.size() > UINT32_MAX) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB3 transform payload is too large.");
  }

  auto transformHeader = buildTransformHeader(
      static_cast<std::uint32_t>(payload.value.size()), sessionId, nonce,
      dialect);
  const auto aad = smb3AadFromHeader(transformHeader);
  const auto ccmNonce = ByteVector(nonce.begin(),
                                  nonce.begin() + kSmb3AesCcmNonceSize);
  const auto encrypted = aes128CcmEncrypt(
      encryptionKey, ccmNonce, payload.value, aad, kSmb3AesCcmTagSize);
  if (!encrypted.ok) {
    return DecodeResult<ByteVector>::failure(encrypted.error.code,
                                             encrypted.error.message);
  }

  std::copy_n(encrypted.value.tag.begin(), kSmb3AesCcmTagSize,
              transformHeader.begin() + kSmb3TransformSignatureOffset);
  transformHeader.insert(transformHeader.end(),
                         encrypted.value.ciphertext.begin(),
                         encrypted.value.ciphertext.end());
  return DecodeResult<ByteVector>::success(
      encodeDirectTcpFrame(transformHeader));
}

DecodeResult<ByteVector>
decryptSmb3DirectTcpFrame(const ByteVector &responseFrame,
                          const ByteVector &decryptionKey,
                          std::uint64_t expectedSessionId, Dialect dialect) {
  if (dialect != Dialect::Smb300 && dialect != Dialect::Smb302) {
    return unsupportedDialect(dialect);
  }

  const auto payload = decodeDirectTcpPayload(responseFrame);
  if (!payload.ok) {
    return DecodeResult<ByteVector>::failure(payload.error.code,
                                             payload.error.message);
  }
  if (payload.value.size() < kSmb3TransformHeaderSize ||
      readU32Le(payload.value, 0) != kSmb3TransformProtocolId) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB3 response is not an encrypted transform message.");
  }

  const auto originalMessageSize = readU32Le(payload.value, 36);
  const auto algorithm = readU16Le(payload.value, 42);
  const auto sessionId = readU64Le(payload.value, 44);
  if (algorithm != kSmb3Aes128CcmAlgorithm) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::UnsupportedCapability,
        "SMB3 response uses an unsupported encryption algorithm.");
  }
  if (sessionId != expectedSessionId) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB3 response transform session id does not match the session.");
  }

  const auto aad = smb3AadFromHeader(payload.value);
  const auto ccmNonce = ccmNonceFromTransformNonce(payload.value);
  const auto tag =
      ByteVector(payload.value.begin() + kSmb3TransformSignatureOffset,
                 payload.value.begin() + kSmb3TransformSignatureOffset +
                     kSmb3AesCcmTagSize);
  const auto ciphertext =
      ByteVector(payload.value.begin() + kSmb3TransformHeaderSize,
                 payload.value.end());
  const auto plaintext =
      aes128CcmDecrypt(decryptionKey, ccmNonce, ciphertext, aad, tag);
  if (!plaintext.ok) {
    return plaintext;
  }
  if (plaintext.value.size() != originalMessageSize) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB3 decrypted message size does not match transform header.");
  }
  return DecodeResult<ByteVector>::success(
      encodeDirectTcpFrame(plaintext.value));
}

Smb3EncryptionTransport::Smb3EncryptionTransport(
    std::unique_ptr<Transport> inner, ByteVector encryptionKey,
    ByteVector decryptionKey, std::uint64_t sessionId, Dialect dialect)
    : m_inner(std::move(inner)), m_encryptionKey(std::move(encryptionKey)),
      m_decryptionKey(std::move(decryptionKey)), m_sessionId(sessionId),
      m_dialect(dialect) {}

DecodeResult<ByteVector>
Smb3EncryptionTransport::exchange(const ByteVector &requestFrame,
                                  const OperationContext &context) {
  if (!m_inner) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::InternalError,
        "SMB3 encryption transport has no inner transport.");
  }

  const auto encryptedRequest = encryptSmb3DirectTcpFrame(
      requestFrame, m_encryptionKey, m_sessionId, nextNonce(), m_dialect);
  if (!encryptedRequest.ok) {
    return encryptedRequest;
  }

  const auto encryptedResponse =
      m_inner->exchange(encryptedRequest.value, context);
  if (!encryptedResponse.ok) {
    return encryptedResponse;
  }

  return decryptSmb3DirectTcpFrame(encryptedResponse.value, m_decryptionKey,
                                   m_sessionId, m_dialect);
}

Nonce16 Smb3EncryptionTransport::nextNonce() {
  Nonce16 nonce{};
  auto value = m_nonceCounter++;
  for (int index = 0; index < 8; ++index) {
    nonce[static_cast<std::size_t>(index)] =
        static_cast<std::uint8_t>((value >> (index * 8)) & 0xFF);
  }
  return nonce;
}

} // namespace smb::native_smb
