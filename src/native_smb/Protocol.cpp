#include "Protocol.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace smb::native_smb {
namespace {

constexpr std::uint16_t kSigningEnabled = 0x0001;
constexpr std::uint16_t kSigningRequired = 0x0002;
constexpr std::uint32_t kMaxDirectTcpPayloadLength = 0x00FFFFFF;

void appendU16Le(ByteVector &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void appendU32Le(ByteVector &bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

void appendU64Le(ByteVector &bytes, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

void appendUtf16CodeUnit(ByteVector &bytes, std::uint16_t value) {
  appendU16Le(bytes, value);
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

std::uint64_t readU64Le(const std::uint8_t *data) {
  std::uint64_t value = 0;
  for (int index = 7; index >= 0; --index) {
    value <<= 8;
    value |= data[index];
  }
  return value;
}

bool isOfferedDialect(Dialect dialect, const std::vector<Dialect> &offered) {
  return std::find(offered.begin(), offered.end(), dialect) != offered.end();
}

std::string uncPath(const TreeConnectRequestOptions &options) {
  if (options.server.empty() || options.share.empty()) {
    throw std::invalid_argument("SMB tree connect requires server and share.");
  }
  return "\\\\" + options.server + "\\" + options.share;
}

} // namespace

std::uint32_t capabilityMask(std::initializer_list<GlobalCapability> values) {
  std::uint32_t mask = 0;
  for (const auto value : values) {
    mask |= static_cast<std::uint32_t>(value);
  }
  return mask;
}

std::uint16_t securityModeForPolicy(SecurityPolicy policy) {
  switch (policy) {
  case SecurityPolicy::Required:
    return kSigningEnabled | kSigningRequired;
  case SecurityPolicy::Preferred:
    return kSigningEnabled;
  case SecurityPolicy::Disabled:
    return 0;
  }
  return kSigningEnabled | kSigningRequired;
}

std::vector<Dialect> defaultInitialDialects() {
  return {Dialect::Smb202, Dialect::Smb210, Dialect::Smb300,
          Dialect::Smb302};
}

ByteVector encodeUtf16Le(std::string_view text) {
  ByteVector bytes;
  bytes.reserve(text.size() * 2);

  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<std::uint8_t>(text[i]);
    std::uint32_t codePoint = 0;
    std::size_t consumed = 0;

    if ((first & 0x80) == 0) {
      codePoint = first;
      consumed = 1;
    } else if ((first & 0xE0) == 0xC0 && i + 1 < text.size()) {
      const auto second = static_cast<std::uint8_t>(text[i + 1]);
      if ((second & 0xC0) != 0x80) {
        throw std::invalid_argument("Invalid UTF-8 continuation byte.");
      }
      codePoint = ((first & 0x1F) << 6) | (second & 0x3F);
      consumed = 2;
    } else if ((first & 0xF0) == 0xE0 && i + 2 < text.size()) {
      const auto second = static_cast<std::uint8_t>(text[i + 1]);
      const auto third = static_cast<std::uint8_t>(text[i + 2]);
      if ((second & 0xC0) != 0x80 || (third & 0xC0) != 0x80) {
        throw std::invalid_argument("Invalid UTF-8 continuation byte.");
      }
      codePoint = ((first & 0x0F) << 12) | ((second & 0x3F) << 6) |
                  (third & 0x3F);
      consumed = 3;
    } else if ((first & 0xF8) == 0xF0 && i + 3 < text.size()) {
      const auto second = static_cast<std::uint8_t>(text[i + 1]);
      const auto third = static_cast<std::uint8_t>(text[i + 2]);
      const auto fourth = static_cast<std::uint8_t>(text[i + 3]);
      if ((second & 0xC0) != 0x80 || (third & 0xC0) != 0x80 ||
          (fourth & 0xC0) != 0x80) {
        throw std::invalid_argument("Invalid UTF-8 continuation byte.");
      }
      codePoint = ((first & 0x07) << 18) | ((second & 0x3F) << 12) |
                  ((third & 0x3F) << 6) | (fourth & 0x3F);
      consumed = 4;
    } else {
      throw std::invalid_argument("Invalid UTF-8 sequence.");
    }

    if (codePoint <= 0xFFFF) {
      appendUtf16CodeUnit(bytes, static_cast<std::uint16_t>(codePoint));
    } else if (codePoint <= 0x10FFFF) {
      const auto adjusted = codePoint - 0x10000;
      appendUtf16CodeUnit(
          bytes, static_cast<std::uint16_t>(0xD800 + (adjusted >> 10)));
      appendUtf16CodeUnit(
          bytes, static_cast<std::uint16_t>(0xDC00 + (adjusted & 0x3FF)));
    } else {
      throw std::invalid_argument("Unicode code point is out of range.");
    }
    i += consumed;
  }

  return bytes;
}

ByteVector encodeSmb2SyncHeader(const Smb2SyncHeader &header) {
  ByteVector bytes;
  bytes.reserve(kSmb2HeaderSize);

  appendU32Le(bytes, kSmb2ProtocolId);
  appendU16Le(bytes, kSmb2HeaderStructureSize);
  appendU16Le(bytes, header.creditCharge);
  appendU32Le(bytes, header.status);
  appendU16Le(bytes, static_cast<std::uint16_t>(header.command));
  appendU16Le(bytes, header.creditRequest);
  appendU32Le(bytes, header.flags);
  appendU32Le(bytes, header.nextCommand);
  appendU64Le(bytes, header.messageId);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, header.treeId);
  appendU64Le(bytes, header.sessionId);
  bytes.insert(bytes.end(), header.signature.begin(), header.signature.end());

  return bytes;
}

DecodeResult<Smb2SyncHeader> decodeSmb2SyncHeader(const std::uint8_t *data,
                                                  std::size_t size) {
  if (data == nullptr || size < kSmb2HeaderSize) {
    return DecodeResult<Smb2SyncHeader>::failure(
        ErrorCode::IoError, "SMB2 header buffer is shorter than 64 bytes.");
  }

  if (readU32Le(data) != kSmb2ProtocolId) {
    return DecodeResult<Smb2SyncHeader>::failure(
        ErrorCode::ProtocolUnsupported, "Invalid SMB2 protocol identifier.");
  }

  if (readU16Le(data + 4) != kSmb2HeaderStructureSize) {
    return DecodeResult<Smb2SyncHeader>::failure(
        ErrorCode::ProtocolUnsupported, "Invalid SMB2 header structure size.");
  }

  Smb2SyncHeader header;
  header.creditCharge = readU16Le(data + 6);
  header.status = readU32Le(data + 8);
  header.command = static_cast<Command>(readU16Le(data + 12));
  header.creditRequest = readU16Le(data + 14);
  header.flags = readU32Le(data + 16);
  header.nextCommand = readU32Le(data + 20);
  header.messageId = readU64Le(data + 24);
  header.treeId = readU32Le(data + 36);
  header.sessionId = readU64Le(data + 40);
  std::copy(data + 48, data + 64, header.signature.begin());

  return DecodeResult<Smb2SyncHeader>::success(header);
}

DecodeResult<Smb2SyncHeader> decodeSmb2SyncHeader(const ByteVector &bytes) {
  return decodeSmb2SyncHeader(bytes.data(), bytes.size());
}

ByteVector buildNegotiateRequest(const NegotiateRequestOptions &options,
                                 std::uint64_t messageId) {
  const auto dialects =
      options.dialects.empty() ? defaultInitialDialects() : options.dialects;
  if (dialects.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw std::invalid_argument("Too many SMB dialects in negotiate request.");
  }

  Smb2SyncHeader header;
  header.command = Command::Negotiate;
  header.messageId = messageId;
  header.creditRequest = 1;

  auto bytes = encodeSmb2SyncHeader(header);
  appendU16Le(bytes, kNegotiateRequestStructureSize);
  appendU16Le(bytes, static_cast<std::uint16_t>(dialects.size()));
  appendU16Le(bytes, securityModeForPolicy(options.signing));
  appendU16Le(bytes, 0);
  appendU32Le(bytes, options.capabilities);
  bytes.insert(bytes.end(), options.clientGuid.begin(),
               options.clientGuid.end());
  appendU64Le(bytes, 0);
  for (const auto dialect : dialects) {
    appendU16Le(bytes, static_cast<std::uint16_t>(dialect));
  }
  return bytes;
}

DecodeResult<NegotiateResponse>
decodeNegotiateResponse(const std::uint8_t *data, std::size_t size,
                        const std::vector<Dialect> &offeredDialects) {
  const auto headerResult = decodeSmb2SyncHeader(data, size);
  if (!headerResult.ok) {
    return DecodeResult<NegotiateResponse>::failure(
        headerResult.error.code, headerResult.error.message);
  }
  if (headerResult.value.command != Command::Negotiate) {
    return DecodeResult<NegotiateResponse>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB2 response is not a NEGOTIATE response.");
  }
  if ((headerResult.value.flags & kFlagServerToRedir) == 0) {
    return DecodeResult<NegotiateResponse>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB2 NEGOTIATE response is missing server-to-redirector flag.");
  }
  if (size < kSmb2HeaderSize + 64) {
    return DecodeResult<NegotiateResponse>::failure(
        ErrorCode::IoError,
        "SMB2 NEGOTIATE response buffer is shorter than fixed response.");
  }

  const auto *body = data + kSmb2HeaderSize;
  if (readU16Le(body) != kNegotiateResponseStructureSize) {
    return DecodeResult<NegotiateResponse>::failure(
        ErrorCode::ProtocolUnsupported,
        "Invalid SMB2 NEGOTIATE response structure size.");
  }

  const auto rawDialect = readU16Le(body + 4);
  const auto dialect = static_cast<Dialect>(rawDialect);
  if (!isOfferedDialect(dialect, offeredDialects)) {
    return DecodeResult<NegotiateResponse>::failure(
        ErrorCode::ProtocolUnsupported,
        "Server selected an SMB dialect that was not offered.");
  }

  const auto securityBufferOffset = readU16Le(body + 56);
  const auto securityBufferLength = readU16Le(body + 58);
  const auto securityBufferEnd =
      static_cast<std::size_t>(securityBufferOffset) + securityBufferLength;
  if (securityBufferOffset < kSmb2HeaderSize ||
      securityBufferEnd > size ||
      securityBufferEnd < securityBufferOffset) {
    return DecodeResult<NegotiateResponse>::failure(
        ErrorCode::IoError,
        "SMB2 NEGOTIATE response security buffer is out of bounds.");
  }

  NegotiateResponse response;
  response.securityMode = readU16Le(body + 2);
  response.dialect = dialect;
  response.negotiateContextCount = readU16Le(body + 6);
  std::copy(body + 8, body + 24, response.serverGuid.begin());
  response.capabilities = readU32Le(body + 24);
  response.maxTransactSize = readU32Le(body + 28);
  response.maxReadSize = readU32Le(body + 32);
  response.maxWriteSize = readU32Le(body + 36);
  response.systemTime = readU64Le(body + 40);
  response.serverStartTime = readU64Le(body + 48);
  response.negotiateContextOffset = readU32Le(body + 60);
  response.securityBuffer.assign(data + securityBufferOffset,
                                 data + securityBufferEnd);

  return DecodeResult<NegotiateResponse>::success(response);
}

DecodeResult<NegotiateResponse>
decodeNegotiateResponse(const ByteVector &bytes,
                        const std::vector<Dialect> &offeredDialects) {
  return decodeNegotiateResponse(bytes.data(), bytes.size(), offeredDialects);
}

ByteVector buildSessionSetupRequest(const SessionSetupRequestOptions &options,
                                    std::uint64_t messageId,
                                    std::uint64_t sessionId) {
  if (options.securityBuffer.size() >
      std::numeric_limits<std::uint16_t>::max()) {
    throw std::invalid_argument("SMB2 SESSION_SETUP security buffer is too large.");
  }

  Smb2SyncHeader header;
  header.command = Command::SessionSetup;
  header.messageId = messageId;
  header.sessionId = sessionId;
  header.creditRequest = 1;

  constexpr std::uint16_t kSecurityBufferOffset =
      kSmb2HeaderSize + 24;

  auto bytes = encodeSmb2SyncHeader(header);
  appendU16Le(bytes, kSessionSetupRequestStructureSize);
  bytes.push_back(options.flags);
  bytes.push_back(
      static_cast<std::uint8_t>(securityModeForPolicy(options.signing)));
  appendU32Le(bytes, options.capabilities);
  appendU32Le(bytes, 0);
  appendU16Le(bytes, kSecurityBufferOffset);
  appendU16Le(bytes,
              static_cast<std::uint16_t>(options.securityBuffer.size()));
  appendU64Le(bytes, options.previousSessionId);
  bytes.insert(bytes.end(), options.securityBuffer.begin(),
               options.securityBuffer.end());
  return bytes;
}

DecodeResult<SessionSetupResponse>
decodeSessionSetupResponse(const std::uint8_t *data, std::size_t size) {
  const auto headerResult = decodeSmb2SyncHeader(data, size);
  if (!headerResult.ok) {
    return DecodeResult<SessionSetupResponse>::failure(
        headerResult.error.code, headerResult.error.message);
  }
  if (headerResult.value.command != Command::SessionSetup) {
    return DecodeResult<SessionSetupResponse>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB2 response is not a SESSION_SETUP response.");
  }
  if ((headerResult.value.flags & kFlagServerToRedir) == 0) {
    return DecodeResult<SessionSetupResponse>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB2 SESSION_SETUP response is missing server-to-redirector flag.");
  }
  if (size < kSmb2HeaderSize + 8) {
    return DecodeResult<SessionSetupResponse>::failure(
        ErrorCode::IoError,
        "SMB2 SESSION_SETUP response buffer is shorter than fixed response.");
  }

  const auto *body = data + kSmb2HeaderSize;
  if (readU16Le(body) != kSessionSetupResponseStructureSize) {
    return DecodeResult<SessionSetupResponse>::failure(
        ErrorCode::ProtocolUnsupported,
        "Invalid SMB2 SESSION_SETUP response structure size.");
  }

  const auto securityBufferOffset = readU16Le(body + 4);
  const auto securityBufferLength = readU16Le(body + 6);
  const auto securityBufferEnd =
      static_cast<std::size_t>(securityBufferOffset) + securityBufferLength;
  if (securityBufferLength > 0 &&
      (securityBufferOffset < kSmb2HeaderSize || securityBufferEnd > size ||
       securityBufferEnd < securityBufferOffset)) {
    return DecodeResult<SessionSetupResponse>::failure(
        ErrorCode::IoError,
        "SMB2 SESSION_SETUP response security buffer is out of bounds.");
  }

  SessionSetupResponse response;
  response.status = headerResult.value.status;
  response.sessionId = headerResult.value.sessionId;
  response.sessionFlags = readU16Le(body + 2);
  response.moreProcessingRequired =
      headerResult.value.status == kStatusMoreProcessingRequired;
  response.guestSession =
      (response.sessionFlags & kSessionFlagIsGuest) != 0;
  response.nullSession = (response.sessionFlags & kSessionFlagIsNull) != 0;
  response.encryptData =
      (response.sessionFlags & kSessionFlagEncryptData) != 0;
  if (securityBufferLength > 0) {
    response.securityBuffer.assign(data + securityBufferOffset,
                                   data + securityBufferEnd);
  }
  return DecodeResult<SessionSetupResponse>::success(response);
}

DecodeResult<SessionSetupResponse>
decodeSessionSetupResponse(const ByteVector &bytes) {
  return decodeSessionSetupResponse(bytes.data(), bytes.size());
}

ByteVector buildTreeConnectRequest(const TreeConnectRequestOptions &options,
                                   std::uint64_t messageId,
                                   std::uint64_t sessionId) {
  const auto path = encodeUtf16Le(uncPath(options));
  if (path.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw std::invalid_argument("SMB2 TREE_CONNECT path is too large.");
  }

  Smb2SyncHeader header;
  header.command = Command::TreeConnect;
  header.messageId = messageId;
  header.sessionId = sessionId;
  header.creditRequest = 1;

  constexpr std::uint16_t kPathOffset = kSmb2HeaderSize + 8;

  auto bytes = encodeSmb2SyncHeader(header);
  appendU16Le(bytes, kTreeConnectRequestStructureSize);
  appendU16Le(bytes, options.flags);
  appendU16Le(bytes, kPathOffset);
  appendU16Le(bytes, static_cast<std::uint16_t>(path.size()));
  bytes.insert(bytes.end(), path.begin(), path.end());
  return bytes;
}

DecodeResult<TreeConnectResponse>
decodeTreeConnectResponse(const std::uint8_t *data, std::size_t size) {
  const auto headerResult = decodeSmb2SyncHeader(data, size);
  if (!headerResult.ok) {
    return DecodeResult<TreeConnectResponse>::failure(
        headerResult.error.code, headerResult.error.message);
  }
  if (headerResult.value.command != Command::TreeConnect) {
    return DecodeResult<TreeConnectResponse>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB2 response is not a TREE_CONNECT response.");
  }
  if ((headerResult.value.flags & kFlagServerToRedir) == 0) {
    return DecodeResult<TreeConnectResponse>::failure(
        ErrorCode::ProtocolUnsupported,
        "SMB2 TREE_CONNECT response is missing server-to-redirector flag.");
  }
  if (size < kSmb2HeaderSize + 16) {
    return DecodeResult<TreeConnectResponse>::failure(
        ErrorCode::IoError,
        "SMB2 TREE_CONNECT response buffer is shorter than fixed response.");
  }

  const auto *body = data + kSmb2HeaderSize;
  if (readU16Le(body) != kTreeConnectResponseStructureSize) {
    return DecodeResult<TreeConnectResponse>::failure(
        ErrorCode::ProtocolUnsupported,
        "Invalid SMB2 TREE_CONNECT response structure size.");
  }

  TreeConnectResponse response;
  response.shareType = static_cast<ShareType>(body[2]);
  response.shareFlags = readU32Le(body + 4);
  response.capabilities = readU32Le(body + 8);
  response.maximalAccess = readU32Le(body + 12);
  response.isDfs = (response.shareFlags & kShareFlagDfs) != 0 ||
                   (response.capabilities & kShareCapabilityDfs) != 0;
  response.isDfsRoot = (response.shareFlags & kShareFlagDfsRoot) != 0;
  response.requiresEncryption =
      (response.shareFlags & kShareFlagEncryptData) != 0;
  return DecodeResult<TreeConnectResponse>::success(response);
}

DecodeResult<TreeConnectResponse>
decodeTreeConnectResponse(const ByteVector &bytes) {
  return decodeTreeConnectResponse(bytes.data(), bytes.size());
}

ByteVector encodeDirectTcpFrame(const ByteVector &smb2Message) {
  if (smb2Message.size() > kMaxDirectTcpPayloadLength) {
    throw std::invalid_argument("SMB2 Direct TCP payload is too large.");
  }

  const auto length = static_cast<std::uint32_t>(smb2Message.size());
  ByteVector bytes;
  bytes.reserve(kDirectTcpHeaderSize + smb2Message.size());
  bytes.push_back(0);
  bytes.push_back(static_cast<std::uint8_t>((length >> 16) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>(length & 0xFF));
  bytes.insert(bytes.end(), smb2Message.begin(), smb2Message.end());
  return bytes;
}

DecodeResult<std::uint32_t>
decodeDirectTcpPayloadLength(const std::uint8_t *data, std::size_t size) {
  if (data == nullptr || size < kDirectTcpHeaderSize) {
    return DecodeResult<std::uint32_t>::failure(
        ErrorCode::IoError,
        "SMB2 Direct TCP header buffer is shorter than 4 bytes.");
  }

  if (data[0] != 0) {
    return DecodeResult<std::uint32_t>::failure(
        ErrorCode::ProtocolUnsupported,
        "Invalid SMB2 Direct TCP header marker.");
  }

  const auto length = (static_cast<std::uint32_t>(data[1]) << 16) |
                      (static_cast<std::uint32_t>(data[2]) << 8) |
                      static_cast<std::uint32_t>(data[3]);
  return DecodeResult<std::uint32_t>::success(length);
}

DecodeResult<std::uint32_t>
decodeDirectTcpPayloadLength(const ByteVector &bytes) {
  return decodeDirectTcpPayloadLength(bytes.data(), bytes.size());
}

DecodeResult<ByteVector> decodeDirectTcpPayload(const ByteVector &bytes) {
  const auto payloadLength = decodeDirectTcpPayloadLength(bytes);
  if (!payloadLength.ok) {
    return DecodeResult<ByteVector>::failure(payloadLength.error.code,
                                             payloadLength.error.message);
  }
  if (bytes.size() < kDirectTcpHeaderSize + payloadLength.value) {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::IoError,
        "SMB2 Direct TCP frame is shorter than declared payload.");
  }

  return DecodeResult<ByteVector>::success(ByteVector(
      bytes.begin() + kDirectTcpHeaderSize,
      bytes.begin() + kDirectTcpHeaderSize + payloadLength.value));
}

bool containsSmb1Dialect(const std::vector<Dialect> &) { return false; }

} // namespace smb::native_smb
