#include "DfsReferral.h"

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

void writeU16Le(smb::native_smb::ByteVector &bytes, std::size_t offset,
                std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void writeU32Le(smb::native_smb::ByteVector &bytes, std::size_t offset,
                std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes[offset++] = static_cast<std::uint8_t>((value >> shift) & 0xFF);
  }
}

void appendUtf16Null(smb::native_smb::ByteVector &bytes,
                     std::string_view text) {
  const auto encoded = smb::native_smb::encodeUtf16Le(text);
  bytes.insert(bytes.end(), encoded.begin(), encoded.end());
  appendU16Le(bytes, 0);
}

std::uint16_t checkedOffset(std::size_t from, std::size_t to) {
  return static_cast<std::uint16_t>(to - from);
}

smb::native_smb::ByteVector referralResponseFixture() {
  const auto dfsPath = std::string{"\\\\dfs.example.com\\root\\Finance"};
  const auto target1 = std::string{"\\\\fs01.example.com\\Finance"};
  const auto target2 = std::string{"\\\\fs02.example.com\\Finance"};

  smb::native_smb::ByteVector bytes;
  appendU16Le(bytes, static_cast<std::uint16_t>(dfsPath.size() * 2));
  appendU16Le(bytes, 2);
  appendU32Le(bytes, smb::native_smb::kDfsReferralHeaderStorageServers);

  const auto entry1 = bytes.size();
  bytes.resize(bytes.size() + 34);
  const auto entry2 = bytes.size();
  bytes.resize(bytes.size() + 34);
  const auto dfsPathOffset = bytes.size();
  appendUtf16Null(bytes, dfsPath);
  const auto target1Offset = bytes.size();
  appendUtf16Null(bytes, target1);
  const auto target2Offset = bytes.size();
  appendUtf16Null(bytes, target2);

  auto writeEntry = [&](std::size_t entryOffset, std::size_t targetOffset) {
    writeU16Le(bytes, entryOffset, smb::native_smb::kDfsReferralVersion3);
    writeU16Le(bytes, entryOffset + 2, 34);
    writeU16Le(bytes, entryOffset + 4, 0);
    writeU16Le(bytes, entryOffset + 6, 0);
    writeU32Le(bytes, entryOffset + 8, 300);
    const auto dfsOffset = checkedOffset(entryOffset, dfsPathOffset);
    const auto target = checkedOffset(entryOffset, targetOffset);
    writeU16Le(bytes, entryOffset + 12, dfsOffset);
    writeU16Le(bytes, entryOffset + 14, dfsOffset);
    writeU16Le(bytes, entryOffset + 16, target);
  };

  writeEntry(entry1, target1Offset);
  writeEntry(entry2, target2Offset);
  return bytes;
}

} // namespace

class NativeSmbDfsReferralTest final : public QObject {
  Q_OBJECT

private slots:
  void buildsDfsReferralRequest() {
    const auto bytes = smb::native_smb::buildDfsGetReferralRequest(
        "\\\\dfs.example.com\\root\\Finance", 3);

    QCOMPARE(readU16Le(bytes, 0), 3U);
    QVERIFY(bytes.size() > 4);
    QCOMPARE(readU16Le(bytes, bytes.size() - 2), 0U);
  }

  void decodesVersionThreeReferralTargets() {
    const auto decoded =
        smb::native_smb::decodeDfsReferralResponse(referralResponseFixture());

    QVERIFY2(decoded.ok, decoded.error.message.c_str());
    QCOMPARE(decoded.value.entries.size(), std::size_t{2});
    QCOMPARE(decoded.value.pathConsumedBytes,
             static_cast<std::uint16_t>(
                 std::string{"\\\\dfs.example.com\\root\\Finance"}.size() *
                 2));
    QCOMPARE(decoded.value.headerFlags,
             smb::native_smb::kDfsReferralHeaderStorageServers);
    QCOMPARE(QString::fromStdString(decoded.value.entries[0].dfsPath),
             QStringLiteral("\\\\dfs.example.com\\root\\Finance"));
    QCOMPARE(QString::fromStdString(decoded.value.entries[0].networkAddress),
             QStringLiteral("\\\\fs01.example.com\\Finance"));
    QCOMPARE(QString::fromStdString(decoded.value.entries[1].networkAddress),
             QStringLiteral("\\\\fs02.example.com\\Finance"));
    QCOMPARE(decoded.value.entries[0].timeToLiveSeconds, 300U);
    QVERIFY(!decoded.value.entries[0].nameListReferral);
  }

  void rejectsUnsupportedReferralVersion() {
    auto bytes = referralResponseFixture();
    bytes[8] = 9;

    const auto decoded = smb::native_smb::decodeDfsReferralResponse(bytes);

    QVERIFY(!decoded.ok);
    QCOMPARE(static_cast<int>(decoded.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::ProtocolUnsupported));
  }
};

QTEST_MAIN(NativeSmbDfsReferralTest)

#include "test_native_smb_dfs_referral.moc"
