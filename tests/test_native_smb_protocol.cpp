#include "Protocol.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

void appendU16Le(smb::native_smb::ByteVector &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void appendU32Le(smb::native_smb::ByteVector &bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

void appendU64Le(smb::native_smb::ByteVector &bytes, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

void appendZeros(smb::native_smb::ByteVector &bytes, std::size_t count) {
  bytes.insert(bytes.end(), count, 0);
}

std::uint16_t readU16Le(const smb::native_smb::ByteVector &bytes,
                        std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

std::uint32_t readU32Le(const smb::native_smb::ByteVector &bytes,
                        std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint64_t readU64Le(const smb::native_smb::ByteVector &bytes,
                        std::size_t offset) {
  std::uint64_t value = 0;
  for (int index = 7; index >= 0; --index) {
    value <<= 8;
    value |= bytes[offset + index];
  }
  return value;
}

smb::native_smb::ByteVector buildNegotiateResponse(
    smb::native_smb::Dialect dialect = smb::native_smb::Dialect::Smb302) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Negotiate;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 7;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kNegotiateResponseStructureSize);
  appendU16Le(bytes, 0x0003);
  appendU16Le(bytes, static_cast<std::uint16_t>(dialect));
  appendU16Le(bytes, 0);
  for (int i = 1; i <= 16; ++i) {
    bytes.push_back(static_cast<std::uint8_t>(i));
  }
  appendU32Le(bytes,
              smb::native_smb::capabilityMask(
                  {smb::native_smb::GlobalCapability::Dfs,
                   smb::native_smb::GlobalCapability::Encryption}));
  appendU32Le(bytes, 0x00100000);
  appendU32Le(bytes, 0x00200000);
  appendU32Le(bytes, 0x00300000);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU16Le(bytes, 128);
  appendU16Le(bytes, 4);
  appendU32Le(bytes, 0);
  bytes.push_back('G');
  bytes.push_back('S');
  bytes.push_back('S');
  bytes.push_back(0);
  return bytes;
}

smb::native_smb::ByteVector buildTreeConnectResponse() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::TreeConnect;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 14;
  header.treeId = 99;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kTreeConnectResponseStructureSize);
  bytes.push_back(static_cast<std::uint8_t>(
      smb::native_smb::ShareType::Disk));
  bytes.push_back(0);
  appendU32Le(bytes, smb::native_smb::kShareFlagDfsRoot |
                       smb::native_smb::kShareFlagEncryptData);
  appendU32Le(bytes, smb::native_smb::kShareCapabilityDfs);
  appendU32Le(bytes, 0x001F01FF);
  return bytes;
}

smb::native_smb::ByteVector buildSessionSetupResponse() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::SessionSetup;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.status = smb::native_smb::kStatusMoreProcessingRequired;
  header.messageId = 12;
  header.sessionId = 0x1122334455667788ULL;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kSessionSetupResponseStructureSize);
  appendU16Le(bytes, smb::native_smb::kSessionFlagEncryptData);
  appendU16Le(bytes, 72);
  appendU16Le(bytes, 3);
  bytes.push_back('G');
  bytes.push_back('S');
  bytes.push_back('S');
  return bytes;
}

smb::native_smb::ByteVector buildCreateResponse() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Create;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 20;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kCreateResponseStructureSize);
  bytes.push_back(0);
  bytes.push_back(0);
  appendU32Le(bytes, 1);
  appendU64Le(bytes, 10);
  appendU64Le(bytes, 20);
  appendU64Le(bytes, 30);
  appendU64Le(bytes, 40);
  appendU64Le(bytes, 4096);
  appendU64Le(bytes, 123);
  appendU32Le(bytes, smb::native_smb::kFileAttributeDirectory);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector buildCloseResponse() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Close;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 22;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kCloseResponseStructureSize);
  appendU16Le(bytes, smb::native_smb::kCloseFlagPostQueryAttrib);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 10);
  appendU64Le(bytes, 20);
  appendU64Le(bytes, 30);
  appendU64Le(bytes, 40);
  appendU64Le(bytes, 4096);
  appendU64Le(bytes, 123);
  appendU32Le(bytes, smb::native_smb::kFileAttributeDirectory);
  return bytes;
}

smb::native_smb::ByteVector buildReadResponse() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Read;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 23;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kReadResponseStructureSize);
  bytes.push_back(80);
  bytes.push_back(0);
  appendU32Le(bytes, 5);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  bytes.push_back('h');
  bytes.push_back('e');
  bytes.push_back('l');
  bytes.push_back('l');
  bytes.push_back('o');
  return bytes;
}

smb::native_smb::ByteVector buildWriteResponse() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Write;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 24;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kWriteResponseStructureSize);
  appendU16Le(bytes, 0);
  appendU32Le(bytes, 5);
  appendU32Le(bytes, 0);
  appendU16Le(bytes, 0);
  appendU16Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector buildIoctlResponse(
    const smb::native_smb::ByteVector &output) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Ioctl;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 25;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kIoctlResponseStructureSize);
  appendU16Le(bytes, 0);
  appendU32Le(bytes, smb::native_smb::kFsctlSrvCopyChunk);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 112);
  appendU32Le(bytes, static_cast<std::uint32_t>(output.size()));
  appendU32Le(bytes, smb::native_smb::kIoctlIsFsctl);
  appendU32Le(bytes, 0);
  bytes.insert(bytes.end(), output.begin(), output.end());
  return bytes;
}

smb::native_smb::ByteVector buildSetInfoResponse() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::SetInfo;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 25;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kSetInfoResponseStructureSize);
  return bytes;
}

smb::native_smb::ByteVector fileBasicInformationBuffer() {
  smb::native_smb::ByteVector bytes;
  appendU64Le(bytes, 10);
  appendU64Le(bytes, 20);
  appendU64Le(bytes, 30);
  appendU64Le(bytes, 40);
  appendU32Le(bytes, smb::native_smb::kFileAttributeReparsePoint);
  appendU32Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector fileStandardInformationBuffer() {
  smb::native_smb::ByteVector bytes;
  appendU64Le(bytes, 4096);
  appendU64Le(bytes, 123);
  appendU32Le(bytes, 2);
  bytes.push_back(1);
  bytes.push_back(0);
  appendU16Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector buildQueryInfoResponse(
    const smb::native_smb::ByteVector &buffer) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::QueryInfo;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 26;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kQueryInfoResponseStructureSize);
  appendU16Le(bytes, 72);
  appendU32Le(bytes, static_cast<std::uint32_t>(buffer.size()));
  bytes.insert(bytes.end(), buffer.begin(), buffer.end());
  return bytes;
}

smb::native_smb::ByteVector buildStatusOnlyResponse(
    smb::native_smb::Command command, std::uint32_t status) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.status = status;
  header.messageId = 99;
  header.treeId = 77;
  header.sessionId = 34;
  return smb::native_smb::encodeSmb2SyncHeader(header);
}

smb::native_smb::ByteVector buildEmptyStructureResponse(
    smb::native_smb::Command command, std::uint16_t structureSize) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 99;
  header.treeId = 77;
  header.sessionId = 34;
  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, structureSize);
  appendU16Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector fileIdBothEntry(
    const std::string &name, std::uint32_t nextEntryOffset,
    std::uint32_t attributes, std::uint64_t size, std::uint64_t fileId) {
  auto encodedName = smb::native_smb::encodeUtf16Le(name);
  smb::native_smb::ByteVector bytes;
  appendU32Le(bytes, nextEntryOffset);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 1);
  appendU64Le(bytes, 2);
  appendU64Le(bytes, 3);
  appendU64Le(bytes, 4);
  appendU64Le(bytes, size);
  appendU64Le(bytes, size);
  appendU32Le(bytes, attributes);
  appendU32Le(bytes, static_cast<std::uint32_t>(encodedName.size()));
  appendU32Le(bytes, 0);
  bytes.push_back(0);
  bytes.push_back(0);
  appendZeros(bytes, 24);
  appendU16Le(bytes, 0);
  appendU64Le(bytes, fileId);
  bytes.insert(bytes.end(), encodedName.begin(), encodedName.end());
  if (nextEntryOffset > bytes.size()) {
    appendZeros(bytes, nextEntryOffset - bytes.size());
  }
  return bytes;
}

smb::native_smb::ByteVector buildQueryDirectoryResponse() {
  auto entries = fileIdBothEntry("alpha.txt", 128, 0, 123, 1001);
  const auto folder = fileIdBothEntry(
      "folder", 0, smb::native_smb::kFileAttributeDirectory, 0, 1002);
  entries.insert(entries.end(), folder.begin(), folder.end());

  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::QueryDirectory;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 21;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kQueryDirectoryResponseStructureSize);
  appendU16Le(bytes, 72);
  appendU32Le(bytes, static_cast<std::uint32_t>(entries.size()));
  bytes.insert(bytes.end(), entries.begin(), entries.end());
  return bytes;
}

smb::native_smb::ByteVector notifyEntry(const std::string &name,
                                        std::uint32_t action,
                                        std::uint32_t nextEntryOffset) {
  auto encodedName = smb::native_smb::encodeUtf16Le(name);
  smb::native_smb::ByteVector bytes;
  appendU32Le(bytes, nextEntryOffset);
  appendU32Le(bytes, action);
  appendU32Le(bytes, static_cast<std::uint32_t>(encodedName.size()));
  bytes.insert(bytes.end(), encodedName.begin(), encodedName.end());
  if (nextEntryOffset > bytes.size()) {
    appendZeros(bytes, nextEntryOffset - bytes.size());
  }
  return bytes;
}

smb::native_smb::ByteVector buildChangeNotifyResponse(
    std::uint32_t status = smb::native_smb::kStatusSuccess) {
  auto entries = notifyEntry("old.txt",
                             smb::native_smb::kFileActionRenamedOldName, 28);
  const auto newEntry = notifyEntry(
      "new.txt", smb::native_smb::kFileActionRenamedNewName, 0);
  entries.insert(entries.end(), newEntry.begin(), newEntry.end());

  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::ChangeNotify;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.status = status;
  header.messageId = 27;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kChangeNotifyResponseStructureSize);
  appendU16Le(bytes, 72);
  appendU32Le(bytes, static_cast<std::uint32_t>(entries.size()));
  bytes.insert(bytes.end(), entries.begin(), entries.end());
  return bytes;
}

} // namespace

class NativeSmbProtocolTest final : public QObject {
  Q_OBJECT

private slots:
  void encodesAndDecodesSyncHeader() {
    smb::native_smb::Smb2SyncHeader header;
    header.command = smb::native_smb::Command::TreeConnect;
    header.creditCharge = 2;
    header.creditRequest = 8;
    header.flags = smb::native_smb::kFlagSigned;
    header.nextCommand = 128;
    header.messageId = 0x0102030405060708ULL;
    header.treeId = 0x11223344;
    header.sessionId = 0x8877665544332211ULL;
    header.signature[0] = 0xAA;
    header.signature[15] = 0x55;

    const auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);

    QCOMPARE(bytes.size(), smb::native_smb::kSmb2HeaderSize);
    QCOMPARE(bytes[0], std::uint8_t{0xFE});
    QCOMPARE(bytes[1], std::uint8_t{'S'});
    QCOMPARE(bytes[2], std::uint8_t{'M'});
    QCOMPARE(bytes[3], std::uint8_t{'B'});
    QCOMPARE(bytes[4], std::uint8_t{64});
    QCOMPARE(bytes[12], std::uint8_t{0x03});
    QCOMPARE(bytes[13], std::uint8_t{0x00});

    const auto decoded = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(decoded.ok);
    QCOMPARE(static_cast<int>(decoded.value.command),
             static_cast<int>(smb::native_smb::Command::TreeConnect));
    QCOMPARE(decoded.value.creditCharge, std::uint16_t{2});
    QCOMPARE(decoded.value.creditRequest, std::uint16_t{8});
    QCOMPARE(decoded.value.flags, smb::native_smb::kFlagSigned);
    QCOMPARE(decoded.value.nextCommand, std::uint32_t{128});
    QCOMPARE(decoded.value.messageId, std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(decoded.value.treeId, std::uint32_t{0x11223344});
    QCOMPARE(decoded.value.sessionId, std::uint64_t{0x8877665544332211ULL});
    QCOMPARE(decoded.value.signature[0], std::uint8_t{0xAA});
    QCOMPARE(decoded.value.signature[15], std::uint8_t{0x55});
  }

  void rejectsInvalidHeaders() {
    smb::native_smb::ByteVector shortHeader(8, 0);
    auto decoded = smb::native_smb::decodeSmb2SyncHeader(shortHeader);
    QVERIFY(!decoded.ok);
    QCOMPARE(static_cast<int>(decoded.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::IoError));

    auto invalidProtocol = smb::native_smb::encodeSmb2SyncHeader({});
    invalidProtocol[0] = 0xFF;
    decoded = smb::native_smb::decodeSmb2SyncHeader(invalidProtocol);
    QVERIFY(!decoded.ok);
    QCOMPARE(static_cast<int>(decoded.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }

  void buildsNegotiateRequestWithoutSmb1Dialects() {
    smb::native_smb::NegotiateRequestOptions options;
    options.signing = smb::native_smb::SecurityPolicy::Required;
    options.capabilities = smb::native_smb::capabilityMask(
        {smb::native_smb::GlobalCapability::Dfs,
         smb::native_smb::GlobalCapability::Encryption});
    for (std::size_t index = 0; index < options.clientGuid.size(); ++index) {
      options.clientGuid[index] = static_cast<std::uint8_t>(index + 1);
    }

    const auto bytes = smb::native_smb::buildNegotiateRequest(options, 7);

    QCOMPARE(bytes.size(), std::size_t{176});
    QCOMPARE(bytes[64], std::uint8_t{36});
    QCOMPARE(bytes[65], std::uint8_t{0});
    QCOMPARE(bytes[66], std::uint8_t{5});
    QCOMPARE(bytes[67], std::uint8_t{0});
    QCOMPARE(bytes[68], std::uint8_t{3});
    QCOMPARE(bytes[69], std::uint8_t{0});
    QCOMPARE(bytes[72], std::uint8_t{0x41});
    QCOMPARE(bytes[73], std::uint8_t{0});
    QCOMPARE(bytes[76], std::uint8_t{1});
    QCOMPARE(bytes[91], std::uint8_t{16});
    QCOMPARE(bytes[92], std::uint8_t{0x70});
    QCOMPARE(bytes[93], std::uint8_t{0});
    QCOMPARE(bytes[96], std::uint8_t{2});
    QCOMPARE(bytes[97], std::uint8_t{0});
    QCOMPARE(bytes[100], std::uint8_t{0x02});
    QCOMPARE(bytes[101], std::uint8_t{0x02});
    QCOMPARE(bytes[102], std::uint8_t{0x10});
    QCOMPARE(bytes[103], std::uint8_t{0x02});
    QCOMPARE(bytes[104], std::uint8_t{0x00});
    QCOMPARE(bytes[105], std::uint8_t{0x03});
    QCOMPARE(bytes[106], std::uint8_t{0x02});
    QCOMPARE(bytes[107], std::uint8_t{0x03});
    QCOMPARE(bytes[108], std::uint8_t{0x11});
    QCOMPARE(bytes[109], std::uint8_t{0x03});
    QCOMPARE(bytes[112], std::uint8_t{0x01});
    QCOMPARE(bytes[113], std::uint8_t{0x00});
    QCOMPARE(bytes[114], std::uint8_t{38});
    QCOMPARE(bytes[120], std::uint8_t{1});
    QCOMPARE(bytes[122], std::uint8_t{32});
    QCOMPARE(bytes[124], std::uint8_t{1});
    QCOMPARE(bytes[126], std::uint8_t{1});
    QCOMPARE(bytes[141], std::uint8_t{16});
    QCOMPARE(bytes[142], std::uint8_t{1});
    QCOMPARE(bytes[157], std::uint8_t{16});
    QCOMPARE(bytes[160], std::uint8_t{0x02});
    QCOMPARE(bytes[162], std::uint8_t{4});
    QCOMPARE(bytes[168], std::uint8_t{1});
    QCOMPARE(bytes[170], std::uint8_t{1});

    const auto defaultDialects = smb::native_smb::defaultInitialDialects();
    QVERIFY(!smb::native_smb::containsSmb1Dialect(defaultDialects));
    QVERIFY(std::find(defaultDialects.begin(), defaultDialects.end(),
                      smb::native_smb::Dialect::Smb311) !=
            defaultDialects.end());
  }

  void decodesNegotiateResponse() {
    const auto bytes = buildNegotiateResponse();

    const auto decoded = smb::native_smb::decodeNegotiateResponse(
        bytes, smb::native_smb::defaultInitialDialects());

    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.securityMode, std::uint16_t{0x0003});
    QCOMPARE(static_cast<int>(decoded.value.dialect),
             static_cast<int>(smb::native_smb::Dialect::Smb302));
    QCOMPARE(decoded.value.serverGuid[0], std::uint8_t{1});
    QCOMPARE(decoded.value.serverGuid[15], std::uint8_t{16});
    QCOMPARE(decoded.value.capabilities, std::uint32_t{0x00000041});
    QCOMPARE(decoded.value.maxTransactSize, std::uint32_t{0x00100000});
    QCOMPARE(decoded.value.maxReadSize, std::uint32_t{0x00200000});
    QCOMPARE(decoded.value.maxWriteSize, std::uint32_t{0x00300000});
    QCOMPARE(decoded.value.systemTime, std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(decoded.value.serverStartTime,
             std::uint64_t{0x1112131415161718ULL});
    QCOMPARE(decoded.value.securityBuffer.size(), std::size_t{4});
    QCOMPARE(decoded.value.securityBuffer[0], std::uint8_t{'G'});
  }

  void rejectsNegotiateResponseWithUnofferedDialect() {
    const auto bytes =
        buildNegotiateResponse(smb::native_smb::Dialect::Smb311);

    const auto decoded = smb::native_smb::decodeNegotiateResponse(
        bytes, {smb::native_smb::Dialect::Smb202});

    QVERIFY(!decoded.ok);
    QCOMPARE(static_cast<int>(decoded.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }

  void mapsNtStatusToTypedNativeErrors() {
    QCOMPARE(static_cast<int>(smb::native_smb::errorCodeForNtStatus(
                 smb::native_smb::kStatusLogonFailure)),
             static_cast<int>(
                 smb::native_smb::ErrorCode::AuthenticationFailed));
    QCOMPARE(static_cast<int>(smb::native_smb::errorCodeForNtStatus(
                 smb::native_smb::kStatusAccessDenied)),
             static_cast<int>(smb::native_smb::ErrorCode::PermissionDenied));
    QCOMPARE(static_cast<int>(smb::native_smb::errorCodeForNtStatus(
                 smb::native_smb::kStatusBadNetworkName)),
             static_cast<int>(smb::native_smb::ErrorCode::ShareUnavailable));
    QCOMPARE(static_cast<int>(smb::native_smb::errorCodeForNtStatus(
                 smb::native_smb::kStatusObjectNameNotFound)),
             static_cast<int>(smb::native_smb::ErrorCode::FileNotFound));
    QCOMPARE(static_cast<int>(smb::native_smb::errorCodeForNtStatus(
                 smb::native_smb::kStatusDirectoryNotEmpty)),
             static_cast<int>(
                 smb::native_smb::ErrorCode::DirectoryNotEmpty));
    QCOMPARE(QString::fromStdString(smb::native_smb::ntStatusName(
                 smb::native_smb::kStatusPathNotCovered)),
             QStringLiteral("STATUS_PATH_NOT_COVERED"));
  }

  void responseDecodersReturnNtStatusFailuresBeforeParsingBodies() {
    const auto session = smb::native_smb::decodeSessionSetupResponse(
        buildStatusOnlyResponse(smb::native_smb::Command::SessionSetup,
                                smb::native_smb::kStatusLogonFailure));
    QVERIFY(!session.ok);
    QCOMPARE(static_cast<int>(session.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::AuthenticationFailed));

    const auto tree = smb::native_smb::decodeTreeConnectResponse(
        buildStatusOnlyResponse(smb::native_smb::Command::TreeConnect,
                                smb::native_smb::kStatusBadNetworkName));
    QVERIFY(!tree.ok);
    QCOMPARE(static_cast<int>(tree.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::ShareUnavailable));

    const auto create = smb::native_smb::decodeCreateResponse(
        buildStatusOnlyResponse(smb::native_smb::Command::Create,
                                smb::native_smb::kStatusAccessDenied));
    QVERIFY(!create.ok);
    QCOMPARE(static_cast<int>(create.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::PermissionDenied));

    const auto read = smb::native_smb::decodeReadResponse(
        buildStatusOnlyResponse(smb::native_smb::Command::Read,
                                smb::native_smb::kStatusObjectNameNotFound));
    QVERIFY(!read.ok);
    QCOMPARE(static_cast<int>(read.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::FileNotFound));
  }

  void queryDirectoryNoMoreFilesReturnsEmptySuccess() {
    const auto response = smb::native_smb::decodeQueryDirectoryResponse(
        buildStatusOnlyResponse(smb::native_smb::Command::QueryDirectory,
                                smb::native_smb::kStatusNoMoreFiles));

    QVERIFY(response.ok);
    QCOMPARE(response.value.status, smb::native_smb::kStatusNoMoreFiles);
    QCOMPARE(response.value.entries.size(), std::size_t{0});
  }

  void buildsSessionSetupRequest() {
    smb::native_smb::SessionSetupRequestOptions options;
    options.flags = 0x01;
    options.signing = smb::native_smb::SecurityPolicy::Preferred;
    options.capabilities = smb::native_smb::capabilityMask(
        {smb::native_smb::GlobalCapability::Dfs});
    options.previousSessionId = 0x0102030405060708ULL;
    options.securityBuffer = {'N', 'T', 'L', 'M'};

    const auto bytes =
        smb::native_smb::buildSessionSetupRequest(options, 12, 34);

    QCOMPARE(bytes.size(), std::size_t{92});
    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kSessionSetupRequestStructureSize);
    QCOMPARE(bytes[66], std::uint8_t{0x01});
    QCOMPARE(bytes[67], std::uint8_t{0x01});
    QCOMPARE(readU32Le(bytes, 68), std::uint32_t{0x00000001});
    QCOMPARE(readU32Le(bytes, 72), std::uint32_t{0});
    QCOMPARE(readU16Le(bytes, 76), std::uint16_t{88});
    QCOMPARE(readU16Le(bytes, 78), std::uint16_t{4});
    QCOMPARE(readU64Le(bytes, 80), std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(bytes[88], std::uint8_t{'N'});
    QCOMPARE(bytes[91], std::uint8_t{'M'});

    const auto header = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::SessionSetup));
    QCOMPARE(header.value.messageId, std::uint64_t{12});
    QCOMPARE(header.value.sessionId, std::uint64_t{34});
  }

  void decodesSessionSetupResponse() {
    const auto response =
        smb::native_smb::decodeSessionSetupResponse(buildSessionSetupResponse());

    QVERIFY(response.ok);
    QCOMPARE(response.value.status,
             smb::native_smb::kStatusMoreProcessingRequired);
    QCOMPARE(response.value.sessionId, std::uint64_t{0x1122334455667788ULL});
    QCOMPARE(response.value.sessionFlags,
             smb::native_smb::kSessionFlagEncryptData);
    QVERIFY(response.value.moreProcessingRequired);
    QVERIFY(!response.value.guestSession);
    QVERIFY(!response.value.nullSession);
    QVERIFY(response.value.encryptData);
    QCOMPARE(response.value.securityBuffer.size(), std::size_t{3});
    QCOMPARE(response.value.securityBuffer[0], std::uint8_t{'G'});
  }

  void encodesUtf16Le() {
    const auto ascii = smb::native_smb::encodeUtf16Le("A");
    QCOMPARE(ascii.size(), std::size_t{2});
    QCOMPARE(ascii[0], std::uint8_t{'A'});
    QCOMPARE(ascii[1], std::uint8_t{0});

    const auto cyrillic = smb::native_smb::encodeUtf16Le("я");
    QCOMPARE(cyrillic.size(), std::size_t{2});
    QCOMPARE(readU16Le(cyrillic, 0), std::uint16_t{0x044F});

    QVERIFY_EXCEPTION_THROWN(
        smb::native_smb::encodeUtf16Le(std::string("\xC3", 1)),
        std::invalid_argument);
  }

  void decodesUtf16Le() {
    const auto bytes = smb::native_smb::encodeUtf16Le("папка");
    const auto decoded = smb::native_smb::decodeUtf16Le(bytes);

    QVERIFY(decoded.ok);
    QCOMPARE(QString::fromStdString(decoded.value), QStringLiteral("папка"));

    const smb::native_smb::ByteVector invalid{0x00, 0xD8};
    const auto failed = smb::native_smb::decodeUtf16Le(invalid);
    QVERIFY(!failed.ok);
  }

  void buildsTreeConnectRequest() {
    smb::native_smb::TreeConnectRequestOptions options;
    options.server = "server";
    options.share = "share";

    const auto bytes =
        smb::native_smb::buildTreeConnectRequest(options, 14, 34);

    QCOMPARE(bytes.size(), std::size_t{100});
    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kTreeConnectRequestStructureSize);
    QCOMPARE(readU16Le(bytes, 66), std::uint16_t{0});
    QCOMPARE(readU16Le(bytes, 68), std::uint16_t{72});
    QCOMPARE(readU16Le(bytes, 70), std::uint16_t{28});
    QCOMPARE(readU16Le(bytes, 72), std::uint16_t{'\\'});
    QCOMPARE(readU16Le(bytes, 74), std::uint16_t{'\\'});
    QCOMPARE(readU16Le(bytes, 76), std::uint16_t{'s'});
    QCOMPARE(readU16Le(bytes, 98), std::uint16_t{'e'});

    const auto header = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::TreeConnect));
    QCOMPARE(header.value.messageId, std::uint64_t{14});
    QCOMPARE(header.value.sessionId, std::uint64_t{34});
  }

  void decodesTreeConnectResponse() {
    const auto response =
        smb::native_smb::decodeTreeConnectResponse(buildTreeConnectResponse());

    QVERIFY(response.ok);
    QCOMPARE(static_cast<int>(response.value.shareType),
             static_cast<int>(smb::native_smb::ShareType::Disk));
    QCOMPARE(response.value.shareFlags,
             smb::native_smb::kShareFlagDfsRoot |
                 smb::native_smb::kShareFlagEncryptData);
    QCOMPARE(response.value.capabilities,
             smb::native_smb::kShareCapabilityDfs);
    QCOMPARE(response.value.maximalAccess, std::uint32_t{0x001F01FF});
    QVERIFY(response.value.isDfs);
    QVERIFY(response.value.isDfsRoot);
    QVERIFY(response.value.requiresEncryption);
  }

  void buildsCreateDirectoryRequest() {
    smb::native_smb::CreateRequestOptions options;
    options.path = "folder";
    options.desiredAccess =
        smb::native_smb::kFileListDirectory |
        smb::native_smb::kFileReadEa |
        smb::native_smb::kFileReadAttributes;
    options.fileAttributes = smb::native_smb::kFileAttributeNormal;
    options.shareAccess = smb::native_smb::kFileShareRead |
                          smb::native_smb::kFileShareWrite |
                          smb::native_smb::kFileShareDelete;
    options.createDisposition = smb::native_smb::kFileOpen;
    options.createOptions = smb::native_smb::kFileDirectoryFile;

    const auto bytes =
        smb::native_smb::buildCreateRequest(options, 20, 77, 34);

    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kCreateRequestStructureSize);
    QCOMPARE(bytes[66], std::uint8_t{0});
    QCOMPARE(bytes[67], std::uint8_t{0});
    QCOMPARE(readU32Le(bytes, 68), std::uint32_t{2});
    QCOMPARE(readU32Le(bytes, 88), std::uint32_t{0x00000089});
    QCOMPARE(readU32Le(bytes, 96), std::uint32_t{7});
    QCOMPARE(readU32Le(bytes, 100), smb::native_smb::kFileOpen);
    QCOMPARE(readU32Le(bytes, 104), smb::native_smb::kFileDirectoryFile);
    QCOMPARE(readU16Le(bytes, 108), std::uint16_t{120});
    QCOMPARE(readU16Le(bytes, 110), std::uint16_t{12});
    QCOMPARE(readU16Le(bytes, 120), std::uint16_t{'f'});

    const auto header = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::Create));
    QCOMPARE(header.value.treeId, std::uint32_t{77});
    QCOMPARE(header.value.sessionId, std::uint64_t{34});
  }

  void buildsCreateRootDirectoryRequestWithRequiredBufferByte() {
    smb::native_smb::CreateRequestOptions options;
    options.desiredAccess =
        smb::native_smb::kFileListDirectory |
        smb::native_smb::kFileReadEa |
        smb::native_smb::kFileReadAttributes;
    options.fileAttributes = smb::native_smb::kFileAttributeNormal;
    options.shareAccess = smb::native_smb::kFileShareRead |
                          smb::native_smb::kFileShareWrite |
                          smb::native_smb::kFileShareDelete;
    options.createDisposition = smb::native_smb::kFileOpen;
    options.createOptions = smb::native_smb::kFileDirectoryFile;

    const auto bytes =
        smb::native_smb::buildCreateRequest(options, 20, 77, 34);

    QCOMPARE(bytes.size(), std::size_t{121});
    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kCreateRequestStructureSize);
    QCOMPARE(readU16Le(bytes, 108), std::uint16_t{120});
    QCOMPARE(readU16Le(bytes, 110), std::uint16_t{0});
    QCOMPARE(readU32Le(bytes, 112), std::uint32_t{0});
    QCOMPARE(readU32Le(bytes, 116), std::uint32_t{0});
    QCOMPARE(bytes[120], std::uint8_t{0});
  }

  void decodesCreateResponse() {
    const auto response =
        smb::native_smb::decodeCreateResponse(buildCreateResponse());

    QVERIFY(response.ok);
    QCOMPARE(response.value.createAction, std::uint32_t{1});
    QCOMPARE(response.value.creationTime, std::uint64_t{10});
    QCOMPARE(response.value.endOfFile, std::uint64_t{123});
    QCOMPARE(response.value.fileAttributes,
             smb::native_smb::kFileAttributeDirectory);
    QCOMPARE(response.value.fileId.persistent,
             std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(response.value.fileId.volatileId,
             std::uint64_t{0x1112131415161718ULL});
    QVERIFY(!response.value.isReparsePoint);
  }

  void buildsCloseRequest() {
    smb::native_smb::CloseRequestOptions options;
    options.flags = smb::native_smb::kCloseFlagPostQueryAttrib;
    options.fileId.persistent = 0x0102030405060708ULL;
    options.fileId.volatileId = 0x1112131415161718ULL;

    const auto bytes =
        smb::native_smb::buildCloseRequest(options, 22, 77, 34);

    QCOMPARE(bytes.size(), std::size_t{88});
    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kCloseRequestStructureSize);
    QCOMPARE(readU16Le(bytes, 66),
             smb::native_smb::kCloseFlagPostQueryAttrib);
    QCOMPARE(readU32Le(bytes, 68), std::uint32_t{0});
    QCOMPARE(readU64Le(bytes, 72), std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(readU64Le(bytes, 80), std::uint64_t{0x1112131415161718ULL});

    const auto header = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::Close));
    QCOMPARE(header.value.treeId, std::uint32_t{77});
    QCOMPARE(header.value.sessionId, std::uint64_t{34});
  }

  void decodesCloseResponse() {
    const auto response =
        smb::native_smb::decodeCloseResponse(buildCloseResponse());

    QVERIFY(response.ok);
    QCOMPARE(response.value.flags,
             smb::native_smb::kCloseFlagPostQueryAttrib);
    QVERIFY(response.value.hasPostQueryAttributes);
    QCOMPARE(response.value.creationTime, std::uint64_t{10});
    QCOMPARE(response.value.lastWriteTime, std::uint64_t{30});
    QCOMPARE(response.value.allocationSize, std::uint64_t{4096});
    QCOMPARE(response.value.endOfFile, std::uint64_t{123});
    QCOMPARE(response.value.fileAttributes,
             smb::native_smb::kFileAttributeDirectory);
  }

  void buildsReadRequest() {
    smb::native_smb::ReadRequestOptions options;
    options.fileId.persistent = 0x0102030405060708ULL;
    options.fileId.volatileId = 0x1112131415161718ULL;
    options.length = 131072;
    options.offset = 4096;

    const auto bytes = smb::native_smb::buildReadRequest(options, 23, 77, 34);

    QCOMPARE(bytes.size(), std::size_t{113});
    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kReadRequestStructureSize);
    QCOMPARE(bytes[66], std::uint8_t{0});
    QCOMPARE(bytes[67], std::uint8_t{0});
    QCOMPARE(readU32Le(bytes, 68), std::uint32_t{131072});
    QCOMPARE(readU64Le(bytes, 72), std::uint64_t{4096});
    QCOMPARE(readU64Le(bytes, 80), std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(readU64Le(bytes, 88), std::uint64_t{0x1112131415161718ULL});
    QCOMPARE(readU32Le(bytes, 96), std::uint32_t{0});
    QCOMPARE(readU32Le(bytes, 100), std::uint32_t{0});
    QCOMPARE(readU32Le(bytes, 104), std::uint32_t{0});
    QCOMPARE(readU16Le(bytes, 108), std::uint16_t{0});
    QCOMPARE(readU16Le(bytes, 110), std::uint16_t{0});
    QCOMPARE(bytes[112], std::uint8_t{0});

    const auto header = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::Read));
    QCOMPARE(header.value.creditCharge, std::uint16_t{2});
  }

  void decodesReadResponse() {
    const auto response =
        smb::native_smb::decodeReadResponse(buildReadResponse());

    QVERIFY(response.ok);
    QCOMPARE(response.value.dataOffset, std::uint8_t{80});
    QCOMPARE(response.value.dataRemaining, std::uint32_t{0});
    QCOMPARE(response.value.data.size(), std::size_t{5});
    QCOMPARE(QString::fromUtf8(
                 reinterpret_cast<const char *>(response.value.data.data()),
                 static_cast<int>(response.value.data.size())),
             QStringLiteral("hello"));
  }

  void buildsWriteRequest() {
    smb::native_smb::WriteRequestOptions options;
    options.fileId.persistent = 0x0102030405060708ULL;
    options.fileId.volatileId = 0x1112131415161718ULL;
    options.data = {'h', 'e', 'l', 'l', 'o'};
    options.offset = 4096;
    options.flags = smb::native_smb::kWriteFlagWriteThrough;

    const auto bytes = smb::native_smb::buildWriteRequest(options, 24, 77, 34);

    QCOMPARE(bytes.size(), std::size_t{117});
    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kWriteRequestStructureSize);
    QCOMPARE(readU16Le(bytes, 66), std::uint16_t{112});
    QCOMPARE(readU32Le(bytes, 68), std::uint32_t{5});
    QCOMPARE(readU64Le(bytes, 72), std::uint64_t{4096});
    QCOMPARE(readU64Le(bytes, 80), std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(readU64Le(bytes, 88), std::uint64_t{0x1112131415161718ULL});
    QCOMPARE(readU32Le(bytes, 96), std::uint32_t{0});
    QCOMPARE(readU32Le(bytes, 100), std::uint32_t{0});
    QCOMPARE(readU16Le(bytes, 104), std::uint16_t{0});
    QCOMPARE(readU16Le(bytes, 106), std::uint16_t{0});
    QCOMPARE(readU32Le(bytes, 108), smb::native_smb::kWriteFlagWriteThrough);
    QCOMPARE(bytes[112], std::uint8_t{'h'});
    QCOMPARE(bytes[116], std::uint8_t{'o'});

    const auto header = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::Write));
    QCOMPARE(header.value.creditCharge, std::uint16_t{1});
  }

  void decodesWriteResponse() {
    const auto response =
        smb::native_smb::decodeWriteResponse(buildWriteResponse());

    QVERIFY(response.ok);
    QCOMPARE(response.value.count, std::uint32_t{5});
    QCOMPARE(response.value.remaining, std::uint32_t{0});
    QCOMPARE(response.value.writeChannelInfoOffset, std::uint16_t{0});
    QCOMPARE(response.value.writeChannelInfoLength, std::uint16_t{0});
  }

  void buildsIoctlRequest() {
    smb::native_smb::IoctlRequestOptions options;
    options.ctlCode = smb::native_smb::kFsctlSrvRequestResumeKey;
    options.fileId.persistent = 0x0102030405060708ULL;
    options.fileId.volatileId = 0x1112131415161718ULL;
    options.input = {'a', 'b'};
    options.maxOutputResponse = 32;
    options.flags = smb::native_smb::kIoctlIsFsctl;

    const auto bytes = smb::native_smb::buildIoctlRequest(options, 25, 77, 34);

    QCOMPARE(bytes.size(), std::size_t{122});
    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kIoctlRequestStructureSize);
    QCOMPARE(readU32Le(bytes, 68),
             smb::native_smb::kFsctlSrvRequestResumeKey);
    QCOMPARE(readU64Le(bytes, 72), std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(readU64Le(bytes, 80), std::uint64_t{0x1112131415161718ULL});
    QCOMPARE(readU32Le(bytes, 88), std::uint32_t{120});
    QCOMPARE(readU32Le(bytes, 92), std::uint32_t{2});
    QCOMPARE(readU32Le(bytes, 108), std::uint32_t{32});
    QCOMPARE(readU32Le(bytes, 112), smb::native_smb::kIoctlIsFsctl);
    QCOMPARE(bytes[120], std::uint8_t{'a'});
    QCOMPARE(bytes[121], std::uint8_t{'b'});

    const auto header = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::Ioctl));
  }

  void decodesIoctlResponse() {
    smb::native_smb::ByteVector output;
    appendU32Le(output, 1);
    appendU32Le(output, 512);
    appendU32Le(output, 512);

    const auto response =
        smb::native_smb::decodeIoctlResponse(buildIoctlResponse(output));

    QVERIFY(response.ok);
    QCOMPARE(response.value.ctlCode, smb::native_smb::kFsctlSrvCopyChunk);
    QCOMPARE(response.value.fileId.persistent,
             std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(response.value.output.size(), std::size_t{12});
    QCOMPARE(response.value.flags, smb::native_smb::kIoctlIsFsctl);
  }

  void buildsAndDecodesSrvCopyChunkPayloads() {
    smb::native_smb::ByteVector resumeKey(24, 0xAB);
    const auto request = smb::native_smb::buildSrvCopyChunkRequest(
        resumeKey, {smb::native_smb::CopyChunk{10, 20, 4096}});

    QCOMPARE(request.size(), std::size_t{56});
    QCOMPARE(request[0], std::uint8_t{0xAB});
    QCOMPARE(readU32Le(request, 24), std::uint32_t{1});
    QCOMPARE(readU64Le(request, 32), std::uint64_t{10});
    QCOMPARE(readU64Le(request, 40), std::uint64_t{20});
    QCOMPARE(readU32Le(request, 48), std::uint32_t{4096});

    smb::native_smb::ByteVector response;
    appendU32Le(response, 1);
    appendU32Le(response, 4096);
    appendU32Le(response, 4096);
    const auto decoded = smb::native_smb::decodeSrvCopyChunkResponse(response);

    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.chunksWritten, std::uint32_t{1});
    QCOMPARE(decoded.value.totalBytesWritten, std::uint32_t{4096});
  }

  void buildsSymbolicLinkReparseBuffer() {
    const auto bytes = smb::native_smb::buildSymbolicLinkReparseBuffer(
        "target.txt", "", true);

    QCOMPARE(readU32Le(bytes, 0), smb::native_smb::kIoReparseTagSymlink);
    QCOMPARE(readU16Le(bytes, 4), std::uint16_t{52});
    QCOMPARE(readU16Le(bytes, 8), std::uint16_t{0});
    QCOMPARE(readU16Le(bytes, 10), std::uint16_t{20});
    QCOMPARE(readU16Le(bytes, 12), std::uint16_t{20});
    QCOMPARE(readU16Le(bytes, 14), std::uint16_t{20});
    QCOMPARE(readU32Le(bytes, 16), smb::native_smb::kSymlinkFlagRelative);
    QCOMPARE(readU16Le(bytes, 20), std::uint16_t{'t'});
    QCOMPARE(readU16Le(bytes, 58), std::uint16_t{'t'});
  }

  void buildsSetInfoDispositionRequest() {
    smb::native_smb::SetInfoRequestOptions options;
    options.fileId.persistent = 0x0102030405060708ULL;
    options.fileId.volatileId = 0x1112131415161718ULL;
    options.infoType = smb::native_smb::kInfoTypeFile;
    options.fileInfoClass = smb::native_smb::kFileDispositionInformation;
    options.buffer = smb::native_smb::buildFileDispositionInformation(true);

    const auto bytes =
        smb::native_smb::buildSetInfoRequest(options, 25, 77, 34);

    QCOMPARE(bytes.size(), std::size_t{97});
    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kSetInfoRequestStructureSize);
    QCOMPARE(bytes[66], smb::native_smb::kInfoTypeFile);
    QCOMPARE(bytes[67], smb::native_smb::kFileDispositionInformation);
    QCOMPARE(readU32Le(bytes, 68), std::uint32_t{1});
    QCOMPARE(readU16Le(bytes, 72), std::uint16_t{96});
    QCOMPARE(readU16Le(bytes, 74), std::uint16_t{0});
    QCOMPARE(readU32Le(bytes, 76), std::uint32_t{0});
    QCOMPARE(readU64Le(bytes, 80), std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(readU64Le(bytes, 88), std::uint64_t{0x1112131415161718ULL});
    QCOMPARE(bytes[96], std::uint8_t{1});

    const auto header = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::SetInfo));
  }

  void buildsFileRenameInformationForSmb2() {
    const auto bytes =
        smb::native_smb::buildFileRenameInformation("renamed.txt", true);

    QCOMPARE(bytes[0], std::uint8_t{1});
    for (int i = 1; i < 8; ++i) {
      QCOMPARE(bytes[static_cast<std::size_t>(i)], std::uint8_t{0});
    }
    QCOMPARE(readU64Le(bytes, 8), std::uint64_t{0});
    QCOMPARE(readU32Le(bytes, 16), std::uint32_t{22});
    QCOMPARE(readU16Le(bytes, 20), std::uint16_t{'r'});
    QCOMPARE(readU16Le(bytes, 40), std::uint16_t{'t'});
  }

  void buildsFileLinkInformationForSmb2() {
    const auto bytes =
        smb::native_smb::buildFileLinkInformation("linked.txt", false);

    QCOMPARE(bytes[0], std::uint8_t{0});
    for (int i = 1; i < 8; ++i) {
      QCOMPARE(bytes[static_cast<std::size_t>(i)], std::uint8_t{0});
    }
    QCOMPARE(readU64Le(bytes, 8), std::uint64_t{0});
    QCOMPARE(readU32Le(bytes, 16), std::uint32_t{20});
    QCOMPARE(readU16Le(bytes, 20), std::uint16_t{'l'});
    QCOMPARE(readU16Le(bytes, 38), std::uint16_t{'t'});
  }

  void decodesSetInfoResponse() {
    const auto response =
        smb::native_smb::decodeSetInfoResponse(buildSetInfoResponse());

    QVERIFY(response.ok);
    QCOMPARE(response.value.status, smb::native_smb::kStatusSuccess);
  }

  void buildsQueryInfoRequest() {
    smb::native_smb::QueryInfoRequestOptions options;
    options.fileId.persistent = 0x0102030405060708ULL;
    options.fileId.volatileId = 0x1112131415161718ULL;
    options.infoType = smb::native_smb::kInfoTypeFile;
    options.fileInfoClass = smb::native_smb::kFileBasicInformation;
    options.outputBufferLength = 40;

    const auto bytes =
        smb::native_smb::buildQueryInfoRequest(options, 26, 77, 34);

    QCOMPARE(bytes.size(), std::size_t{104});
    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kQueryInfoRequestStructureSize);
    QCOMPARE(bytes[66], smb::native_smb::kInfoTypeFile);
    QCOMPARE(bytes[67], smb::native_smb::kFileBasicInformation);
    QCOMPARE(readU32Le(bytes, 68), std::uint32_t{40});
    QCOMPARE(readU16Le(bytes, 72), std::uint16_t{0});
    QCOMPARE(readU16Le(bytes, 74), std::uint16_t{0});
    QCOMPARE(readU32Le(bytes, 76), std::uint32_t{0});
    QCOMPARE(readU32Le(bytes, 80), std::uint32_t{0});
    QCOMPARE(readU32Le(bytes, 84), std::uint32_t{0});
    QCOMPARE(readU64Le(bytes, 88), std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(readU64Le(bytes, 96), std::uint64_t{0x1112131415161718ULL});

    const auto header = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::QueryInfo));
  }

  void decodesQueryInfoResponse() {
    const auto response = smb::native_smb::decodeQueryInfoResponse(
        buildQueryInfoResponse(fileBasicInformationBuffer()));

    QVERIFY(response.ok);
    QCOMPARE(response.value.outputBufferOffset, std::uint16_t{72});
    QCOMPARE(response.value.buffer.size(), std::size_t{40});
  }

  void decodesFileBasicInformation() {
    const auto basic =
        smb::native_smb::decodeFileBasicInformation(fileBasicInformationBuffer());

    QVERIFY(basic.ok);
    QCOMPARE(basic.value.creationTime, std::uint64_t{10});
    QCOMPARE(basic.value.lastAccessTime, std::uint64_t{20});
    QCOMPARE(basic.value.lastWriteTime, std::uint64_t{30});
    QCOMPARE(basic.value.changeTime, std::uint64_t{40});
    QCOMPARE(basic.value.fileAttributes,
             smb::native_smb::kFileAttributeReparsePoint);
  }

  void buildsFileBasicInformation() {
    smb::native_smb::FileBasicInformation info;
    info.creationTime = 11;
    info.lastAccessTime = 22;
    info.lastWriteTime = 33;
    info.changeTime = 44;
    info.fileAttributes = smb::native_smb::kFileAttributeNormal;

    const auto bytes = smb::native_smb::buildFileBasicInformation(info);

    QCOMPARE(bytes.size(), std::size_t{40});
    QCOMPARE(readU64Le(bytes, 0), std::uint64_t{11});
    QCOMPARE(readU64Le(bytes, 8), std::uint64_t{22});
    QCOMPARE(readU64Le(bytes, 16), std::uint64_t{33});
    QCOMPARE(readU64Le(bytes, 24), std::uint64_t{44});
    QCOMPARE(readU32Le(bytes, 32), smb::native_smb::kFileAttributeNormal);
  }

  void decodesFileStandardInformation() {
    const auto standard = smb::native_smb::decodeFileStandardInformation(
        fileStandardInformationBuffer());

    QVERIFY(standard.ok);
    QCOMPARE(standard.value.allocationSize, std::uint64_t{4096});
    QCOMPARE(standard.value.endOfFile, std::uint64_t{123});
    QCOMPARE(standard.value.numberOfLinks, std::uint32_t{2});
    QVERIFY(standard.value.deletePending);
    QVERIFY(!standard.value.directory);
  }

  void buildsAndDecodesFileFullEaInformation() {
    smb::native_smb::FileFullEaInformation first;
    first.name = "user.comment";
    first.value = {'o', 'k'};
    first.needEa = true;
    smb::native_smb::FileFullEaInformation second;
    second.name = "user.empty";

    const auto bytes =
        smb::native_smb::buildFileFullEaInformation({first, second});

    QCOMPARE(readU32Le(bytes, 0), std::uint32_t{24});
    QCOMPARE(bytes[4], smb::native_smb::kFileNeedEa);
    QCOMPARE(bytes[5], std::uint8_t{12});
    QCOMPARE(readU16Le(bytes, 6), std::uint16_t{2});
    QCOMPARE(readU32Le(bytes, 24), std::uint32_t{0});

    const auto decoded = smb::native_smb::decodeFileFullEaInformation(bytes);
    QVERIFY(decoded.ok);
    QCOMPARE(decoded.value.size(), std::size_t{2});
    QCOMPARE(decoded.value[0].name, std::string{"user.comment"});
    QCOMPARE(decoded.value[0].value, smb::native_smb::ByteVector({'o', 'k'}));
    QVERIFY(decoded.value[0].needEa);
    QCOMPARE(decoded.value[1].name, std::string{"user.empty"});
    QVERIFY(decoded.value[1].value.empty());
    QVERIFY(!decoded.value[1].needEa);
  }

  void buildsAndDecodesTreeDisconnect() {
    const auto request =
        smb::native_smb::buildTreeDisconnectRequest(21, 77, 34);

    QCOMPARE(readU16Le(request, 64),
             smb::native_smb::kTreeDisconnectRequestStructureSize);
    QCOMPARE(readU16Le(request, 66), std::uint16_t{0});
    const auto header = smb::native_smb::decodeSmb2SyncHeader(request);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::TreeDisconnect));
    QCOMPARE(header.value.messageId, std::uint64_t{21});
    QCOMPARE(header.value.treeId, std::uint32_t{77});
    QCOMPARE(header.value.sessionId, std::uint64_t{34});

    const auto response = smb::native_smb::decodeTreeDisconnectResponse(
        buildEmptyStructureResponse(
            smb::native_smb::Command::TreeDisconnect,
            smb::native_smb::kTreeDisconnectResponseStructureSize));

    QVERIFY(response.ok);
    QCOMPARE(response.value.status, smb::native_smb::kStatusSuccess);
  }

  void buildsAndDecodesLogoff() {
    const auto request = smb::native_smb::buildLogoffRequest(22, 34);

    QCOMPARE(readU16Le(request, 64),
             smb::native_smb::kLogoffRequestStructureSize);
    QCOMPARE(readU16Le(request, 66), std::uint16_t{0});
    const auto header = smb::native_smb::decodeSmb2SyncHeader(request);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::Logoff));
    QCOMPARE(header.value.messageId, std::uint64_t{22});
    QCOMPARE(header.value.sessionId, std::uint64_t{34});

    const auto response = smb::native_smb::decodeLogoffResponse(
        buildEmptyStructureResponse(smb::native_smb::Command::Logoff,
                                    smb::native_smb::kLogoffResponseStructureSize));

    QVERIFY(response.ok);
    QCOMPARE(response.value.status, smb::native_smb::kStatusSuccess);
  }

  void buildsQueryDirectoryRequest() {
    smb::native_smb::QueryDirectoryRequestOptions options;
    options.fileId.persistent = 0x0102030405060708ULL;
    options.fileId.volatileId = 0x1112131415161718ULL;
    options.pattern = "*";

    const auto bytes =
        smb::native_smb::buildQueryDirectoryRequest(options, 21, 77, 34);

    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kQueryDirectoryRequestStructureSize);
    QCOMPARE(bytes[66], smb::native_smb::kFileIdBothDirectoryInformation);
    QCOMPARE(bytes[67], smb::native_smb::kQueryDirectoryRestartScans);
    QCOMPARE(readU32Le(bytes, 68), std::uint32_t{0});
    QCOMPARE(readU64Le(bytes, 72), std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(readU64Le(bytes, 80), std::uint64_t{0x1112131415161718ULL});
    QCOMPARE(readU16Le(bytes, 88), std::uint16_t{96});
    QCOMPARE(readU16Le(bytes, 90), std::uint16_t{2});
    QCOMPARE(readU32Le(bytes, 92), std::uint32_t{65536});
    QCOMPARE(readU16Le(bytes, 96), std::uint16_t{'*'});

    const auto header = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::QueryDirectory));
  }

  void decodesQueryDirectoryResponse() {
    const auto response = smb::native_smb::decodeQueryDirectoryResponse(
        buildQueryDirectoryResponse());

    QVERIFY(response.ok);
    QCOMPARE(response.value.entries.size(), std::size_t{2});
    QCOMPARE(QString::fromStdString(response.value.entries[0].name),
             QStringLiteral("alpha.txt"));
    QCOMPARE(response.value.entries[0].endOfFile, std::uint64_t{123});
    QVERIFY(!response.value.entries[0].isDirectory);
    QCOMPARE(response.value.entries[0].fileId, std::uint64_t{1001});
    QCOMPARE(QString::fromStdString(response.value.entries[1].name),
             QStringLiteral("folder"));
    QVERIFY(response.value.entries[1].isDirectory);
    QCOMPARE(response.value.entries[1].fileId, std::uint64_t{1002});
  }

  void buildsChangeNotifyRequest() {
    smb::native_smb::ChangeNotifyRequestOptions options;
    options.fileId.persistent = 0x0102030405060708ULL;
    options.fileId.volatileId = 0x1112131415161718ULL;
    options.flags = smb::native_smb::kSmb2WatchTree;
    options.outputBufferLength = 131072;
    options.completionFilter =
        smb::native_smb::kFileNotifyChangeFileName |
        smb::native_smb::kFileNotifyChangeLastWrite;

    const auto bytes =
        smb::native_smb::buildChangeNotifyRequest(options, 27, 77, 34);

    QCOMPARE(bytes.size(), std::size_t{96});
    QCOMPARE(readU16Le(bytes, 64),
             smb::native_smb::kChangeNotifyRequestStructureSize);
    QCOMPARE(readU16Le(bytes, 66), smb::native_smb::kSmb2WatchTree);
    QCOMPARE(readU32Le(bytes, 68), std::uint32_t{131072});
    QCOMPARE(readU64Le(bytes, 72), std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(readU64Le(bytes, 80), std::uint64_t{0x1112131415161718ULL});
    QCOMPARE(readU32Le(bytes, 88),
             smb::native_smb::kFileNotifyChangeFileName |
                 smb::native_smb::kFileNotifyChangeLastWrite);
    QCOMPARE(readU32Le(bytes, 92), std::uint32_t{0});

    const auto header = smb::native_smb::decodeSmb2SyncHeader(bytes);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::ChangeNotify));
    QCOMPARE(header.value.creditCharge, std::uint16_t{2});
  }

  void decodesChangeNotifyResponse() {
    const auto response = smb::native_smb::decodeChangeNotifyResponse(
        buildChangeNotifyResponse());

    QVERIFY(response.ok);
    QCOMPARE(response.value.status, smb::native_smb::kStatusSuccess);
    QCOMPARE(response.value.outputBufferOffset, std::uint16_t{72});
    QCOMPARE(response.value.entries.size(), std::size_t{2});
    QCOMPARE(response.value.entries[0].action,
             smb::native_smb::kFileActionRenamedOldName);
    QCOMPARE(QString::fromStdString(response.value.entries[0].name),
             QStringLiteral("old.txt"));
    QCOMPARE(response.value.entries[1].action,
             smb::native_smb::kFileActionRenamedNewName);
    QCOMPARE(QString::fromStdString(response.value.entries[1].name),
             QStringLiteral("new.txt"));
  }

  void changeNotifyEnumDirIsSuccessfulEmptyResponse() {
    const auto response = smb::native_smb::decodeChangeNotifyResponse(
        buildChangeNotifyResponse(smb::native_smb::kStatusNotifyEnumDir));

    QVERIFY(response.ok);
    QCOMPARE(response.value.status, smb::native_smb::kStatusNotifyEnumDir);
    QCOMPARE(response.value.entries.size(), std::size_t{2});
  }

  void mapsSigningPolicyToSecurityMode() {
    QCOMPARE(smb::native_smb::securityModeForPolicy(
                 smb::native_smb::SecurityPolicy::Required),
             std::uint16_t{0x0003});
    QCOMPARE(smb::native_smb::securityModeForPolicy(
                 smb::native_smb::SecurityPolicy::Preferred),
             std::uint16_t{0x0001});
    QCOMPARE(smb::native_smb::securityModeForPolicy(
                 smb::native_smb::SecurityPolicy::Disabled),
             std::uint16_t{0x0000});
  }

  void encodesAndDecodesDirectTcpFrame() {
    smb::native_smb::ByteVector message(0x123, 0xAB);

    const auto frame = smb::native_smb::encodeDirectTcpFrame(message);

    QCOMPARE(frame.size(), message.size() + 4);
    QCOMPARE(frame[0], std::uint8_t{0});
    QCOMPARE(frame[1], std::uint8_t{0});
    QCOMPARE(frame[2], std::uint8_t{0x01});
    QCOMPARE(frame[3], std::uint8_t{0x23});
    QCOMPARE(frame[4], std::uint8_t{0xAB});

    const auto length = smb::native_smb::decodeDirectTcpPayloadLength(frame);
    QVERIFY(length.ok);
    QCOMPARE(length.value, std::uint32_t{0x123});
  }

  void rejectsInvalidDirectTcpFrameHeader() {
    smb::native_smb::ByteVector shortHeader{0, 1, 2};
    auto length = smb::native_smb::decodeDirectTcpPayloadLength(shortHeader);
    QVERIFY(!length.ok);
    QCOMPARE(static_cast<int>(length.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::IoError));

    smb::native_smb::ByteVector invalidMarker{1, 0, 0, 1};
    length = smb::native_smb::decodeDirectTcpPayloadLength(invalidMarker);
    QVERIFY(!length.ok);
    QCOMPARE(static_cast<int>(length.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }
};

QTEST_MAIN(NativeSmbProtocolTest)

#include "test_native_smb_protocol.moc"
