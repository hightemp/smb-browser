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
  std::uint64_t messagesUsed = 0;
};

template <typename T> struct DecodeResult {
  bool ok = false;
  T value{};
  ProtocolError error;

  static DecodeResult success(T parsedValue) {
    DecodeResult result;
    result.ok = true;
    result.value = std::move(parsedValue);
    return result;
  }

  static DecodeResult failure(ErrorCode code, std::string message,
                              std::uint64_t messagesUsed = 0) {
    DecodeResult result;
    result.error.code = code;
    result.error.message = std::move(message);
    result.error.messagesUsed = messagesUsed;
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

struct TreeDisconnectResponse {
  std::uint32_t status = 0;
};

struct LogoffResponse {
  std::uint32_t status = 0;
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

struct CloseRequestOptions {
  FileId fileId;
  std::uint16_t flags = 0;
};

struct CloseResponse {
  std::uint32_t status = 0;
  std::uint16_t flags = 0;
  std::uint64_t creationTime = 0;
  std::uint64_t lastAccessTime = 0;
  std::uint64_t lastWriteTime = 0;
  std::uint64_t changeTime = 0;
  std::uint64_t allocationSize = 0;
  std::uint64_t endOfFile = 0;
  std::uint32_t fileAttributes = 0;
  bool hasPostQueryAttributes = false;
};

struct ReadRequestOptions {
  FileId fileId;
  std::uint32_t length = 0;
  std::uint64_t offset = 0;
  std::uint8_t flags = 0;
  std::uint32_t minimumCount = 0;
  std::uint32_t channel = 0;
  std::uint32_t remainingBytes = 0;
  ByteVector channelInfo;
};

struct ReadResponse {
  std::uint32_t status = 0;
  std::uint8_t dataOffset = 0;
  std::uint32_t dataRemaining = 0;
  std::uint8_t flags = 0;
  ByteVector data;
};

struct WriteRequestOptions {
  FileId fileId;
  ByteVector data;
  std::uint64_t offset = 0;
  std::uint32_t channel = 0;
  std::uint32_t remainingBytes = 0;
  ByteVector channelInfo;
  std::uint32_t flags = 0;
};

struct WriteResponse {
  std::uint32_t status = 0;
  std::uint32_t count = 0;
  std::uint32_t remaining = 0;
  std::uint16_t writeChannelInfoOffset = 0;
  std::uint16_t writeChannelInfoLength = 0;
};

struct IoctlRequestOptions {
  std::uint32_t ctlCode = 0;
  FileId fileId;
  ByteVector input;
  std::uint32_t maxInputResponse = 0;
  std::uint32_t maxOutputResponse = 65536;
  std::uint32_t flags = 0x00000001;
};

struct IoctlResponse {
  std::uint32_t status = 0;
  std::uint32_t ctlCode = 0;
  FileId fileId;
  ByteVector input;
  ByteVector output;
  std::uint32_t flags = 0;
};

struct CopyChunk {
  std::uint64_t sourceOffset = 0;
  std::uint64_t targetOffset = 0;
  std::uint32_t length = 0;
};

struct CopyChunkResponse {
  std::uint32_t chunksWritten = 0;
  std::uint32_t chunkBytesWritten = 0;
  std::uint32_t totalBytesWritten = 0;
};

struct SetInfoRequestOptions {
  FileId fileId;
  std::uint8_t infoType = 0x01;
  std::uint8_t fileInfoClass = 0;
  ByteVector buffer;
  std::uint32_t additionalInformation = 0;
};

struct SetInfoResponse {
  std::uint32_t status = 0;
};

struct QueryInfoRequestOptions {
  FileId fileId;
  std::uint8_t infoType = 0x01;
  std::uint8_t fileInfoClass = 0;
  std::uint32_t outputBufferLength = 65536;
  ByteVector inputBuffer;
  std::uint32_t additionalInformation = 0;
  std::uint32_t flags = 0;
};

struct QueryInfoResponse {
  std::uint32_t status = 0;
  std::uint16_t outputBufferOffset = 0;
  ByteVector buffer;
};

struct FileBasicInformation {
  std::uint64_t creationTime = 0;
  std::uint64_t lastAccessTime = 0;
  std::uint64_t lastWriteTime = 0;
  std::uint64_t changeTime = 0;
  std::uint32_t fileAttributes = 0;
};

struct FileStandardInformation {
  std::uint64_t allocationSize = 0;
  std::uint64_t endOfFile = 0;
  std::uint32_t numberOfLinks = 0;
  bool deletePending = false;
  bool directory = false;
};

struct FileFullEaInformation {
  std::string name;
  ByteVector value;
  bool needEa = false;
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

struct ChangeNotifyRequestOptions {
  FileId fileId;
  std::uint16_t flags = 0;
  std::uint32_t outputBufferLength = 65536;
  std::uint32_t completionFilter = 0;
};

struct ChangeNotifyEntry {
  std::uint32_t action = 0;
  std::string name;
};

struct ChangeNotifyResponse {
  std::uint32_t status = 0;
  std::uint16_t outputBufferOffset = 0;
  std::vector<ChangeNotifyEntry> entries;
};

constexpr std::size_t kSmb2HeaderSize = 64;
constexpr std::size_t kDirectTcpHeaderSize = 4;
constexpr std::uint32_t kSmb2ProtocolId = 0x424D53FE;
constexpr std::uint16_t kSmb2HeaderStructureSize = 64;
constexpr std::uint16_t kNegotiateRequestStructureSize = 36;
constexpr std::uint16_t kNegotiateResponseStructureSize = 65;
constexpr std::uint16_t kSessionSetupRequestStructureSize = 25;
constexpr std::uint16_t kSessionSetupResponseStructureSize = 9;
constexpr std::uint16_t kLogoffRequestStructureSize = 4;
constexpr std::uint16_t kLogoffResponseStructureSize = 4;
constexpr std::uint16_t kTreeConnectRequestStructureSize = 9;
constexpr std::uint16_t kTreeConnectResponseStructureSize = 16;
constexpr std::uint16_t kTreeDisconnectRequestStructureSize = 4;
constexpr std::uint16_t kTreeDisconnectResponseStructureSize = 4;
constexpr std::uint16_t kCreateRequestStructureSize = 57;
constexpr std::uint16_t kCreateResponseStructureSize = 89;
constexpr std::uint16_t kCloseRequestStructureSize = 24;
constexpr std::uint16_t kCloseResponseStructureSize = 60;
constexpr std::uint16_t kReadRequestStructureSize = 49;
constexpr std::uint16_t kReadResponseStructureSize = 17;
constexpr std::uint16_t kWriteRequestStructureSize = 49;
constexpr std::uint16_t kWriteResponseStructureSize = 17;
constexpr std::uint16_t kIoctlRequestStructureSize = 57;
constexpr std::uint16_t kIoctlResponseStructureSize = 49;
constexpr std::uint16_t kSetInfoRequestStructureSize = 33;
constexpr std::uint16_t kSetInfoResponseStructureSize = 2;
constexpr std::uint16_t kQueryInfoRequestStructureSize = 41;
constexpr std::uint16_t kQueryInfoResponseStructureSize = 9;
constexpr std::uint16_t kQueryDirectoryRequestStructureSize = 33;
constexpr std::uint16_t kQueryDirectoryResponseStructureSize = 9;
constexpr std::uint16_t kChangeNotifyRequestStructureSize = 32;
constexpr std::uint16_t kChangeNotifyResponseStructureSize = 9;
constexpr std::uint32_t kStatusSuccess = 0x00000000;
constexpr std::uint32_t kStatusNotifyEnumDir = 0x0000010C;
constexpr std::uint32_t kStatusNoSuchFile = 0xC000000F;
constexpr std::uint32_t kStatusAccessDenied = 0xC0000022;
constexpr std::uint32_t kStatusObjectNameNotFound = 0xC0000034;
constexpr std::uint32_t kStatusObjectNameCollision = 0xC0000035;
constexpr std::uint32_t kStatusObjectPathNotFound = 0xC000003A;
constexpr std::uint32_t kStatusObjectPathSyntaxBad = 0xC000003B;
constexpr std::uint32_t kStatusInvalidParameter = 0xC000000D;
constexpr std::uint32_t kStatusInvalidDeviceRequest = 0xC0000010;
constexpr std::uint32_t kStatusMoreProcessingRequired = 0xC0000016;
constexpr std::uint32_t kStatusSharingViolation = 0xC0000043;
constexpr std::uint32_t kStatusLogonFailure = 0xC000006D;
constexpr std::uint32_t kStatusAccountRestriction = 0xC000006E;
constexpr std::uint32_t kStatusInvalidLogonHours = 0xC000006F;
constexpr std::uint32_t kStatusInvalidWorkstation = 0xC0000070;
constexpr std::uint32_t kStatusPasswordExpired = 0xC0000071;
constexpr std::uint32_t kStatusAccountDisabled = 0xC0000072;
constexpr std::uint32_t kStatusIoTimeout = 0xC00000B5;
constexpr std::uint32_t kStatusFileIsADirectory = 0xC00000BA;
constexpr std::uint32_t kStatusNotSupported = 0xC00000BB;
constexpr std::uint32_t kStatusNetworkNameDeleted = 0xC00000C9;
constexpr std::uint32_t kStatusNetworkAccessDenied = 0xC00000CA;
constexpr std::uint32_t kStatusBadNetworkName = 0xC00000CC;
constexpr std::uint32_t kStatusDirectoryNotEmpty = 0xC0000101;
constexpr std::uint32_t kStatusNotADirectory = 0xC0000103;
constexpr std::uint32_t kStatusPathNotCovered = 0xC0000257;
constexpr std::uint32_t kStatusNoMoreFiles = 0x80000006;
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
constexpr std::uint8_t kInfoTypeFile = 0x01;
constexpr std::uint8_t kInfoTypeSecurity = 0x03;
constexpr std::uint8_t kFileBasicInformation = 0x04;
constexpr std::uint8_t kFileStandardInformation = 0x05;
constexpr std::uint8_t kFileRenameInformation = 0x0A;
constexpr std::uint8_t kFileLinkInformation = 0x0B;
constexpr std::uint8_t kFileDispositionInformation = 0x0D;
constexpr std::uint8_t kFileFullEaInformation = 0x0F;
constexpr std::uint32_t kFileReadData = 0x00000001;
constexpr std::uint32_t kFileListDirectory = 0x00000001;
constexpr std::uint32_t kFileWriteData = 0x00000002;
constexpr std::uint32_t kFileAppendData = 0x00000004;
constexpr std::uint32_t kFileReadEa = 0x00000008;
constexpr std::uint32_t kFileWriteEa = 0x00000010;
constexpr std::uint32_t kFileReadAttributes = 0x00000080;
constexpr std::uint32_t kFileWriteAttributes = 0x00000100;
constexpr std::uint32_t kReadControlAccess = 0x00020000;
constexpr std::uint32_t kWriteDacAccess = 0x00040000;
constexpr std::uint32_t kWriteOwnerAccess = 0x00080000;
constexpr std::uint32_t kSynchronizeAccess = 0x00100000;
constexpr std::uint32_t kAccessSystemSecurity = 0x01000000;
constexpr std::uint32_t kFileAttributeDirectory = 0x00000010;
constexpr std::uint32_t kFileAttributeNormal = 0x00000080;
constexpr std::uint32_t kFileAttributeReparsePoint = 0x00000400;
constexpr std::uint32_t kFileShareRead = 0x00000001;
constexpr std::uint32_t kFileShareWrite = 0x00000002;
constexpr std::uint32_t kFileShareDelete = 0x00000004;
constexpr std::uint32_t kFileOpen = 0x00000001;
constexpr std::uint32_t kFileCreate = 0x00000002;
constexpr std::uint32_t kFileOpenIf = 0x00000003;
constexpr std::uint32_t kFileOverwrite = 0x00000004;
constexpr std::uint32_t kFileOverwriteIf = 0x00000005;
constexpr std::uint32_t kFileDirectoryFile = 0x00000001;
constexpr std::uint32_t kFileNonDirectoryFile = 0x00000040;
constexpr std::uint32_t kFileOpenReparsePoint = 0x00200000;
constexpr std::uint32_t kDeleteAccess = 0x00010000;
constexpr std::uint16_t kCloseFlagPostQueryAttrib = 0x0001;
constexpr std::uint32_t kWriteFlagWriteThrough = 0x00000001;
constexpr std::uint32_t kWriteFlagWriteUnbuffered = 0x00000002;
constexpr std::uint8_t kFileIdBothDirectoryInformation = 0x25;
constexpr std::uint8_t kQueryDirectoryRestartScans = 0x01;
constexpr std::uint16_t kSmb2WatchTree = 0x0001;
constexpr std::uint32_t kFileNotifyChangeFileName = 0x00000001;
constexpr std::uint32_t kFileNotifyChangeDirName = 0x00000002;
constexpr std::uint32_t kFileNotifyChangeAttributes = 0x00000004;
constexpr std::uint32_t kFileNotifyChangeSize = 0x00000008;
constexpr std::uint32_t kFileNotifyChangeLastWrite = 0x00000010;
constexpr std::uint32_t kFileNotifyChangeLastAccess = 0x00000020;
constexpr std::uint32_t kFileNotifyChangeCreation = 0x00000040;
constexpr std::uint32_t kFileNotifyChangeEa = 0x00000080;
constexpr std::uint32_t kFileNotifyChangeSecurity = 0x00000100;
constexpr std::uint32_t kOwnerSecurityInformation = 0x00000001;
constexpr std::uint32_t kGroupSecurityInformation = 0x00000002;
constexpr std::uint32_t kDaclSecurityInformation = 0x00000004;
constexpr std::uint32_t kSaclSecurityInformation = 0x00000008;
constexpr std::uint8_t kFileNeedEa = 0x80;
constexpr std::uint32_t kFileNotifyChangeStreamName = 0x00000200;
constexpr std::uint32_t kFileNotifyChangeStreamSize = 0x00000400;
constexpr std::uint32_t kFileNotifyChangeStreamWrite = 0x00000800;
constexpr std::uint32_t kFileNotifyChangeDefault =
    kFileNotifyChangeFileName | kFileNotifyChangeDirName |
    kFileNotifyChangeAttributes | kFileNotifyChangeSize |
    kFileNotifyChangeLastWrite | kFileNotifyChangeCreation;
constexpr std::uint32_t kFileActionAdded = 0x00000001;
constexpr std::uint32_t kFileActionRemoved = 0x00000002;
constexpr std::uint32_t kFileActionModified = 0x00000003;
constexpr std::uint32_t kFileActionRenamedOldName = 0x00000004;
constexpr std::uint32_t kFileActionRenamedNewName = 0x00000005;
constexpr std::uint32_t kIoctlIsFsctl = 0x00000001;
constexpr std::uint32_t kIoReparseTagSymlink = 0xA000000C;
constexpr std::uint32_t kSymlinkFlagRelative = 0x00000001;
constexpr std::uint32_t kFsctlSetReparsePoint = 0x000900A4;
constexpr std::uint32_t kFsctlSrvRequestResumeKey = 0x00140078;
constexpr std::uint32_t kFsctlSrvCopyChunk = 0x001440F2;

std::uint32_t capabilityMask(std::initializer_list<GlobalCapability> values);
std::uint16_t securityModeForPolicy(SecurityPolicy policy);
ErrorCode errorCodeForNtStatus(std::uint32_t status);
std::string ntStatusName(std::uint32_t status);

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
ByteVector buildTreeDisconnectRequest(std::uint64_t messageId,
                                      std::uint32_t treeId,
                                      std::uint64_t sessionId);
DecodeResult<TreeDisconnectResponse>
decodeTreeDisconnectResponse(const std::uint8_t *data, std::size_t size);
DecodeResult<TreeDisconnectResponse>
decodeTreeDisconnectResponse(const ByteVector &bytes);
ByteVector buildLogoffRequest(std::uint64_t messageId,
                              std::uint64_t sessionId);
DecodeResult<LogoffResponse>
decodeLogoffResponse(const std::uint8_t *data, std::size_t size);
DecodeResult<LogoffResponse> decodeLogoffResponse(const ByteVector &bytes);
ByteVector buildCreateRequest(const CreateRequestOptions &options,
                              std::uint64_t messageId,
                              std::uint32_t treeId,
                              std::uint64_t sessionId);
DecodeResult<CreateResponse> decodeCreateResponse(const std::uint8_t *data,
                                                  std::size_t size);
DecodeResult<CreateResponse> decodeCreateResponse(const ByteVector &bytes);
ByteVector buildCloseRequest(const CloseRequestOptions &options,
                             std::uint64_t messageId,
                             std::uint32_t treeId,
                             std::uint64_t sessionId);
DecodeResult<CloseResponse> decodeCloseResponse(const std::uint8_t *data,
                                                std::size_t size);
DecodeResult<CloseResponse> decodeCloseResponse(const ByteVector &bytes);
ByteVector buildReadRequest(const ReadRequestOptions &options,
                            std::uint64_t messageId,
                            std::uint32_t treeId,
                            std::uint64_t sessionId);
DecodeResult<ReadResponse> decodeReadResponse(const std::uint8_t *data,
                                              std::size_t size);
DecodeResult<ReadResponse> decodeReadResponse(const ByteVector &bytes);
ByteVector buildWriteRequest(const WriteRequestOptions &options,
                             std::uint64_t messageId,
                             std::uint32_t treeId,
                             std::uint64_t sessionId);
DecodeResult<WriteResponse> decodeWriteResponse(const std::uint8_t *data,
                                                std::size_t size);
DecodeResult<WriteResponse> decodeWriteResponse(const ByteVector &bytes);
ByteVector buildIoctlRequest(const IoctlRequestOptions &options,
                             std::uint64_t messageId,
                             std::uint32_t treeId,
                             std::uint64_t sessionId);
DecodeResult<IoctlResponse> decodeIoctlResponse(const std::uint8_t *data,
                                                std::size_t size);
DecodeResult<IoctlResponse> decodeIoctlResponse(const ByteVector &bytes);
ByteVector buildSrvCopyChunkRequest(const ByteVector &resumeKey,
                                    const std::vector<CopyChunk> &chunks);
DecodeResult<CopyChunkResponse>
decodeSrvCopyChunkResponse(const ByteVector &bytes);
ByteVector buildSymbolicLinkReparseBuffer(std::string_view substituteName,
                                          std::string_view printName,
                                          bool relative);
ByteVector buildSetInfoRequest(const SetInfoRequestOptions &options,
                               std::uint64_t messageId,
                               std::uint32_t treeId,
                               std::uint64_t sessionId);
DecodeResult<SetInfoResponse> decodeSetInfoResponse(const std::uint8_t *data,
                                                    std::size_t size);
DecodeResult<SetInfoResponse> decodeSetInfoResponse(const ByteVector &bytes);
ByteVector buildFileDispositionInformation(bool deletePending);
ByteVector buildFileRenameInformation(std::string_view newPath,
                                      bool replaceIfExists);
ByteVector buildFileLinkInformation(std::string_view newPath,
                                    bool replaceIfExists);
ByteVector buildQueryInfoRequest(const QueryInfoRequestOptions &options,
                                 std::uint64_t messageId,
                                 std::uint32_t treeId,
                                 std::uint64_t sessionId);
DecodeResult<QueryInfoResponse> decodeQueryInfoResponse(const std::uint8_t *data,
                                                        std::size_t size);
DecodeResult<QueryInfoResponse> decodeQueryInfoResponse(const ByteVector &bytes);
ByteVector buildFileBasicInformation(const FileBasicInformation &info);
DecodeResult<FileBasicInformation>
decodeFileBasicInformation(const ByteVector &bytes);
DecodeResult<FileStandardInformation>
decodeFileStandardInformation(const ByteVector &bytes);
ByteVector
buildFileFullEaInformation(const std::vector<FileFullEaInformation> &entries);
DecodeResult<std::vector<FileFullEaInformation>>
decodeFileFullEaInformation(const ByteVector &bytes);
ByteVector buildQueryDirectoryRequest(
    const QueryDirectoryRequestOptions &options, std::uint64_t messageId,
    std::uint32_t treeId, std::uint64_t sessionId);
DecodeResult<QueryDirectoryResponse>
decodeQueryDirectoryResponse(const std::uint8_t *data, std::size_t size);
DecodeResult<QueryDirectoryResponse>
decodeQueryDirectoryResponse(const ByteVector &bytes);
ByteVector buildChangeNotifyRequest(const ChangeNotifyRequestOptions &options,
                                    std::uint64_t messageId,
                                    std::uint32_t treeId,
                                    std::uint64_t sessionId);
DecodeResult<ChangeNotifyResponse>
decodeChangeNotifyResponse(const std::uint8_t *data, std::size_t size);
DecodeResult<ChangeNotifyResponse>
decodeChangeNotifyResponse(const ByteVector &bytes);

ByteVector encodeDirectTcpFrame(const ByteVector &smb2Message);
DecodeResult<std::uint32_t>
decodeDirectTcpPayloadLength(const std::uint8_t *data, std::size_t size);
DecodeResult<std::uint32_t>
decodeDirectTcpPayloadLength(const ByteVector &bytes);
DecodeResult<ByteVector> decodeDirectTcpPayload(const ByteVector &bytes);

bool containsSmb1Dialect(const std::vector<Dialect> &dialects);

} // namespace smb::native_smb
