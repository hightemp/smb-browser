#include "RemoteDfsReferralFetcher.h"

#include <QtTest/QtTest>

#include <deque>
#include <limits>

namespace {

class ScriptedTransport final : public smb::native_smb::Transport {
public:
  explicit ScriptedTransport(
      std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>
          responses)
      : m_responses(std::move(responses)) {}

  smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
  exchange(const smb::native_smb::ByteVector &requestFrame,
           const smb::native_smb::OperationContext &) override {
    requestFrames.push_back(requestFrame);
    if (m_responses.empty()) {
      return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::
          failure(smb::native_smb::ErrorCode::IoError,
                  "No scripted transport response.");
    }
    auto response = m_responses.front();
    m_responses.pop_front();
    return response;
  }

  std::vector<smb::native_smb::ByteVector> requestFrames;

private:
  std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>
      m_responses;
};

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

smb::native_smb::ByteVector referralResponseFixture() {
  const auto dfsPath = std::string{"\\\\dfs.example.com\\root\\Finance"};
  const auto target = std::string{"\\\\fs01.example.com\\Finance"};

  smb::native_smb::ByteVector bytes;
  appendU16Le(bytes, static_cast<std::uint16_t>(dfsPath.size() * 2));
  appendU16Le(bytes, 1);
  appendU32Le(bytes, smb::native_smb::kDfsReferralHeaderStorageServers);

  const auto entry = bytes.size();
  bytes.resize(bytes.size() + 34);
  const auto dfsPathOffset = bytes.size();
  appendUtf16Null(bytes, dfsPath);
  const auto targetOffset = bytes.size();
  appendUtf16Null(bytes, target);

  writeU16Le(bytes, entry, smb::native_smb::kDfsReferralVersion3);
  writeU16Le(bytes, entry + 2, 34);
  writeU16Le(bytes, entry + 4, 0);
  writeU16Le(bytes, entry + 6, 0);
  writeU32Le(bytes, entry + 8, 300);
  const auto dfsOffset = static_cast<std::uint16_t>(dfsPathOffset - entry);
  const auto networkOffset = static_cast<std::uint16_t>(targetOffset - entry);
  writeU16Le(bytes, entry + 12, dfsOffset);
  writeU16Le(bytes, entry + 14, dfsOffset);
  writeU16Le(bytes, entry + 16, networkOffset);
  return bytes;
}

smb::native_smb::ByteVector ioctlResponsePayload(
    const smb::native_smb::ByteVector &output) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Ioctl;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 10;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kIoctlResponseStructureSize);
  appendU16Le(bytes, 0);
  appendU32Le(bytes, smb::native_smb::kFsctlDfsGetReferrals);
  appendU64Le(bytes, std::numeric_limits<std::uint64_t>::max());
  appendU64Le(bytes, std::numeric_limits<std::uint64_t>::max());
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 112);
  appendU32Le(bytes, static_cast<std::uint32_t>(output.size()));
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  bytes.insert(bytes.end(), output.begin(), output.end());
  return bytes;
}

smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
framedSuccess(const smb::native_smb::ByteVector &payload) {
  return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
      smb::native_smb::encodeDirectTcpFrame(payload));
}

} // namespace

class NativeSmbRemoteDfsReferralFetcherTest final : public QObject {
  Q_OBJECT

private slots:
  void requestsDfsReferralsWithAllOnesFileId() {
    ScriptedTransport transport(
        {framedSuccess(ioctlResponsePayload(referralResponseFixture()))});

    const smb::native_smb::RemoteDfsReferralFetcher fetcher;
    const auto result = fetcher.getReferrals(
        transport, "\\\\dfs.example.com\\root\\Finance", 10, 77, 34, {});

    QVERIFY2(result.ok, result.error.message.c_str());
    QCOMPARE(result.value.messagesUsed, 1ULL);
    QCOMPARE(result.value.response.entries.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(
                 result.value.response.entries[0].networkAddress),
             QStringLiteral("\\\\fs01.example.com\\Finance"));

    QCOMPARE(transport.requestFrames.size(), std::size_t{1});
    const auto payload =
        smb::native_smb::decodeDirectTcpPayload(transport.requestFrames[0]);
    QVERIFY(payload.ok);
    const auto header = smb::native_smb::decodeSmb2SyncHeader(payload.value);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::Ioctl));
    QCOMPARE(readU32Le(payload.value, 68),
             smb::native_smb::kFsctlDfsGetReferrals);
    QCOMPARE(readU64Le(payload.value, 72),
             std::numeric_limits<std::uint64_t>::max());
    QCOMPARE(readU64Le(payload.value, 80),
             std::numeric_limits<std::uint64_t>::max());
    QCOMPARE(readU32Le(payload.value, 112), smb::native_smb::kIoctlIsFsctl);
  }
};

QTEST_MAIN(NativeSmbRemoteDfsReferralFetcherTest)

#include "test_native_smb_remote_dfs_referral_fetcher.moc"
