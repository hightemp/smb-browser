#include "SpnegoToken.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace smb::native_smb {
namespace {

constexpr std::uint8_t kDerSequence = 0x30;
constexpr std::uint8_t kDerOctetString = 0x04;
constexpr std::uint8_t kDerObjectIdentifier = 0x06;
constexpr std::uint8_t kDerApplication0 = 0x60;
constexpr std::uint8_t kDerContext0 = 0xA0;
constexpr std::uint8_t kDerContext1 = 0xA1;
constexpr std::uint8_t kDerContext2 = 0xA2;

const ByteVector kSpnegoOid{0x2B, 0x06, 0x01, 0x05, 0x05, 0x02};
const ByteVector kNtlmOid{0x2B, 0x06, 0x01, 0x04, 0x01,
                          0x82, 0x37, 0x02, 0x02, 0x0A};

void appendDerLength(ByteVector &bytes, std::size_t length) {
  if (length < 0x80) {
    bytes.push_back(static_cast<std::uint8_t>(length));
    return;
  }

  ByteVector encoded;
  while (length > 0) {
    encoded.push_back(static_cast<std::uint8_t>(length & 0xFF));
    length >>= 8;
  }
  bytes.push_back(static_cast<std::uint8_t>(0x80 | encoded.size()));
  for (auto it = encoded.rbegin(); it != encoded.rend(); ++it) {
    bytes.push_back(*it);
  }
}

ByteVector derTlv(std::uint8_t tag, const ByteVector &content) {
  ByteVector bytes;
  bytes.reserve(2 + content.size());
  bytes.push_back(tag);
  appendDerLength(bytes, content.size());
  bytes.insert(bytes.end(), content.begin(), content.end());
  return bytes;
}

ByteVector derOid(const ByteVector &oid) {
  return derTlv(kDerObjectIdentifier, oid);
}

ByteVector derOctetString(const ByteVector &value) {
  return derTlv(kDerOctetString, value);
}

ByteVector derSequence(const ByteVector &value) {
  return derTlv(kDerSequence, value);
}

ByteVector derContext(std::uint8_t tag, const ByteVector &value) {
  return derTlv(tag, value);
}

bool startsWithNtlmSignature(const ByteVector &bytes) {
  constexpr std::array<std::uint8_t, 8> kSignature{
      'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};
  return bytes.size() >= kSignature.size() &&
         std::equal(kSignature.begin(), kSignature.end(), bytes.begin());
}

DecodeResult<std::size_t> readDerLength(const ByteVector &bytes,
                                        std::size_t &offset) {
  if (offset >= bytes.size()) {
    return DecodeResult<std::size_t>::failure(
        ErrorCode::ProtocolUnsupported, "SPNEGO DER length is truncated.");
  }

  const auto first = bytes[offset++];
  if ((first & 0x80) == 0) {
    return DecodeResult<std::size_t>::success(first);
  }

  const auto count = first & 0x7F;
  if (count == 0 || count > sizeof(std::size_t) ||
      offset + count > bytes.size()) {
    return DecodeResult<std::size_t>::failure(
        ErrorCode::ProtocolUnsupported, "SPNEGO DER length is invalid.");
  }

  std::size_t length = 0;
  for (std::uint8_t i = 0; i < count; ++i) {
    length = (length << 8) | bytes[offset++];
  }
  return DecodeResult<std::size_t>::success(length);
}

DecodeResult<ByteVector> findNtlmOctetString(const ByteVector &bytes,
                                             int depth) {
  if (depth > 16) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::ProtocolUnsupported, "SPNEGO DER nesting is too deep.");
  }
  if (startsWithNtlmSignature(bytes)) {
    return DecodeResult<ByteVector>::success(bytes);
  }

  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto tag = bytes[offset++];
    auto length = readDerLength(bytes, offset);
    if (!length.ok) {
      return DecodeResult<ByteVector>::failure(length.error.code,
                                               length.error.message);
    }
    if (offset + length.value > bytes.size()) {
      return DecodeResult<ByteVector>::failure(
          ErrorCode::ProtocolUnsupported, "SPNEGO DER value is truncated.");
    }

    ByteVector content(bytes.begin() + offset,
                       bytes.begin() + offset + length.value);
    if (tag == kDerOctetString && startsWithNtlmSignature(content)) {
      return DecodeResult<ByteVector>::success(std::move(content));
    }
    if ((tag & 0x20) != 0 || tag == kDerSequence ||
        tag == kDerApplication0) {
      auto nested = findNtlmOctetString(content, depth + 1);
      if (nested.ok) {
        return nested;
      }
      if (nested.error.code != ErrorCode::ProtocolUnsupported) {
        return nested;
      }
    }
    offset += length.value;
  }

  return DecodeResult<ByteVector>::failure(
      ErrorCode::ProtocolUnsupported, "SPNEGO token does not contain NTLMSSP.");
}

} // namespace

ByteVector buildSpnegoNegTokenInit(const ByteVector &ntlmToken) {
  const auto mechTypes = derContext(kDerContext0, derSequence(derOid(kNtlmOid)));
  const auto mechToken = derContext(kDerContext2, derOctetString(ntlmToken));

  ByteVector initSequence;
  initSequence.insert(initSequence.end(), mechTypes.begin(), mechTypes.end());
  initSequence.insert(initSequence.end(), mechToken.begin(), mechToken.end());

  const auto negotiationToken =
      derContext(kDerContext0, derSequence(initSequence));
  ByteVector applicationContent = derOid(kSpnegoOid);
  applicationContent.insert(applicationContent.end(), negotiationToken.begin(),
                            negotiationToken.end());
  return derTlv(kDerApplication0, applicationContent);
}

ByteVector buildSpnegoNegTokenResp(const ByteVector &ntlmToken) {
  const auto responseToken = derContext(kDerContext2, derOctetString(ntlmToken));
  return derContext(kDerContext1, derSequence(responseToken));
}

DecodeResult<ByteVector> unwrapSpnegoNtlmToken(const ByteVector &token) {
  return findNtlmOctetString(token, 0);
}

} // namespace smb::native_smb
