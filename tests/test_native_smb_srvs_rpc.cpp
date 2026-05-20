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

std::uint32_t readU32Le(const smb::native_smb::ByteVector &bytes,
                        std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void align4(smb::native_smb::ByteVector &bytes) {
  while ((bytes.size() % 4) != 0) {
    bytes.push_back(0);
  }
}

void appendNdrUtf16String(smb::native_smb::ByteVector &bytes,
                          std::string_view text) {
  const auto utf16 = smb::native_smb::encodeUtf16Le(text);
  const auto characters = static_cast<std::uint32_t>(utf16.size() / 2 + 1);
  appendU32Le(bytes, characters);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, characters);
  bytes.insert(bytes.end(), utf16.begin(), utf16.end());
  appendU16Le(bytes, 0);
  align4(bytes);
}

smb::native_smb::ByteVector shareEnumResponseFixture() {
  smb::native_smb::ByteVector bytes;
  appendU32Le(bytes, 1);
  appendU32Le(bytes, 1);
  appendU32Le(bytes, 0x00020000);
  appendU32Le(bytes, 2);
  appendU32Le(bytes, 0x00020004);
  appendU32Le(bytes, 2);

  appendU32Le(bytes, 0x00020008);
  appendU32Le(bytes, smb::native_smb::kShareTypeDisk);
  appendU32Le(bytes, 0x0002000C);

  appendU32Le(bytes, 0x00020010);
  appendU32Le(bytes,
              smb::native_smb::kShareTypeIpc |
                  smb::native_smb::kShareTypeSpecial);
  appendU32Le(bytes, 0);

  appendNdrUtf16String(bytes, "public");
  appendNdrUtf16String(bytes, "Public files");
  appendNdrUtf16String(bytes, "IPC$");

  appendU32Le(bytes, 2);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, smb::native_smb::kNetApiStatusSuccess);
  return bytes;
}

} // namespace

class NativeSmbSrvsRpcTest final : public QObject {
  Q_OBJECT

private slots:
  void buildsLevelOneShareEnumRequestStub() {
    const auto bytes =
        smb::native_smb::buildNetrShareEnumRequestStub("server", 4096, 7);

    QCOMPARE(readU32Le(bytes, 0), 0x00020000U);
    QCOMPARE(readU32Le(bytes, 4), 9U);
    QCOMPARE(readU32Le(bytes, 8), 0U);
    QCOMPARE(readU32Le(bytes, 12), 9U);

    const auto levelOffset = 36U;
    QCOMPARE(readU32Le(bytes, levelOffset), 1U);
    QCOMPARE(readU32Le(bytes, levelOffset + 4), 1U);
    QCOMPARE(readU32Le(bytes, levelOffset + 8), 0x00020004U);
    QCOMPARE(readU32Le(bytes, levelOffset + 12), 0U);
    QCOMPARE(readU32Le(bytes, levelOffset + 16), 0U);
    QCOMPARE(readU32Le(bytes, levelOffset + 20), 4096U);
    QCOMPARE(readU32Le(bytes, levelOffset + 24), 0x00020008U);
    QCOMPARE(readU32Le(bytes, levelOffset + 28), 7U);
  }

  void decodesLevelOneShareEnumResponseStub() {
    const auto decoded = smb::native_smb::decodeNetrShareEnumResponseStub(
        shareEnumResponseFixture());

    QVERIFY2(decoded.ok, decoded.error.message.c_str());
    QCOMPARE(static_cast<int>(decoded.value.shares.size()), 2);
    QCOMPARE(QString::fromStdString(decoded.value.shares[0].name),
             QStringLiteral("public"));
    QCOMPARE(static_cast<int>(decoded.value.shares[0].kind),
             static_cast<int>(smb::native_smb::SrvsShareKind::Disk));
    QVERIFY(!decoded.value.shares[0].hidden);
    QCOMPARE(QString::fromStdString(decoded.value.shares[0].comment),
             QStringLiteral("Public files"));

    QCOMPARE(QString::fromStdString(decoded.value.shares[1].name),
             QStringLiteral("IPC$"));
    QCOMPARE(static_cast<int>(decoded.value.shares[1].kind),
             static_cast<int>(smb::native_smb::SrvsShareKind::Ipc));
    QVERIFY(decoded.value.shares[1].hidden);
    QVERIFY(decoded.value.shares[1].special);
    QCOMPARE(decoded.value.totalEntries, 2U);
    QVERIFY(!decoded.value.resumeHandle.has_value());
    QCOMPARE(decoded.value.apiStatus, smb::native_smb::kNetApiStatusSuccess);
  }

  void decodesMoreDataStatusWithResumeHandle() {
    auto bytes = shareEnumResponseFixture();
    bytes.resize(bytes.size() - 8);
    appendU32Le(bytes, 0x00020010);
    appendU32Le(bytes, 12);
    appendU32Le(bytes, smb::native_smb::kNetApiStatusMoreData);

    const auto decoded =
        smb::native_smb::decodeNetrShareEnumResponseStub(bytes);

    QVERIFY2(decoded.ok, decoded.error.message.c_str());
    QVERIFY(decoded.value.moreData);
    QVERIFY(decoded.value.resumeHandle.has_value());
    QCOMPARE(decoded.value.resumeHandle.value(), 12U);
  }

  void rejectsUnsupportedInfoLevel() {
    auto bytes = shareEnumResponseFixture();
    bytes[0] = 2;

    const auto decoded =
        smb::native_smb::decodeNetrShareEnumResponseStub(bytes);

    QVERIFY(!decoded.ok);
  }
};

QTEST_MAIN(NativeSmbSrvsRpcTest)

#include "test_native_smb_srvs_rpc.moc"
