#include "NtlmMessages.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace smb::native_smb {
namespace {

constexpr std::uint32_t kNtlmNegotiateMessageType = 0x00000001;
constexpr std::uint32_t kNtlmChallengeMessageType = 0x00000002;
constexpr std::uint32_t kNtlmAuthenticateMessageType = 0x00000003;

void appendU16Le(ByteVector &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void appendU32Le(ByteVector &bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

std::uint16_t readU16Le(const std::uint8_t *data) {
  return static_cast<std::uint16_t>(data[0]) |
         static_cast<std::uint16_t>(data[1] << 8);
}

std::uint32_t readU32Le(const std::uint8_t *data) {
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) |
         (static_cast<std::uint32_t>(data[3]) << 24);
}

void appendSignature(ByteVector &bytes) {
  bytes.push_back('N');
  bytes.push_back('T');
  bytes.push_back('L');
  bytes.push_back('M');
  bytes.push_back('S');
  bytes.push_back('S');
  bytes.push_back('P');
  bytes.push_back(0);
}

bool hasSignature(const ByteVector &bytes) {
  constexpr std::array<std::uint8_t, 8> kSignature{
      'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};
  return bytes.size() >= kSignature.size() &&
         std::equal(kSignature.begin(), kSignature.end(), bytes.begin());
}

ByteVector encodeUnicodeUpper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  return encodeUtf16Le(text);
}

void appendSecurityBuffer(ByteVector &bytes, std::uint16_t length,
                          std::uint32_t offset) {
  appendU16Le(bytes, length);
  appendU16Le(bytes, length);
  appendU32Le(bytes, length == 0 ? 0 : offset);
}

void appendPayload(ByteVector &payload, const ByteVector &field) {
  payload.insert(payload.end(), field.begin(), field.end());
}

std::uint16_t checkedFieldSize(std::size_t size, const char *fieldName) {
  if (size > std::numeric_limits<std::uint16_t>::max()) {
    throw std::invalid_argument(std::string("NTLM ") + fieldName +
                                " field is too large.");
  }
  return static_cast<std::uint16_t>(size);
}

DecodeResult<ByteVector> readSecurityBuffer(const ByteVector &bytes,
                                            std::size_t descriptorOffset) {
  if (descriptorOffset + 8 > bytes.size()) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::IoError, "NTLM security buffer descriptor is truncated.");
  }

  const auto length = readU16Le(bytes.data() + descriptorOffset);
  const auto maxLength = readU16Le(bytes.data() + descriptorOffset + 2);
  const auto offset = readU32Le(bytes.data() + descriptorOffset + 4);
  if (length > maxLength) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::IoError, "NTLM security buffer length exceeds max length.");
  }
  if (length == 0) {
    return DecodeResult<ByteVector>::success({});
  }

  const auto end = static_cast<std::size_t>(offset) + length;
  if (offset < 32 || end > bytes.size() || end < offset) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::IoError, "NTLM security buffer is out of bounds.");
  }
  return DecodeResult<ByteVector>::success(
      ByteVector(bytes.begin() + offset, bytes.begin() + end));
}

} // namespace

ByteVector buildNtlmNegotiateMessage(const NtlmNegotiateOptions &options) {
  const auto domain = encodeUnicodeUpper(options.domain);
  const auto workstation = encodeUnicodeUpper(options.workstation);
  if (domain.size() > std::numeric_limits<std::uint16_t>::max() ||
      workstation.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw std::invalid_argument("NTLM negotiate identity field is too large.");
  }

  constexpr std::uint32_t kPayloadOffset = 40;
  const auto domainOffset = kPayloadOffset;
  const auto workstationOffset =
      static_cast<std::uint32_t>(domainOffset + domain.size());

  ByteVector bytes;
  bytes.reserve(kPayloadOffset + domain.size() + workstation.size());
  appendSignature(bytes);
  appendU32Le(bytes, kNtlmNegotiateMessageType);
  appendU32Le(bytes, options.flags);
  appendSecurityBuffer(bytes, static_cast<std::uint16_t>(domain.size()),
                       domainOffset);
  appendSecurityBuffer(bytes, static_cast<std::uint16_t>(workstation.size()),
                       workstationOffset);
  bytes.insert(bytes.end(), options.version.begin(), options.version.end());
  bytes.insert(bytes.end(), domain.begin(), domain.end());
  bytes.insert(bytes.end(), workstation.begin(), workstation.end());
  return bytes;
}

ByteVector
buildNtlmAuthenticateMessage(const NtlmAuthenticateOptions &options) {
  const auto domain = encodeUtf16Le(options.domain);
  const auto username = encodeUtf16Le(options.username);
  const auto workstation = encodeUtf16Le(options.workstation);
  const auto lmLength =
      checkedFieldSize(options.lmChallengeResponse.size(), "LM response");
  const auto ntLength =
      checkedFieldSize(options.ntChallengeResponse.size(), "NT response");
  const auto domainLength = checkedFieldSize(domain.size(), "domain");
  const auto usernameLength = checkedFieldSize(username.size(), "username");
  const auto workstationLength =
      checkedFieldSize(workstation.size(), "workstation");
  const auto sessionKeyLength = checkedFieldSize(
      options.encryptedRandomSessionKey.size(), "encrypted session key");

  const auto payloadOffset =
      static_cast<std::uint32_t>(64 + (options.includeVersion ? 8 : 0) +
                                 (options.includeMic ? 16 : 0));
  auto nextOffset = payloadOffset;
  const auto domainOffset = nextOffset;
  nextOffset += domainLength;
  const auto usernameOffset = nextOffset;
  nextOffset += usernameLength;
  const auto workstationOffset = nextOffset;
  nextOffset += workstationLength;
  const auto lmOffset = nextOffset;
  nextOffset += lmLength;
  const auto ntOffset = nextOffset;
  nextOffset += ntLength;
  const auto sessionKeyOffset = nextOffset;
  nextOffset += sessionKeyLength;

  ByteVector bytes;
  bytes.reserve(nextOffset);
  appendSignature(bytes);
  appendU32Le(bytes, kNtlmAuthenticateMessageType);
  appendSecurityBuffer(bytes, lmLength, lmOffset);
  appendSecurityBuffer(bytes, ntLength, ntOffset);
  appendSecurityBuffer(bytes, domainLength, domainOffset);
  appendSecurityBuffer(bytes, usernameLength, usernameOffset);
  appendSecurityBuffer(bytes, workstationLength, workstationOffset);
  appendSecurityBuffer(bytes, sessionKeyLength, sessionKeyOffset);
  appendU32Le(bytes, options.flags);
  if (options.includeVersion) {
    bytes.insert(bytes.end(), options.version.begin(), options.version.end());
  }
  if (options.includeMic) {
    bytes.insert(bytes.end(), options.mic.begin(), options.mic.end());
  }

  appendPayload(bytes, domain);
  appendPayload(bytes, username);
  appendPayload(bytes, workstation);
  appendPayload(bytes, options.lmChallengeResponse);
  appendPayload(bytes, options.ntChallengeResponse);
  appendPayload(bytes, options.encryptedRandomSessionKey);
  return bytes;
}

DecodeResult<NtlmChallengeMessage>
decodeNtlmChallengeMessage(const ByteVector &bytes) {
  if (bytes.size() < 48) {
    return DecodeResult<NtlmChallengeMessage>::failure(
        ErrorCode::IoError, "NTLM CHALLENGE_MESSAGE is shorter than 48 bytes.");
  }
  if (!hasSignature(bytes)) {
    return DecodeResult<NtlmChallengeMessage>::failure(
        ErrorCode::ProtocolUnsupported, "Invalid NTLMSSP signature.");
  }
  if (readU32Le(bytes.data() + 8) != kNtlmChallengeMessageType) {
    return DecodeResult<NtlmChallengeMessage>::failure(
        ErrorCode::ProtocolUnsupported,
        "NTLM message is not a CHALLENGE_MESSAGE.");
  }

  const auto targetName = readSecurityBuffer(bytes, 12);
  if (!targetName.ok) {
    return DecodeResult<NtlmChallengeMessage>::failure(
        targetName.error.code, targetName.error.message);
  }

  const auto flags = readU32Le(bytes.data() + 20);
  const auto targetInfo = readSecurityBuffer(bytes, 40);
  if (!targetInfo.ok) {
    return DecodeResult<NtlmChallengeMessage>::failure(
        targetInfo.error.code, targetInfo.error.message);
  }

  NtlmChallengeMessage message;
  message.flags = flags;
  std::copy(bytes.begin() + 24, bytes.begin() + 32,
            message.serverChallenge.begin());
  message.targetInfo = targetInfo.value;
  message.hasTargetInfo = !message.targetInfo.empty();

  const auto decodedTargetName = decodeUtf16Le(targetName.value);
  if (!decodedTargetName.ok) {
    return DecodeResult<NtlmChallengeMessage>::failure(
        decodedTargetName.error.code, decodedTargetName.error.message);
  }
  message.targetName = decodedTargetName.value;
  return DecodeResult<NtlmChallengeMessage>::success(std::move(message));
}

} // namespace smb::native_smb
