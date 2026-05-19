#include "NativeSmbSession.h"

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

smb::native_smb::ByteVector createResponsePayload(
    smb::native_smb::Command command = smb::native_smb::Command::Create,
    std::uint32_t attributes = smb::native_smb::kFileAttributeNormal) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
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
  appendU32Le(bytes, attributes);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
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

smb::native_smb::ByteVector directoryEntry() {
  const auto name = smb::native_smb::encodeUtf16Le("folder");
  smb::native_smb::ByteVector bytes;
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 11);
  appendU64Le(bytes, 12);
  appendU64Le(bytes, 13);
  appendU64Le(bytes, 14);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU32Le(bytes, smb::native_smb::kFileAttributeDirectory);
  appendU32Le(bytes, static_cast<std::uint32_t>(name.size()));
  appendU32Le(bytes, 0);
  bytes.push_back(0);
  bytes.push_back(0);
  appendZeros(bytes, 24);
  appendU16Le(bytes, 0);
  appendU64Le(bytes, 1002);
  bytes.insert(bytes.end(), name.begin(), name.end());
  return bytes;
}

smb::native_smb::ByteVector queryDirectoryResponsePayload() {
  const auto entry = directoryEntry();
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::QueryDirectory;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kQueryDirectoryResponseStructureSize);
  appendU16Le(bytes, 72);
  appendU32Le(bytes, static_cast<std::uint32_t>(entry.size()));
  bytes.insert(bytes.end(), entry.begin(), entry.end());
  return bytes;
}

smb::native_smb::ByteVector readResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Read;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kReadResponseStructureSize);
  bytes.push_back(80);
  bytes.push_back(0);
  appendU32Le(bytes, 4);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  bytes.push_back('d');
  bytes.push_back('a');
  bytes.push_back('t');
  bytes.push_back('a');
  return bytes;
}

smb::native_smb::ByteVector writeResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Write;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kWriteResponseStructureSize);
  appendU16Le(bytes, 0);
  appendU32Le(bytes, 4);
  appendU32Le(bytes, 0);
  appendU16Le(bytes, 0);
  appendU16Le(bytes, 0);
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
  bytes.push_back(0);
  bytes.push_back(1);
  appendU16Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector queryInfoResponsePayload(
    const smb::native_smb::ByteVector &buffer) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::QueryInfo;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kQueryInfoResponseStructureSize);
  appendU16Le(bytes, 72);
  appendU32Le(bytes, static_cast<std::uint32_t>(buffer.size()));
  bytes.insert(bytes.end(), buffer.begin(), buffer.end());
  return bytes;
}

smb::native_smb::ByteVector setInfoResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::SetInfo;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kSetInfoResponseStructureSize);
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

class NativeSmbSessionTest final : public QObject {
  Q_OBJECT

private slots:
  void routesFacadeOperationsAndAdvancesMessageIds() {
    ScriptedTransport transport({
        framedSuccess(createResponsePayload(
            smb::native_smb::Command::Create,
            smb::native_smb::kFileAttributeDirectory)),
        framedSuccess(queryDirectoryResponsePayload()),
        framedSuccess(closeResponsePayload()),
        framedSuccess(createResponsePayload()),
        framedSuccess(readResponsePayload()),
        framedSuccess(closeResponsePayload()),
        framedSuccess(createResponsePayload()),
        framedSuccess(writeResponsePayload()),
        framedSuccess(closeResponsePayload()),
        framedSuccess(createResponsePayload()),
        framedSuccess(setInfoResponsePayload()),
        framedSuccess(closeResponsePayload()),
    });

    smb::native_smb::NativeSmbSessionConfig config;
    config.treeId = 77;
    config.sessionId = 34;
    config.firstMessageId = 100;
    smb::native_smb::NativeSmbSession session(transport, config);

    const auto listing = session.listDirectory("docs", {});
    QVERIFY(listing.ok);
    QCOMPARE(listing.value.entries.size(), std::size_t{1});
    QCOMPARE(QString::fromStdString(listing.value.entries[0].name),
             QStringLiteral("folder"));
    QVERIFY(listing.value.entries[0].directory);

    const auto read = session.readFileOnce("docs/file.txt", 4, 0, {});
    QVERIFY(read.ok);
    QCOMPARE(read.value.data.size(), std::size_t{4});

    const auto write =
        session.writeFileOnce("docs/file.txt", {'d', 'a', 't', 'a'}, 0, {});
    QVERIFY(write.ok);
    QCOMPARE(write.value.bytesWritten, std::uint32_t{4});

    const auto removed = session.deleteObject("docs/file.txt", false, {});
    QVERIFY(removed.ok);
    QCOMPARE(QString::fromStdString(removed.value.path),
             QStringLiteral("docs/file.txt"));

    QCOMPARE(transport.requestFrames.size(), std::size_t{12});
    QCOMPARE(requestHeader(transport.requestFrames[0]).messageId,
             std::uint64_t{100});
    QCOMPARE(requestHeader(transport.requestFrames[1]).messageId,
             std::uint64_t{101});
    QCOMPARE(requestHeader(transport.requestFrames[2]).messageId,
             std::uint64_t{102});
    QCOMPARE(requestHeader(transport.requestFrames[3]).messageId,
             std::uint64_t{103});
    QCOMPARE(requestHeader(transport.requestFrames[6]).messageId,
             std::uint64_t{106});
    QCOMPARE(requestHeader(transport.requestFrames[9]).messageId,
             std::uint64_t{109});
    QCOMPARE(session.nextMessageIdForTests(), std::uint64_t{112});
  }

  void routesStatObject() {
    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(queryInfoResponsePayload(fileBasicInformationBuffer())),
        framedSuccess(queryInfoResponsePayload(fileStandardInformationBuffer())),
        framedSuccess(closeResponsePayload()),
    });

    smb::native_smb::NativeSmbSessionConfig config;
    config.treeId = 77;
    config.sessionId = 34;
    config.firstMessageId = 10;
    smb::native_smb::NativeSmbSession session(transport, config);

    const auto stat = session.statObject("docs/file.txt", {});

    QVERIFY(stat.ok);
    QCOMPARE(stat.value.size, std::uint64_t{123});
    QCOMPARE(stat.value.allocationSize, std::uint64_t{4096});
    QCOMPARE(stat.value.lastWriteTime, std::uint64_t{30});
    QCOMPARE(stat.value.numberOfLinks, std::uint32_t{2});
    QVERIFY(stat.value.directory);
    QVERIFY(stat.value.reparsePoint);
    QVERIFY(!stat.value.deletePending);
    QCOMPARE(transport.requestFrames.size(), std::size_t{4});
    QCOMPARE(static_cast<int>(requestHeader(transport.requestFrames[1]).command),
             static_cast<int>(smb::native_smb::Command::QueryInfo));
    QCOMPARE(requestHeader(transport.requestFrames[0]).messageId,
             std::uint64_t{10});
    QCOMPARE(requestHeader(transport.requestFrames[3]).messageId,
             std::uint64_t{13});
    QCOMPARE(session.nextMessageIdForTests(), std::uint64_t{14});
  }

  void routesCreateDirectoryAndRename() {
    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(closeResponsePayload()),
        framedSuccess(createResponsePayload()),
        framedSuccess(setInfoResponsePayload()),
        framedSuccess(closeResponsePayload()),
    });

    smb::native_smb::NativeSmbSessionConfig config;
    config.treeId = 77;
    config.sessionId = 34;
    config.firstMessageId = 50;
    smb::native_smb::NativeSmbSession session(transport, config);

    const auto created = session.createDirectory("new-folder", {});
    QVERIFY(created.ok);
    QCOMPARE(QString::fromStdString(created.value.path),
             QStringLiteral("new-folder"));

    const auto renamed =
        session.renameObject("old.txt", "new.txt", true, {});
    QVERIFY(renamed.ok);
    QCOMPARE(QString::fromStdString(renamed.value.path),
             QStringLiteral("new.txt"));

    QCOMPARE(transport.requestFrames.size(), std::size_t{5});
    QCOMPARE(static_cast<int>(requestHeader(transport.requestFrames[0]).command),
             static_cast<int>(smb::native_smb::Command::Create));
    QCOMPARE(static_cast<int>(requestHeader(transport.requestFrames[1]).command),
             static_cast<int>(smb::native_smb::Command::Close));
    QCOMPARE(static_cast<int>(requestHeader(transport.requestFrames[3]).command),
             static_cast<int>(smb::native_smb::Command::SetInfo));
    QCOMPARE(requestHeader(transport.requestFrames[0]).messageId,
             std::uint64_t{50});
    QCOMPARE(requestHeader(transport.requestFrames[2]).messageId,
             std::uint64_t{52});
    QCOMPARE(session.nextMessageIdForTests(), std::uint64_t{55});
  }

  void returnsErrorsWithoutLeakingTransportDetails() {
    ScriptedTransport transport(
        {framedSuccess(createResponsePayload(smb::native_smb::Command::Read))});

    smb::native_smb::NativeSmbSessionConfig config;
    config.treeId = 77;
    config.sessionId = 34;
    smb::native_smb::NativeSmbSession session(transport, config);

    const auto listing = session.listDirectory("docs", {});

    QVERIFY(!listing.ok);
    QCOMPARE(static_cast<int>(listing.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
    QCOMPARE(transport.requestFrames.size(), std::size_t{1});
    QCOMPARE(session.nextMessageIdForTests(), std::uint64_t{4});
  }
};

QTEST_MAIN(NativeSmbSessionTest)

#include "test_native_smb_session.moc"
