#include "Protocol.h"

#include <QtTest/QtTest>

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

    QCOMPARE(bytes.size(), std::size_t{108});
    QCOMPARE(bytes[64], std::uint8_t{36});
    QCOMPARE(bytes[65], std::uint8_t{0});
    QCOMPARE(bytes[66], std::uint8_t{4});
    QCOMPARE(bytes[67], std::uint8_t{0});
    QCOMPARE(bytes[68], std::uint8_t{3});
    QCOMPARE(bytes[69], std::uint8_t{0});
    QCOMPARE(bytes[72], std::uint8_t{0x41});
    QCOMPARE(bytes[73], std::uint8_t{0});
    QCOMPARE(bytes[76], std::uint8_t{1});
    QCOMPARE(bytes[91], std::uint8_t{16});
    QCOMPARE(bytes[100], std::uint8_t{0x02});
    QCOMPARE(bytes[101], std::uint8_t{0x02});
    QCOMPARE(bytes[102], std::uint8_t{0x10});
    QCOMPARE(bytes[103], std::uint8_t{0x02});
    QCOMPARE(bytes[104], std::uint8_t{0x00});
    QCOMPARE(bytes[105], std::uint8_t{0x03});
    QCOMPARE(bytes[106], std::uint8_t{0x02});
    QCOMPARE(bytes[107], std::uint8_t{0x03});

    const auto defaultDialects = smb::native_smb::defaultInitialDialects();
    QVERIFY(!smb::native_smb::containsSmb1Dialect(defaultDialects));
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
