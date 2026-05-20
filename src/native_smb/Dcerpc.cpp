#include "Dcerpc.h"

#include <algorithm>
#include <utility>

namespace smb::native_smb {
namespace {

constexpr std::size_t kCommonHeaderSize = 16;
constexpr std::uint16_t kDcerpcAccepted = 0;

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

void appendCommonHeader(ByteVector &bytes, std::uint8_t packetType,
                        std::uint16_t fragLength, std::uint32_t callId) {
  bytes.push_back(kDcerpcVersion);
  bytes.push_back(0);
  bytes.push_back(packetType);
  bytes.push_back(kDcerpcFlagFirstFragment | kDcerpcFlagLastFragment);
  bytes.push_back(0x10);
  bytes.push_back(0);
  bytes.push_back(0);
  bytes.push_back(0);
  appendU16Le(bytes, fragLength);
  appendU16Le(bytes, 0);
  appendU32Le(bytes, callId);
}

void appendSyntaxId(ByteVector &bytes, const DcerpcSyntaxId &syntax) {
  bytes.insert(bytes.end(), syntax.uuid.begin(), syntax.uuid.end());
  appendU16Le(bytes, syntax.majorVersion);
  appendU16Le(bytes, syntax.minorVersion);
}

bool validateCommonHeader(const std::uint8_t *data, std::size_t size,
                          std::uint8_t expectedPacketType,
                          std::uint16_t *fragLength) {
  if (size < kCommonHeaderSize || data == nullptr) {
    return false;
  }
  if (data[0] != kDcerpcVersion || data[2] != expectedPacketType) {
    return false;
  }
  if ((data[3] & (kDcerpcFlagFirstFragment | kDcerpcFlagLastFragment)) !=
      (kDcerpcFlagFirstFragment | kDcerpcFlagLastFragment)) {
    return false;
  }
  if (data[4] != 0x10) {
    return false;
  }
  *fragLength = readU16Le(data + 8);
  return *fragLength >= kCommonHeaderSize && *fragLength <= size;
}

DecodeResult<DcerpcBindAck> bindAckFailure(std::string message) {
  return DecodeResult<DcerpcBindAck>::failure(ErrorCode::IoError,
                                              std::move(message));
}

DecodeResult<DcerpcResponse> responseFailure(std::string message) {
  return DecodeResult<DcerpcResponse>::failure(ErrorCode::IoError,
                                               std::move(message));
}

} // namespace

const DcerpcSyntaxId &srvsRpcSyntax() {
  static const DcerpcSyntaxId syntax{
      DcerpcUuid{0xc8, 0x4f, 0x32, 0x4b, 0x70, 0x16, 0xd3, 0x01, 0x12, 0x78,
                 0x5a, 0x47, 0xbf, 0x6e, 0xe1, 0x88},
      3,
      0};
  return syntax;
}

const DcerpcSyntaxId &ndr32TransferSyntax() {
  static const DcerpcSyntaxId syntax{
      DcerpcUuid{0x04, 0x5d, 0x88, 0x8a, 0xeb, 0x1c, 0xc9, 0x11, 0x9f, 0xe8,
                 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60},
      2,
      0};
  return syntax;
}

ByteVector buildDcerpcBindPdu(const DcerpcSyntaxId &abstractSyntax,
                              std::uint32_t callId, std::uint16_t contextId,
                              std::uint16_t maxTransmitFrag,
                              std::uint16_t maxReceiveFrag) {
  constexpr std::uint16_t fragLength = 72;
  ByteVector bytes;
  bytes.reserve(fragLength);
  appendCommonHeader(bytes, kDcerpcPacketTypeBind, fragLength, callId);
  appendU16Le(bytes, maxTransmitFrag);
  appendU16Le(bytes, maxReceiveFrag);
  appendU32Le(bytes, 0);
  bytes.push_back(1);
  bytes.push_back(0);
  appendU16Le(bytes, 0);
  appendU16Le(bytes, contextId);
  bytes.push_back(1);
  bytes.push_back(0);
  appendSyntaxId(bytes, abstractSyntax);
  appendSyntaxId(bytes, ndr32TransferSyntax());
  return bytes;
}

DecodeResult<DcerpcBindAck> decodeDcerpcBindAck(const std::uint8_t *data,
                                                std::size_t size) {
  std::uint16_t fragLength = 0;
  if (!validateCommonHeader(data, size, kDcerpcPacketTypeBindAck,
                            &fragLength)) {
    return bindAckFailure("DCE/RPC bind_ack header is invalid.");
  }
  if (fragLength < 36) {
    return bindAckFailure("DCE/RPC bind_ack body is truncated.");
  }

  DcerpcBindAck ack;
  std::size_t offset = kCommonHeaderSize;
  ack.maxTransmitFrag = readU16Le(data + offset);
  offset += 2;
  ack.maxReceiveFrag = readU16Le(data + offset);
  offset += 2;
  ack.associationGroupId = readU32Le(data + offset);
  offset += 4;

  const auto secondaryAddressLength = readU16Le(data + offset);
  offset += 2;
  if (offset + secondaryAddressLength > fragLength) {
    return bindAckFailure("DCE/RPC bind_ack secondary address is truncated.");
  }
  offset += secondaryAddressLength;
  offset = (offset + 3U) & ~std::size_t{3U};
  if (offset + 4 > fragLength) {
    return bindAckFailure("DCE/RPC bind_ack result list is truncated.");
  }

  const auto resultCount = data[offset];
  offset += 4;
  if (resultCount == 0 || offset + 24 > fragLength) {
    return bindAckFailure("DCE/RPC bind_ack has no presentation result.");
  }

  ack.result = readU16Le(data + offset);
  offset += 2;
  ack.reason = readU16Le(data + offset);
  offset += 2;
  std::copy(data + offset, data + offset + 16, ack.transferSyntax.uuid.begin());
  offset += 16;
  ack.transferSyntax.majorVersion = readU16Le(data + offset);
  offset += 2;
  ack.transferSyntax.minorVersion = readU16Le(data + offset);
  ack.accepted = ack.result == kDcerpcAccepted;
  if (!ack.accepted) {
    return DecodeResult<DcerpcBindAck>::failure(
        ErrorCode::ProtocolUnsupported,
        "DCE/RPC bind_ack rejected the requested presentation context.");
  }

  return DecodeResult<DcerpcBindAck>::success(ack);
}

DecodeResult<DcerpcBindAck> decodeDcerpcBindAck(const ByteVector &bytes) {
  return decodeDcerpcBindAck(bytes.data(), bytes.size());
}

ByteVector buildDcerpcRequestPdu(std::uint16_t opnum,
                                 const ByteVector &stubData,
                                 std::uint32_t callId,
                                 std::uint16_t contextId) {
  const auto fragLength =
      static_cast<std::uint16_t>(kCommonHeaderSize + 8 + stubData.size());
  ByteVector bytes;
  bytes.reserve(fragLength);
  appendCommonHeader(bytes, kDcerpcPacketTypeRequest, fragLength, callId);
  appendU32Le(bytes, static_cast<std::uint32_t>(stubData.size()));
  appendU16Le(bytes, contextId);
  appendU16Le(bytes, opnum);
  bytes.insert(bytes.end(), stubData.begin(), stubData.end());
  return bytes;
}

DecodeResult<DcerpcResponse> decodeDcerpcResponse(const std::uint8_t *data,
                                                  std::size_t size) {
  std::uint16_t fragLength = 0;
  if (!validateCommonHeader(data, size, kDcerpcPacketTypeResponse,
                            &fragLength)) {
    return responseFailure("DCE/RPC response header is invalid.");
  }
  if (fragLength < kCommonHeaderSize + 8) {
    return responseFailure("DCE/RPC response body is truncated.");
  }

  DcerpcResponse response;
  std::size_t offset = kCommonHeaderSize;
  response.allocationHint = readU32Le(data + offset);
  offset += 4;
  response.contextId = readU16Le(data + offset);
  offset += 2;
  offset += 2;
  response.stubData.assign(data + offset, data + fragLength);
  return DecodeResult<DcerpcResponse>::success(std::move(response));
}

DecodeResult<DcerpcResponse> decodeDcerpcResponse(const ByteVector &bytes) {
  return decodeDcerpcResponse(bytes.data(), bytes.size());
}

} // namespace smb::native_smb
