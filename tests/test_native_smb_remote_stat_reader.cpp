#include "RemoteStatReader.h"

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

std::uint16_t readU16Le(const smb::native_smb::ByteVector &bytes,
                        std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

smb::native_smb::ByteVector createResponsePayload(
    smb::native_smb::Command command = smb::native_smb::Command::Create) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 90;
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
  appendU32Le(bytes, smb::native_smb::kFileAttributeReparsePoint);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector basicInfoBuffer() {
  smb::native_smb::ByteVector bytes;
  appendU64Le(bytes, 10);
  appendU64Le(bytes, 20);
  appendU64Le(bytes, 30);
  appendU64Le(bytes, 40);
  appendU32Le(bytes, smb::native_smb::kFileAttributeReparsePoint);
  appendU32Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector standardInfoBuffer() {
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
    const smb::native_smb::ByteVector &buffer,
    smb::native_smb::Command command = smb::native_smb::Command::QueryInfo) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 91;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kQueryInfoResponseStructureSize);
  appendU16Le(bytes, 72);
  appendU32Le(bytes, static_cast<std::uint32_t>(buffer.size()));
  bytes.insert(bytes.end(), buffer.begin(), buffer.end());
  return bytes;
}

smb::native_smb::ByteVector closeResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Close;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 93;
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

smb::native_smb::Command requestCommand(
    const smb::native_smb::ByteVector &frame) {
  const auto payload = smb::native_smb::decodeDirectTcpPayload(frame);
  Q_ASSERT(payload.ok);
  const auto header = smb::native_smb::decodeSmb2SyncHeader(payload.value);
  Q_ASSERT(header.ok);
  return header.value.command;
}

smb::native_smb::ByteVector requestPayload(
    const smb::native_smb::ByteVector &frame) {
  const auto payload = smb::native_smb::decodeDirectTcpPayload(frame);
  Q_ASSERT(payload.ok);
  return payload.value;
}

} // namespace

class NativeSmbRemoteStatReaderTest final : public QObject {
  Q_OBJECT

private slots:
  void statsObjectWithCreateQueryInfoAndClose() {
    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(queryInfoResponsePayload(basicInfoBuffer())),
        framedSuccess(queryInfoResponsePayload(standardInfoBuffer())),
        framedSuccess(closeResponsePayload()),
    });

    const smb::native_smb::RemoteStatReader reader;
    const auto result = reader.stat(transport, "folder", 90, 77, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(result.value.creationTime, std::uint64_t{10});
    QCOMPARE(result.value.lastWriteTime, std::uint64_t{30});
    QCOMPARE(result.value.allocationSize, std::uint64_t{4096});
    QCOMPARE(result.value.endOfFile, std::uint64_t{123});
    QCOMPARE(result.value.numberOfLinks, std::uint32_t{2});
    QVERIFY(result.value.directory);
    QVERIFY(result.value.reparsePoint);
    QVERIFY(!result.value.deletePending);

    QCOMPARE(transport.requestFrames.size(), std::size_t{4});
    QCOMPARE(static_cast<int>(requestCommand(transport.requestFrames[0])),
             static_cast<int>(smb::native_smb::Command::Create));
    QCOMPARE(static_cast<int>(requestCommand(transport.requestFrames[1])),
             static_cast<int>(smb::native_smb::Command::QueryInfo));
    QCOMPARE(static_cast<int>(requestCommand(transport.requestFrames[2])),
             static_cast<int>(smb::native_smb::Command::QueryInfo));
    QCOMPARE(static_cast<int>(requestCommand(transport.requestFrames[3])),
             static_cast<int>(smb::native_smb::Command::Close));

    const auto basicQueryPayload = requestPayload(transport.requestFrames[1]);
    QCOMPARE(basicQueryPayload[67], smb::native_smb::kFileBasicInformation);
    const auto standardQueryPayload = requestPayload(transport.requestFrames[2]);
    QCOMPARE(standardQueryPayload[67],
             smb::native_smb::kFileStandardInformation);
  }

  void stopsWhenBasicInfoResponseIsInvalid() {
    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(queryInfoResponsePayload(
            basicInfoBuffer(), smb::native_smb::Command::SetInfo)),
        framedSuccess(queryInfoResponsePayload(standardInfoBuffer())),
        framedSuccess(closeResponsePayload()),
    });

    const smb::native_smb::RemoteStatReader reader;
    const auto result = reader.stat(transport, "folder", 90, 77, 34, {});

    QVERIFY(!result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{2});
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

    const smb::native_smb::RemoteStatReader reader;
    const auto result = reader.stat(transport, "folder", 90, 77, 34, context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{0});
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }
};

QTEST_MAIN(NativeSmbRemoteStatReaderTest)

#include "test_native_smb_remote_stat_reader.moc"
