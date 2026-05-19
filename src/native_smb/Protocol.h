#pragma once

#include "SmbNative.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace smb::native_smb {

using ByteVector = std::vector<std::uint8_t>;

enum class Dialect : std::uint16_t {
  Smb202 = 0x0202,
  Smb210 = 0x0210,
  Smb300 = 0x0300,
  Smb302 = 0x0302,
  Smb311 = 0x0311,
};

enum class Command : std::uint16_t {
  Negotiate = 0x0000,
  SessionSetup = 0x0001,
  Logoff = 0x0002,
  TreeConnect = 0x0003,
  TreeDisconnect = 0x0004,
  Create = 0x0005,
  Close = 0x0006,
  Flush = 0x0007,
  Read = 0x0008,
  Write = 0x0009,
  Lock = 0x000A,
  Ioctl = 0x000B,
  Cancel = 0x000C,
  Echo = 0x000D,
  QueryDirectory = 0x000E,
  ChangeNotify = 0x000F,
  QueryInfo = 0x0010,
  SetInfo = 0x0011,
  OplockBreak = 0x0012,
};

enum class GlobalCapability : std::uint32_t {
  Dfs = 0x00000001,
  Leasing = 0x00000002,
  LargeMtu = 0x00000004,
  MultiChannel = 0x00000008,
  PersistentHandles = 0x00000010,
  DirectoryLeasing = 0x00000020,
  Encryption = 0x00000040,
  Notifications = 0x00000080,
};

enum class ShareType : std::uint8_t {
  Disk = 0x01,
  Pipe = 0x02,
  Print = 0x03,
};

struct FileId {
  std::uint64_t persistent = 0;
  std::uint64_t volatileId = 0;
};

struct ProtocolError {
  ErrorCode code = ErrorCode::None;
  std::string message;
};

template <typename T> struct DecodeResult {
  bool ok = false;
  T value{};
  ProtocolError error;

  static DecodeResult success(T parsedValue) {
    DecodeResult result;
    result.ok = true;
    result.value = parsedValue;
    return result;
  }

  static DecodeResult failure(ErrorCode code, std::string message) {
    DecodeResult result;
    result.error.code = code;
    result.error.message = std::move(message);
    return result;
  }
};

struct Smb2SyncHeader {
  std::uint16_t creditCharge = 0;
  std::uint32_t status = 0;
  Command command = Command::Negotiate;
  std::uint16_t creditRequest = 1;
  std::uint32_t flags = 0;
  std::uint32_t nextCommand = 0;
  std::uint64_t messageId = 0;
  std::uint32_t treeId = 0;
  std::uint64_t sessionId = 0;
  std::array<std::uint8_t, 16> signature{};
};

struct NegotiateRequestOptions {
  std::vector<Dialect> dialects;
  SecurityPolicy signing = SecurityPolicy::Required;
  std::uint32_t capabilities = 0;
  std::array<std::uint8_t, 16> clientGuid{};
};

struct NegotiateResponse {
  std::uint16_t securityMode = 0;
  Dialect dialect = Dialect::Smb202;
  std::uint16_t negotiateContextCount = 0;
  std::array<std::uint8_t, 16> serverGuid{};
  std::uint32_t capabilities = 0;
  std::uint32_t maxTransactSize = 0;
  std::uint32_t maxReadSize = 0;
  std::uint32_t maxWriteSize = 0;
  std::uint64_t systemTime = 0;
  std::uint64_t serverStartTime = 0;
  ByteVector securityBuffer;
  std::uint32_t negotiateContextOffset = 0;
};

struct SessionSetupRequestOptions {
  std::uint8_t flags = 0;
  SecurityPolicy signing = SecurityPolicy::Required;
  std::uint32_t capabilities = 0;
  std::uint64_t previousSessionId = 0;
  ByteVector securityBuffer;
};

struct SessionSetupResponse {
  std::uint32_t status = 0;
  std::uint64_t sessionId = 0;
  std::uint16_t sessionFlags = 0;
  ByteVector securityBuffer;
  bool moreProcessingRequired = false;
  bool guestSession = false;
  bool nullSession = false;
  bool encryptData = false;
};

struct TreeConnectRequestOptions {
  std::string server;
  std::string share;
  std::uint16_t flags = 0;
};

struct TreeConnectResponse {
  ShareType shareType = ShareType::Disk;
  std::uint32_t shareFlags = 0;
  std::uint32_t capabilities = 0;
  std::uint32_t maximalAccess = 0;
  bool isDfs = false;
  bool isDfsRoot = false;
  bool requiresEncryption = false;
};

struct CreateRequestOptions {
  std::string path;
  std::uint8_t requestedOplockLevel = 0;
  std::uint32_t impersonationLevel = 0x00000002;
  std::uint32_t desiredAccess = 0;
  std::uint32_t fileAttributes = 0;
  std::uint32_t shareAccess = 0;
  std::uint32_t createDisposition = 0x00000001;
  std::uint32_t createOptions = 0;
  ByteVector createContexts;
};

struct CreateResponse {
  std::uint8_t oplockLevel = 0;
  std::uint8_t flags = 0;
  std::uint32_t createAction = 0;
  std::uint64_t creationTime = 0;
  std::uint64_t lastAccessTime = 0;
  std::uint64_t lastWriteTime = 0;
  std::uint64_t changeTime = 0;
  std::uint64_t allocationSize = 0;
  std::uint64_t endOfFile = 0;
  std::uint32_t fileAttributes = 0;
  FileId fileId;
  bool isReparsePoint = false;
};

struct QueryDirectoryRequestOptions {
  FileId fileId;
  std::string pattern = "*";
  std::uint8_t informationClass = 0x25;
  std::uint8_t flags = 0x01;
  std::uint32_t fileIndex = 0;
  std::uint32_t outputBufferLength = 65536;
};

struct DirectoryEntry {
  std::string name;
  std::uint32_t fileIndex = 0;
  std::uint64_t creationTime = 0;
  std::uint64_t lastAccessTime = 0;
  std::uint64_t lastWriteTime = 0;
  std::uint64_t changeTime = 0;
  std::uint64_t endOfFile = 0;
  std::uint64_t allocationSize = 0;
  std::uint32_t fileAttributes = 0;
  std::uint32_t eaSizeOrReparseTag = 0;
  std::uint64_t fileId = 0;
  bool isDirectory = false;
  bool isReparsePoint = false;
};

struct QueryDirectoryResponse {
  std::uint32_t status = 0;
  std::vector<DirectoryEntry> entries;
};

constexpr std::size_t kSmb2HeaderSize = 64;
constexpr std::size_t kDirectTcpHeaderSize = 4;
constexpr std::uint32_t kSmb2ProtocolId = 0x424D53FE;
constexpr std::uint16_t kSmb2HeaderStructureSize = 64;
constexpr std::uint16_t kNegotiateRequestStructureSize = 36;
constexpr std::uint16_t kNegotiateResponseStructureSize = 65;
constexpr std::uint16_t kSessionSetupRequestStructureSize = 25;
constexpr std::uint16_t kSessionSetupResponseStructureSize = 9;
constexpr std::uint16_t kTreeConnectRequestStructureSize = 9;
constexpr std::uint16_t kTreeConnectResponseStructureSize = 16;
constexpr std::uint16_t kCreateRequestStructureSize = 57;
constexpr std::uint16_t kCreateResponseStructureSize = 89;
constexpr std::uint16_t kQueryDirectoryRequestStructureSize = 33;
constexpr std::uint16_t kQueryDirectoryResponseStructureSize = 9;
constexpr std::uint32_t kStatusSuccess = 0x00000000;
constexpr std::uint32_t kStatusMoreProcessingRequired = 0xC0000016;
constexpr std::uint32_t kFlagServerToRedir = 0x00000001;
constexpr std::uint32_t kFlagAsyncCommand = 0x00000002;
constexpr std::uint32_t kFlagSigned = 0x00000008;
constexpr std::uint32_t kFlagDfsOperations = 0x10000000;
constexpr std::uint32_t kShareFlagDfs = 0x00000001;
constexpr std::uint32_t kShareFlagDfsRoot = 0x00000002;
constexpr std::uint32_t kShareFlagEncryptData = 0x00008000;
constexpr std::uint32_t kShareCapabilityDfs = 0x00000008;
constexpr std::uint16_t kSessionFlagIsGuest = 0x0001;
constexpr std::uint16_t kSessionFlagIsNull = 0x0002;
constexpr std::uint16_t kSessionFlagEncryptData = 0x0004;
constexpr std::uint32_t kFileReadData = 0x00000001;
constexpr std::uint32_t kFileListDirectory = 0x00000001;
constexpr std::uint32_t kFileReadEa = 0x00000008;
constexpr std::uint32_t kFileReadAttributes = 0x00000080;
constexpr std::uint32_t kFileAttributeDirectory = 0x00000010;
constexpr std::uint32_t kFileAttributeNormal = 0x00000080;
constexpr std::uint32_t kFileAttributeReparsePoint = 0x00000400;
constexpr std::uint32_t kFileShareRead = 0x00000001;
constexpr std::uint32_t kFileShareWrite = 0x00000002;
constexpr std::uint32_t kFileShareDelete = 0x00000004;
constexpr std::uint32_t kFileOpen = 0x00000001;
constexpr std::uint32_t kFileDirectoryFile = 0x00000001;
constexpr std::uint32_t kFileNonDirectoryFile = 0x00000040;
constexpr std::uint8_t kFileIdBothDirectoryInformation = 0x25;
constexpr std::uint8_t kQueryDirectoryRestartScans = 0x01;

std::uint32_t capabilityMask(std::initializer_list<GlobalCapability> values);
std::uint16_t securityModeForPolicy(SecurityPolicy policy);

std::vector<Dialect> defaultInitialDialects();
ByteVector encodeUtf16Le(std::string_view text);
DecodeResult<std::string> decodeUtf16Le(const std::uint8_t *data,
                                        std::size_t size);
DecodeResult<std::string> decodeUtf16Le(const ByteVector &bytes);

ByteVector encodeSmb2SyncHeader(const Smb2SyncHeader &header);
DecodeResult<Smb2SyncHeader> decodeSmb2SyncHeader(const std::uint8_t *data,
                                                  std::size_t size);
DecodeResult<Smb2SyncHeader> decodeSmb2SyncHeader(const ByteVector &bytes);

ByteVector buildNegotiateRequest(const NegotiateRequestOptions &options,
                                 std::uint64_t messageId = 0);
DecodeResult<NegotiateResponse>
decodeNegotiateResponse(const std::uint8_t *data, std::size_t size,
                        const std::vector<Dialect> &offeredDialects);
DecodeResult<NegotiateResponse>
decodeNegotiateResponse(const ByteVector &bytes,
                        const std::vector<Dialect> &offeredDialects);

ByteVector buildSessionSetupRequest(const SessionSetupRequestOptions &options,
                                    std::uint64_t messageId,
                                    std::uint64_t sessionId = 0);
DecodeResult<SessionSetupResponse>
decodeSessionSetupResponse(const std::uint8_t *data, std::size_t size);
DecodeResult<SessionSetupResponse>
decodeSessionSetupResponse(const ByteVector &bytes);
ByteVector buildTreeConnectRequest(const TreeConnectRequestOptions &options,
                                   std::uint64_t messageId,
                                   std::uint64_t sessionId);
DecodeResult<TreeConnectResponse>
decodeTreeConnectResponse(const std::uint8_t *data, std::size_t size);
DecodeResult<TreeConnectResponse>
decodeTreeConnectResponse(const ByteVector &bytes);
ByteVector buildCreateRequest(const CreateRequestOptions &options,
                              std::uint64_t messageId,
                              std::uint32_t treeId,
                              std::uint64_t sessionId);
DecodeResult<CreateResponse> decodeCreateResponse(const std::uint8_t *data,
                                                  std::size_t size);
DecodeResult<CreateResponse> decodeCreateResponse(const ByteVector &bytes);
ByteVector buildQueryDirectoryRequest(
    const QueryDirectoryRequestOptions &options, std::uint64_t messageId,
    std::uint32_t treeId, std::uint64_t sessionId);
DecodeResult<QueryDirectoryResponse>
decodeQueryDirectoryResponse(const std::uint8_t *data, std::size_t size);
DecodeResult<QueryDirectoryResponse>
decodeQueryDirectoryResponse(const ByteVector &bytes);

ByteVector encodeDirectTcpFrame(const ByteVector &smb2Message);
DecodeResult<std::uint32_t>
decodeDirectTcpPayloadLength(const std::uint8_t *data, std::size_t size);
DecodeResult<std::uint32_t>
decodeDirectTcpPayloadLength(const ByteVector &bytes);
DecodeResult<ByteVector> decodeDirectTcpPayload(const ByteVector &bytes);

bool containsSmb1Dialect(const std::vector<Dialect> &dialects);

} // namespace smb::native_smb
