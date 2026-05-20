#pragma once

#include "Protocol.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace smb::native_smb {

constexpr std::uint32_t kFsctlDfsGetReferrals = 0x00060194;
constexpr std::uint16_t kDfsReferralVersion2 = 2;
constexpr std::uint16_t kDfsReferralVersion3 = 3;
constexpr std::uint16_t kDfsReferralVersion4 = 4;
constexpr std::uint16_t kDfsReferralNameList = 0x0002;
constexpr std::uint32_t kDfsReferralHeaderReferralServers = 0x00000001;
constexpr std::uint32_t kDfsReferralHeaderStorageServers = 0x00000002;
constexpr std::uint32_t kDfsReferralHeaderTargetFailback = 0x00000004;

struct DfsReferralEntry {
  std::uint16_t version = 0;
  std::uint16_t serverType = 0;
  std::uint16_t flags = 0;
  std::uint32_t timeToLiveSeconds = 0;
  std::string dfsPath;
  std::string dfsAlternatePath;
  std::string networkAddress;
  bool rootTarget = false;
  bool nameListReferral = false;
};

struct DfsReferralResponse {
  std::uint16_t pathConsumedBytes = 0;
  std::uint32_t headerFlags = 0;
  std::vector<DfsReferralEntry> entries;
};

ByteVector buildDfsGetReferralRequest(std::string_view requestPath,
                                      std::uint16_t maxReferralLevel = 3);

DecodeResult<DfsReferralResponse>
decodeDfsReferralResponse(const std::uint8_t *data, std::size_t size);
DecodeResult<DfsReferralResponse>
decodeDfsReferralResponse(const ByteVector &bytes);

} // namespace smb::native_smb
