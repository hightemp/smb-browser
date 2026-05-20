#pragma once

#include "Protocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace smb::native_smb {

constexpr std::uint16_t kSrvsNetrShareEnumOpnum = 15;
constexpr std::uint32_t kShareTypeDisk = 0x00000000;
constexpr std::uint32_t kShareTypePrinter = 0x00000001;
constexpr std::uint32_t kShareTypeDevice = 0x00000002;
constexpr std::uint32_t kShareTypeIpc = 0x00000003;
constexpr std::uint32_t kShareTypeTemporary = 0x40000000;
constexpr std::uint32_t kShareTypeSpecial = 0x80000000;
constexpr std::uint32_t kNetApiStatusSuccess = 0;
constexpr std::uint32_t kNetApiStatusMoreData = 234;

enum class SrvsShareKind {
  Disk,
  Printer,
  Device,
  Ipc,
  Unknown,
};

struct SrvsShareInfo {
  std::string name;
  std::uint32_t rawType = 0;
  SrvsShareKind kind = SrvsShareKind::Unknown;
  std::string comment;
  bool hidden = false;
  bool special = false;
  bool temporary = false;
};

struct SrvsShareEnumResponse {
  std::vector<SrvsShareInfo> shares;
  std::uint32_t totalEntries = 0;
  std::optional<std::uint32_t> resumeHandle;
  std::uint32_t apiStatus = kNetApiStatusSuccess;
  bool moreData = false;
};

ByteVector buildNetrShareEnumRequestStub(
    std::string_view serverName,
    std::uint32_t preferredMaximumLength = 0xFFFFFFFF,
    std::optional<std::uint32_t> resumeHandle = std::nullopt);

DecodeResult<SrvsShareEnumResponse>
decodeNetrShareEnumResponseStub(const std::uint8_t *data, std::size_t size);
DecodeResult<SrvsShareEnumResponse>
decodeNetrShareEnumResponseStub(const ByteVector &bytes);

} // namespace smb::native_smb
