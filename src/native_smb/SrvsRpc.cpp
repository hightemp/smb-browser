#include "SrvsRpc.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace smb::native_smb {
namespace {

constexpr std::uint32_t kShareInfoLevel1 = 1;
constexpr std::uint32_t kNullPointer = 0;
constexpr std::uint32_t kServerNameReferentId = 0x00020000;
constexpr std::uint32_t kShareInfoContainerReferentId = 0x00020004;
constexpr std::uint32_t kResumeHandleReferentId = 0x00020008;

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

void align4(ByteVector &bytes) {
  while ((bytes.size() % 4) != 0) {
    bytes.push_back(0);
  }
}

std::string normalizeServerName(std::string_view serverName) {
  std::string normalized(serverName);
  std::replace(normalized.begin(), normalized.end(), '/', '\\');
  if (normalized.rfind("\\\\", 0) == 0) {
    return normalized;
  }
  while (!normalized.empty() && normalized.front() == '\\') {
    normalized.erase(normalized.begin());
  }
  return "\\\\" + normalized;
}

void appendNdrUtf16String(ByteVector &bytes, std::string_view text) {
  const auto utf16 = encodeUtf16Le(text);
  const auto characterCount = static_cast<std::uint32_t>(utf16.size() / 2 + 1);
  appendU32Le(bytes, characterCount);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, characterCount);
  bytes.insert(bytes.end(), utf16.begin(), utf16.end());
  appendU16Le(bytes, 0);
  align4(bytes);
}

void appendNdrUniqueUtf16String(ByteVector &bytes, std::uint32_t referentId,
                                std::string_view text) {
  appendU32Le(bytes, referentId);
  appendNdrUtf16String(bytes, text);
}

struct NdrCursor {
  const std::uint8_t *data = nullptr;
  std::size_t size = 0;
  std::size_t offset = 0;

  bool alignTo4() {
    const auto aligned = (offset + 3U) & ~std::size_t{3U};
    if (aligned > size) {
      return false;
    }
    offset = aligned;
    return true;
  }

  bool readU32(std::uint32_t *value) {
    if (offset + 4 > size) {
      return false;
    }
    *value = readU32Le(data + offset);
    offset += 4;
    return true;
  }

  bool readUtf16String(std::string *value) {
    std::uint32_t maxCount = 0;
    std::uint32_t offsetCount = 0;
    std::uint32_t actualCount = 0;
    if (!readU32(&maxCount) || !readU32(&offsetCount) ||
        !readU32(&actualCount)) {
      return false;
    }
    if (offsetCount != 0 || actualCount > maxCount ||
        actualCount > (std::numeric_limits<std::size_t>::max() / 2)) {
      return false;
    }

    const auto byteCount = static_cast<std::size_t>(actualCount) * 2;
    if (offset + byteCount > size) {
      return false;
    }

    auto decodeBytes = byteCount;
    if (decodeBytes >= 2 && readU16Le(data + offset + decodeBytes - 2) == 0) {
      decodeBytes -= 2;
    }

    const auto decoded = decodeUtf16Le(data + offset, decodeBytes);
    if (!decoded.ok) {
      return false;
    }
    *value = decoded.value;
    offset += byteCount;
    return alignTo4();
  }
};

SrvsShareKind shareKind(std::uint32_t rawType) {
  switch (rawType & 0x000000FFU) {
  case kShareTypeDisk:
    return SrvsShareKind::Disk;
  case kShareTypePrinter:
    return SrvsShareKind::Printer;
  case kShareTypeDevice:
    return SrvsShareKind::Device;
  case kShareTypeIpc:
    return SrvsShareKind::Ipc;
  default:
    return SrvsShareKind::Unknown;
  }
}

bool endsWithDollar(const std::string &text) {
  return !text.empty() && text.back() == '$';
}

DecodeResult<SrvsShareEnumResponse> failure(std::string message) {
  return DecodeResult<SrvsShareEnumResponse>::failure(ErrorCode::IoError,
                                                      std::move(message));
}

} // namespace

ByteVector buildNetrShareEnumRequestStub(
    std::string_view serverName, std::uint32_t preferredMaximumLength,
    std::optional<std::uint32_t> resumeHandle) {
  ByteVector bytes;
  appendNdrUniqueUtf16String(bytes, kServerNameReferentId,
                             normalizeServerName(serverName));
  appendU32Le(bytes, kShareInfoLevel1);
  appendU32Le(bytes, kShareInfoLevel1);
  appendU32Le(bytes, kShareInfoContainerReferentId);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, kNullPointer);
  appendU32Le(bytes, preferredMaximumLength);
  if (resumeHandle.has_value()) {
    appendU32Le(bytes, kResumeHandleReferentId);
    appendU32Le(bytes, resumeHandle.value());
  } else {
    appendU32Le(bytes, kNullPointer);
  }
  return bytes;
}

DecodeResult<SrvsShareEnumResponse>
decodeNetrShareEnumResponseStub(const std::uint8_t *data, std::size_t size) {
  if (data == nullptr && size > 0) {
    return failure("NetrShareEnum response buffer is null.");
  }

  NdrCursor cursor{data, size, 0};
  std::uint32_t level = 0;
  std::uint32_t unionLevel = 0;
  std::uint32_t containerPointer = 0;
  std::uint32_t entriesRead = 0;
  std::uint32_t bufferPointer = 0;
  if (!cursor.readU32(&level) || !cursor.readU32(&unionLevel) ||
      !cursor.readU32(&containerPointer)) {
    return failure("NetrShareEnum response is truncated before share list.");
  }
  if (level != kShareInfoLevel1 || unionLevel != kShareInfoLevel1) {
    return failure("NetrShareEnum response uses an unsupported info level.");
  }

  SrvsShareEnumResponse response;
  struct DeferredShare {
    std::uint32_t namePointer = 0;
    std::uint32_t rawType = 0;
    std::uint32_t commentPointer = 0;
  };
  std::vector<DeferredShare> deferred;

  if (containerPointer != kNullPointer &&
      (!cursor.readU32(&entriesRead) || !cursor.readU32(&bufferPointer))) {
    return failure("NetrShareEnum response share container is truncated.");
  }

  if (bufferPointer != kNullPointer) {
    std::uint32_t arrayCount = 0;
    if (!cursor.readU32(&arrayCount)) {
      return failure("NetrShareEnum response is missing share array count.");
    }
    if (arrayCount < entriesRead) {
      return failure("NetrShareEnum response array count is inconsistent.");
    }
    deferred.reserve(entriesRead);
    for (std::uint32_t index = 0; index < entriesRead; ++index) {
      DeferredShare share;
      if (!cursor.readU32(&share.namePointer) ||
          !cursor.readU32(&share.rawType) ||
          !cursor.readU32(&share.commentPointer)) {
        return failure("NetrShareEnum response share array is truncated.");
      }
      deferred.push_back(share);
    }

    response.shares.reserve(deferred.size());
    for (const auto &item : deferred) {
      SrvsShareInfo share;
      if (item.namePointer != kNullPointer &&
          !cursor.readUtf16String(&share.name)) {
        return failure("NetrShareEnum response share name is invalid.");
      }
      share.rawType = item.rawType;
      share.kind = shareKind(item.rawType);
      share.special = (item.rawType & kShareTypeSpecial) != 0;
      share.temporary = (item.rawType & kShareTypeTemporary) != 0;
      share.hidden = share.special || endsWithDollar(share.name);
      if (item.commentPointer != kNullPointer &&
          !cursor.readUtf16String(&share.comment)) {
        return failure("NetrShareEnum response share comment is invalid.");
      }
      response.shares.push_back(std::move(share));
    }
  } else if (entriesRead != 0) {
    return failure("NetrShareEnum response has entries without a buffer.");
  }

  if (!cursor.readU32(&response.totalEntries)) {
    return failure("NetrShareEnum response is missing total entries.");
  }

  std::uint32_t resumePointer = 0;
  if (!cursor.readU32(&resumePointer)) {
    return failure("NetrShareEnum response is missing resume handle pointer.");
  }
  if (resumePointer != kNullPointer) {
    std::uint32_t resume = 0;
    if (!cursor.readU32(&resume)) {
      return failure("NetrShareEnum response resume handle is truncated.");
    }
    response.resumeHandle = resume;
  }

  if (!cursor.readU32(&response.apiStatus)) {
    return failure("NetrShareEnum response is missing API status.");
  }
  response.moreData = response.apiStatus == kNetApiStatusMoreData;
  if (response.apiStatus != kNetApiStatusSuccess && !response.moreData) {
    return DecodeResult<SrvsShareEnumResponse>::failure(
        response.apiStatus == 5 ? ErrorCode::PermissionDenied
                                : ErrorCode::NetworkError,
        "NetrShareEnum returned a non-success NET_API_STATUS.");
  }

  return DecodeResult<SrvsShareEnumResponse>::success(std::move(response));
}

DecodeResult<SrvsShareEnumResponse>
decodeNetrShareEnumResponseStub(const ByteVector &bytes) {
  return decodeNetrShareEnumResponseStub(bytes.data(), bytes.size());
}

} // namespace smb::native_smb
