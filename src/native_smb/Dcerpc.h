#pragma once

#include "Protocol.h"

#include <array>
#include <cstdint>

namespace smb::native_smb {

constexpr std::uint8_t kDcerpcVersion = 5;
constexpr std::uint8_t kDcerpcPacketTypeRequest = 0;
constexpr std::uint8_t kDcerpcPacketTypeResponse = 2;
constexpr std::uint8_t kDcerpcPacketTypeBind = 11;
constexpr std::uint8_t kDcerpcPacketTypeBindAck = 12;
constexpr std::uint8_t kDcerpcFlagFirstFragment = 0x01;
constexpr std::uint8_t kDcerpcFlagLastFragment = 0x02;
constexpr std::uint16_t kDcerpcDefaultFragSize = 4280;

using DcerpcUuid = std::array<std::uint8_t, 16>;

struct DcerpcSyntaxId {
  DcerpcUuid uuid{};
  std::uint16_t majorVersion = 0;
  std::uint16_t minorVersion = 0;
};

struct DcerpcBindAck {
  std::uint16_t maxTransmitFrag = 0;
  std::uint16_t maxReceiveFrag = 0;
  std::uint32_t associationGroupId = 0;
  bool accepted = false;
  std::uint16_t result = 0;
  std::uint16_t reason = 0;
  DcerpcSyntaxId transferSyntax;
};

struct DcerpcResponse {
  std::uint32_t allocationHint = 0;
  std::uint16_t contextId = 0;
  ByteVector stubData;
};

const DcerpcSyntaxId &srvsRpcSyntax();
const DcerpcSyntaxId &ndr32TransferSyntax();

ByteVector buildDcerpcBindPdu(const DcerpcSyntaxId &abstractSyntax,
                              std::uint32_t callId = 1,
                              std::uint16_t contextId = 0,
                              std::uint16_t maxTransmitFrag =
                                  kDcerpcDefaultFragSize,
                              std::uint16_t maxReceiveFrag =
                                  kDcerpcDefaultFragSize);
DecodeResult<DcerpcBindAck> decodeDcerpcBindAck(const std::uint8_t *data,
                                                std::size_t size);
DecodeResult<DcerpcBindAck> decodeDcerpcBindAck(const ByteVector &bytes);

ByteVector buildDcerpcRequestPdu(std::uint16_t opnum,
                                 const ByteVector &stubData,
                                 std::uint32_t callId,
                                 std::uint16_t contextId = 0);
DecodeResult<DcerpcResponse> decodeDcerpcResponse(const std::uint8_t *data,
                                                  std::size_t size);
DecodeResult<DcerpcResponse> decodeDcerpcResponse(const ByteVector &bytes);

} // namespace smb::native_smb
