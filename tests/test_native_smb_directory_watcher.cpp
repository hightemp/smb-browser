#include "DirectoryWatcher.h"

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

smb::native_smb::ByteVector createResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Create;
  header.flags = smb::native_smb::kFlagServerToRedir;
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

smb::native_smb::ByteVector notifyResponsePayload(
    std::uint32_t status = smb::native_smb::kStatusSuccess) {
  const auto name = smb::native_smb::encodeUtf16Le("changed.txt");
  smb::native_smb::ByteVector entry;
  appendU32Le(entry, 0);
  appendU32Le(entry, smb::native_smb::kFileActionModified);
  appendU32Le(entry, static_cast<std::uint32_t>(name.size()));
  entry.insert(entry.end(), name.begin(), name.end());

  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::ChangeNotify;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.status = status;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kChangeNotifyResponseStructureSize);
  appendU16Le(bytes, 72);
  appendU32Le(bytes, static_cast<std::uint32_t>(entry.size()));
  bytes.insert(bytes.end(), entry.begin(), entry.end());
  return bytes;
}

smb::native_smb::ByteVector closeResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Close;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kCloseResponseStructureSize);
  appendU16Le(bytes, 0);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU32Le(bytes, 0);
  return bytes;
}

smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
framedSuccess(const smb::native_smb::ByteVector &payload) {
  return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
      smb::native_smb::encodeDirectTcpFrame(payload));
}

smb::native_smb::Smb2SyncHeader requestHeader(
    const smb::native_smb::ByteVector &frame) {
  const auto payload = smb::native_smb::decodeDirectTcpPayload(frame);
  Q_ASSERT(payload.ok);
  const auto header = smb::native_smb::decodeSmb2SyncHeader(payload.value);
  Q_ASSERT(header.ok);
  return header.value;
}

} // namespace

class NativeSmbDirectoryWatcherTest final : public QObject {
  Q_OBJECT

private slots:
  void waitsOnceAndClosesDirectoryHandle() {
    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(notifyResponsePayload()),
        framedSuccess(closeResponsePayload()),
    });

    const smb::native_smb::DirectoryWatcher watcher;
    const auto result = watcher.waitOnce(
        transport, "docs", smb::native_smb::kFileNotifyChangeDefault, true,
        65536, 80, 77, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(result.value.status, smb::native_smb::kStatusSuccess);
    QCOMPARE(result.value.entries.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(result.value.entries[0].name),
             QStringLiteral("changed.txt"));
    QCOMPARE(transport.requestFrames.size(), std::size_t{3});
    QCOMPARE(static_cast<int>(requestHeader(transport.requestFrames[0]).command),
             static_cast<int>(smb::native_smb::Command::Create));
    QCOMPARE(static_cast<int>(requestHeader(transport.requestFrames[1]).command),
             static_cast<int>(smb::native_smb::Command::ChangeNotify));
    QCOMPARE(static_cast<int>(requestHeader(transport.requestFrames[2]).command),
             static_cast<int>(smb::native_smb::Command::Close));
    QCOMPARE(requestHeader(transport.requestFrames[0]).messageId,
             std::uint64_t{80});
    QCOMPARE(requestHeader(transport.requestFrames[2]).messageId,
             std::uint64_t{82});
  }

  void returnsCancelledBeforeNetworkIo() {
    ScriptedTransport transport({});
    smb::native_smb::CancellationToken token;
    token.cancel();
    smb::native_smb::OperationContext context;
    context.cancellationToken = &token;

    const smb::native_smb::DirectoryWatcher watcher;
    const auto result = watcher.waitOnce(
        transport, "docs", smb::native_smb::kFileNotifyChangeDefault, false,
        65536, 80, 77, 34, context);

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
    QCOMPARE(transport.requestFrames.size(), std::size_t{0});
  }

  void preservesEnumerationRequiredStatus() {
    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(notifyResponsePayload(
            smb::native_smb::kStatusNotifyEnumDir)),
        framedSuccess(closeResponsePayload()),
    });

    const smb::native_smb::DirectoryWatcher watcher;
    const auto result = watcher.waitOnce(
        transport, "docs", smb::native_smb::kFileNotifyChangeDefault, false,
        65536, 80, 77, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(result.value.status, smb::native_smb::kStatusNotifyEnumDir);
  }
};

QTEST_MAIN(NativeSmbDirectoryWatcherTest)

#include "test_native_smb_directory_watcher.moc"
