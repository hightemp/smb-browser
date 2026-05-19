#include "DirectoryLister.h"

#include <QtTest/QtTest>

#include <cstdint>
#include <deque>

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

void appendZeros(smb::native_smb::ByteVector &bytes, std::size_t count) {
  bytes.insert(bytes.end(), count, 0);
}

smb::native_smb::ByteVector createResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Create;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 30;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kCreateResponseStructureSize);
  bytes.push_back(0);
  bytes.push_back(0);
  appendU32Le(bytes, 1);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU32Le(bytes, smb::native_smb::kFileAttributeDirectory);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector fileIdBothEntry(const std::string &name,
                                            std::uint32_t attributes,
                                            std::uint64_t fileId) {
  const auto encodedName = smb::native_smb::encodeUtf16Le(name);
  smb::native_smb::ByteVector bytes;
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 1);
  appendU64Le(bytes, 2);
  appendU64Le(bytes, 3);
  appendU64Le(bytes, 4);
  appendU64Le(bytes, 5);
  appendU64Le(bytes, 6);
  appendU32Le(bytes, attributes);
  appendU32Le(bytes, static_cast<std::uint32_t>(encodedName.size()));
  appendU32Le(bytes, 0);
  bytes.push_back(0);
  bytes.push_back(0);
  appendZeros(bytes, 24);
  appendU16Le(bytes, 0);
  appendU64Le(bytes, fileId);
  bytes.insert(bytes.end(), encodedName.begin(), encodedName.end());
  return bytes;
}

smb::native_smb::ByteVector queryDirectoryResponsePayload() {
  auto entry = fileIdBothEntry("report.xlsx", 0, 2001);

  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::QueryDirectory;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 31;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kQueryDirectoryResponseStructureSize);
  appendU16Le(bytes, 72);
  appendU32Le(bytes, static_cast<std::uint32_t>(entry.size()));
  bytes.insert(bytes.end(), entry.begin(), entry.end());
  return bytes;
}

smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
framedSuccess(const smb::native_smb::ByteVector &payload) {
  return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
      smb::native_smb::encodeDirectTcpFrame(payload));
}

} // namespace

class NativeSmbDirectoryListerTest final : public QObject {
  Q_OBJECT

private slots:
  void listsDirectoryOverScriptedTransport() {
    ScriptedTransport transport({framedSuccess(createResponsePayload()),
                                 framedSuccess(queryDirectoryResponsePayload())});

    const smb::native_smb::DirectoryLister lister;
    const auto result = lister.list(transport, "folder", 30, 77, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{2});
    QCOMPARE(result.value.directoryFileId.persistent,
             std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(result.value.directoryFileId.volatileId,
             std::uint64_t{0x1112131415161718ULL});
    QCOMPARE(result.value.entries.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(result.value.entries[0].name),
             QStringLiteral("report.xlsx"));
    QCOMPARE(result.value.entries[0].fileId, std::uint64_t{2001});

    const auto createPayload =
        smb::native_smb::decodeDirectTcpPayload(transport.requestFrames[0]);
    QVERIFY(createPayload.ok);
    const auto createHeader =
        smb::native_smb::decodeSmb2SyncHeader(createPayload.value);
    QVERIFY(createHeader.ok);
    QCOMPARE(static_cast<int>(createHeader.value.command),
             static_cast<int>(smb::native_smb::Command::Create));

    const auto queryPayload =
        smb::native_smb::decodeDirectTcpPayload(transport.requestFrames[1]);
    QVERIFY(queryPayload.ok);
    const auto queryHeader =
        smb::native_smb::decodeSmb2SyncHeader(queryPayload.value);
    QVERIFY(queryHeader.ok);
    QCOMPARE(static_cast<int>(queryHeader.value.command),
             static_cast<int>(smb::native_smb::Command::QueryDirectory));
  }

  void stopsWhenCreateResponseIsInvalid() {
    auto badCreate = createResponsePayload();
    badCreate[12] = static_cast<std::uint8_t>(
        static_cast<std::uint16_t>(smb::native_smb::Command::Read) & 0xFF);

    ScriptedTransport transport({framedSuccess(badCreate),
                                 framedSuccess(queryDirectoryResponsePayload())});

    const smb::native_smb::DirectoryLister lister;
    const auto result = lister.list(transport, "folder", 30, 77, 34, {});

    QVERIFY(!result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{1});
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }

  void returnsCancelledBeforeTransportCall() {
    ScriptedTransport transport({framedSuccess(createResponsePayload())});
    smb::native_smb::CancellationToken token;
    token.cancel();
    smb::native_smb::OperationContext context;
    context.cancellationToken = &token;

    const smb::native_smb::DirectoryLister lister;
    const auto result = lister.list(transport, "folder", 30, 77, 34, context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{0});
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }
};

QTEST_MAIN(NativeSmbDirectoryListerTest)

#include "test_native_smb_directory_lister.moc"
