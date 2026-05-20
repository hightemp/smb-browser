#include "DfsReferral.h"

#include <utility>

namespace smb::native_smb {
namespace {

void appendU16Le(ByteVector &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
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

DecodeResult<DfsReferralResponse> failure(std::string message) {
  return DecodeResult<DfsReferralResponse>::failure(ErrorCode::IoError,
                                                    std::move(message));
}

DecodeResult<std::string> readReferralString(const std::uint8_t *data,
                                             std::size_t size,
                                             std::size_t offset) {
  if (offset >= size) {
    return DecodeResult<std::string>::failure(
        ErrorCode::IoError, "DFS referral string offset is out of bounds.");
  }
  auto end = offset;
  while (end + 1 < size) {
    if (readU16Le(data + end) == 0) {
      return decodeUtf16Le(data + offset, end - offset);
    }
    end += 2;
  }
  return DecodeResult<std::string>::failure(
      ErrorCode::IoError, "DFS referral string is not null-terminated.");
}

bool readOptionalString(const std::uint8_t *data, std::size_t size,
                        std::size_t entryOffset, std::uint16_t stringOffset,
                        std::string *value) {
  if (stringOffset == 0) {
    value->clear();
    return true;
  }
  const auto decoded =
      readReferralString(data, size, entryOffset + stringOffset);
  if (!decoded.ok) {
    return false;
  }
  *value = decoded.value;
  return true;
}

DecodeResult<DfsReferralEntry>
decodeReferralEntry(const std::uint8_t *data, std::size_t size,
                    std::size_t entryOffset) {
  if (entryOffset + 4 > size) {
    return DecodeResult<DfsReferralEntry>::failure(
        ErrorCode::IoError, "DFS referral entry is truncated.");
  }

  DfsReferralEntry entry;
  entry.version = readU16Le(data + entryOffset);
  const auto entrySize = readU16Le(data + entryOffset + 2);
  if (entrySize == 0 || entryOffset + entrySize > size) {
    return DecodeResult<DfsReferralEntry>::failure(
        ErrorCode::IoError, "DFS referral entry size is invalid.");
  }

  std::uint16_t dfsPathOffset = 0;
  std::uint16_t dfsAlternatePathOffset = 0;
  std::uint16_t networkAddressOffset = 0;

  if (entry.version == kDfsReferralVersion2) {
    if (entrySize < 22) {
      return DecodeResult<DfsReferralEntry>::failure(
          ErrorCode::IoError, "DFS referral v2 entry is truncated.");
    }
    entry.serverType = readU16Le(data + entryOffset + 4);
    entry.flags = readU16Le(data + entryOffset + 6);
    entry.timeToLiveSeconds = readU32Le(data + entryOffset + 12);
    dfsPathOffset = readU16Le(data + entryOffset + 16);
    dfsAlternatePathOffset = readU16Le(data + entryOffset + 18);
    networkAddressOffset = readU16Le(data + entryOffset + 20);
  } else if (entry.version == kDfsReferralVersion3 ||
             entry.version == kDfsReferralVersion4) {
    if (entrySize < 34) {
      return DecodeResult<DfsReferralEntry>::failure(
          ErrorCode::IoError, "DFS referral v3/v4 entry is truncated.");
    }
    entry.serverType = readU16Le(data + entryOffset + 4);
    entry.flags = readU16Le(data + entryOffset + 6);
    entry.timeToLiveSeconds = readU32Le(data + entryOffset + 8);
    entry.nameListReferral = (entry.flags & kDfsReferralNameList) != 0;
    if (entry.nameListReferral) {
      dfsPathOffset = readU16Le(data + entryOffset + 12);
    } else {
      dfsPathOffset = readU16Le(data + entryOffset + 12);
      dfsAlternatePathOffset = readU16Le(data + entryOffset + 14);
      networkAddressOffset = readU16Le(data + entryOffset + 16);
    }
  } else {
    return DecodeResult<DfsReferralEntry>::failure(
        ErrorCode::ProtocolUnsupported,
        "Unsupported DFS referral entry version.");
  }

  entry.rootTarget = entry.serverType == 1;
  if (!readOptionalString(data, size, entryOffset, dfsPathOffset,
                          &entry.dfsPath) ||
      !readOptionalString(data, size, entryOffset, dfsAlternatePathOffset,
                          &entry.dfsAlternatePath) ||
      !readOptionalString(data, size, entryOffset, networkAddressOffset,
                          &entry.networkAddress)) {
    return DecodeResult<DfsReferralEntry>::failure(
        ErrorCode::IoError, "DFS referral entry string is invalid.");
  }

  return DecodeResult<DfsReferralEntry>::success(std::move(entry));
}

} // namespace

ByteVector buildDfsGetReferralRequest(std::string_view requestPath,
                                      std::uint16_t maxReferralLevel) {
  ByteVector bytes;
  appendU16Le(bytes, maxReferralLevel);
  const auto encoded = encodeUtf16Le(requestPath);
  bytes.insert(bytes.end(), encoded.begin(), encoded.end());
  appendU16Le(bytes, 0);
  return bytes;
}

DecodeResult<DfsReferralResponse>
decodeDfsReferralResponse(const std::uint8_t *data, std::size_t size) {
  if (data == nullptr && size > 0) {
    return failure("DFS referral response buffer is null.");
  }
  if (size < 8) {
    return failure("DFS referral response header is truncated.");
  }

  DfsReferralResponse response;
  response.pathConsumedBytes = readU16Le(data);
  const auto referralCount = readU16Le(data + 2);
  response.headerFlags = readU32Le(data + 4);

  auto entryOffset = std::size_t{8};
  response.entries.reserve(referralCount);
  for (std::uint16_t index = 0; index < referralCount; ++index) {
    if (entryOffset + 4 > size) {
      return failure("DFS referral response entries are truncated.");
    }
    const auto entrySize = readU16Le(data + entryOffset + 2);
    const auto entry = decodeReferralEntry(data, size, entryOffset);
    if (!entry.ok) {
      return DecodeResult<DfsReferralResponse>::failure(entry.error.code,
                                                        entry.error.message);
    }
    response.entries.push_back(entry.value);
    entryOffset += entrySize;
  }

  return DecodeResult<DfsReferralResponse>::success(std::move(response));
}

DecodeResult<DfsReferralResponse>
decodeDfsReferralResponse(const ByteVector &bytes) {
  return decodeDfsReferralResponse(bytes.data(), bytes.size());
}

} // namespace smb::native_smb
