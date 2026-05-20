#include "Dcerpc.h"
#include "SrvsRpc.h"

#include <QtTest/QtTest>

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

void appendCommonHeader(smb::native_smb::ByteVector &bytes,
                        std::uint8_t packetType, std::uint16_t fragLength,
                        std::uint32_t callId) {
  bytes.push_back(smb::native_smb::kDcerpcVersion);
  bytes.push_back(0);
  bytes.push_back(packetType);
  bytes.push_back(smb::native_smb::kDcerpcFlagFirstFragment |
                  smb::native_smb::kDcerpcFlagLastFragment);
  bytes.push_back(0x10);
  bytes.push_back(0);
  bytes.push_back(0);
  bytes.push_back(0);
  appendU16Le(bytes, fragLength);
  appendU16Le(bytes, 0);
  appendU32Le(bytes, callId);
}

smb::native_smb::ByteVector bindAckFixture(std::uint16_t result = 0) {
  smb::native_smb::ByteVector bytes;
  appendCommonHeader(bytes, smb::native_smb::kDcerpcPacketTypeBindAck, 56, 3);
  appendU16Le(bytes, 4280);
  appendU16Le(bytes, 4280);
  appendU32Le(bytes, 99);
  appendU16Le(bytes, 0);
  appendU16Le(bytes, 0);
  bytes.push_back(1);
  bytes.push_back(0);
  appendU16Le(bytes, 0);
  appendU16Le(bytes, result);
  appendU16Le(bytes, 0);
  const auto &syntax = smb::native_smb::ndr32TransferSyntax();
  bytes.insert(bytes.end(), syntax.uuid.begin(), syntax.uuid.end());
  appendU16Le(bytes, syntax.majorVersion);
  appendU16Le(bytes, syntax.minorVersion);
  return bytes;
}

smb::native_smb::ByteVector responseFixture() {
  const smb::native_smb::ByteVector stub{'o', 'k', '\0'};
  smb::native_smb::ByteVector bytes;
  appendCommonHeader(
      bytes, smb::native_smb::kDcerpcPacketTypeResponse,
      static_cast<std::uint16_t>(16 + 8 + stub.size()), 4);
  appendU32Le(bytes, static_cast<std::uint32_t>(stub.size()));
  appendU16Le(bytes, 2);
  bytes.push_back(0);
  bytes.push_back(0);
  bytes.insert(bytes.end(), stub.begin(), stub.end());
  return bytes;
}

} // namespace

class NativeSmbDcerpcTest final : public QObject {
  Q_OBJECT

private slots:
  void buildsBindPduForSrvsInterface() {
    const auto bytes =
        smb::native_smb::buildDcerpcBindPdu(smb::native_smb::srvsRpcSyntax(),
                                            3, 2, 1024, 2048);

    QCOMPARE(bytes.size(), std::size_t{72});
    QCOMPARE(bytes[0], smb::native_smb::kDcerpcVersion);
    QCOMPARE(bytes[2], smb::native_smb::kDcerpcPacketTypeBind);
    QCOMPARE(readU16Le(bytes, 8), 72U);
    QCOMPARE(readU32Le(bytes, 12), 3U);
    QCOMPARE(readU16Le(bytes, 16), 1024U);
    QCOMPARE(readU16Le(bytes, 18), 2048U);
    QCOMPARE(bytes[24], 1U);
    QCOMPARE(readU16Le(bytes, 28), 2U);
    QCOMPARE(bytes[30], 1U);
  }

  void decodesAcceptedBindAck() {
    const auto decoded =
        smb::native_smb::decodeDcerpcBindAck(bindAckFixture());

    QVERIFY2(decoded.ok, decoded.error.message.c_str());
    QVERIFY(decoded.value.accepted);
    QCOMPARE(decoded.value.maxTransmitFrag, 4280U);
    QCOMPARE(decoded.value.maxReceiveFrag, 4280U);
    QCOMPARE(decoded.value.associationGroupId, 99U);
    QCOMPARE(decoded.value.transferSyntax.majorVersion, 2U);
  }

  void rejectsBindAckPresentationFailure() {
    const auto decoded =
        smb::native_smb::decodeDcerpcBindAck(bindAckFixture(2));

    QVERIFY(!decoded.ok);
    QCOMPARE(static_cast<int>(decoded.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::ProtocolUnsupported));
  }

  void buildsRequestPduWithSrvsOpnumAndStub() {
    const smb::native_smb::ByteVector stub{'s', 't', 'u', 'b'};
    const auto bytes = smb::native_smb::buildDcerpcRequestPdu(
        smb::native_smb::kSrvsNetrShareEnumOpnum, stub, 4, 2);

    QCOMPARE(bytes[2], smb::native_smb::kDcerpcPacketTypeRequest);
    QCOMPARE(readU16Le(bytes, 8), 28U);
    QCOMPARE(readU32Le(bytes, 16), 4U);
    QCOMPARE(readU16Le(bytes, 20), 2U);
    QCOMPARE(readU16Le(bytes, 22),
             smb::native_smb::kSrvsNetrShareEnumOpnum);
    QCOMPARE(bytes[24], static_cast<std::uint8_t>('s'));
  }

  void decodesResponseStub() {
    const auto decoded = smb::native_smb::decodeDcerpcResponse(responseFixture());

    QVERIFY2(decoded.ok, decoded.error.message.c_str());
    QCOMPARE(decoded.value.allocationHint, 3U);
    QCOMPARE(decoded.value.contextId, 2U);
    QCOMPARE(decoded.value.stubData.size(), std::size_t{3});
    QCOMPARE(decoded.value.stubData[0], static_cast<std::uint8_t>('o'));
  }
};

QTEST_MAIN(NativeSmbDcerpcTest)

#include "test_native_smb_dcerpc.moc"
