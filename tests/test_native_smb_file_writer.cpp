#include "FileWriter.h"

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
  header.messageId = 80;
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
  appendU32Le(bytes, smb::native_smb::kFileAttributeNormal);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector writeResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Write;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 81;
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

smb::native_smb::ByteVector closeResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Close;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 82;
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

} // namespace

class NativeSmbFileWriterTest final : public QObject {
  Q_OBJECT

private slots:
  void writesFileOverScriptedTransport() {
    ScriptedTransport transport({framedSuccess(createResponsePayload()),
                                 framedSuccess(writeResponsePayload()),
                                 framedSuccess(closeResponsePayload())});

    const smb::native_smb::FileWriter writer;
    const auto result =
        writer.writeOnce(transport, "report.txt", {'t', 'e', 'x', 't'}, 0, 80,
                         77, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{3});
    QCOMPARE(result.value.fileId.persistent,
             std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(result.value.bytesWritten, std::uint32_t{4});

    const auto createPayload =
        smb::native_smb::decodeDirectTcpPayload(transport.requestFrames[0]);
    QVERIFY(createPayload.ok);
    const auto createHeader =
        smb::native_smb::decodeSmb2SyncHeader(createPayload.value);
    QVERIFY(createHeader.ok);
    QCOMPARE(static_cast<int>(createHeader.value.command),
             static_cast<int>(smb::native_smb::Command::Create));

    const auto writePayload =
        smb::native_smb::decodeDirectTcpPayload(transport.requestFrames[1]);
    QVERIFY(writePayload.ok);
    const auto writeHeader =
        smb::native_smb::decodeSmb2SyncHeader(writePayload.value);
    QVERIFY(writeHeader.ok);
    QCOMPARE(static_cast<int>(writeHeader.value.command),
             static_cast<int>(smb::native_smb::Command::Write));

    const auto closePayload =
        smb::native_smb::decodeDirectTcpPayload(transport.requestFrames[2]);
    QVERIFY(closePayload.ok);
    const auto closeHeader =
        smb::native_smb::decodeSmb2SyncHeader(closePayload.value);
    QVERIFY(closeHeader.ok);
    QCOMPARE(static_cast<int>(closeHeader.value.command),
             static_cast<int>(smb::native_smb::Command::Close));
  }

  void stopsWhenWriteResponseIsInvalid() {
    auto badWrite = writeResponsePayload();
    badWrite[12] = static_cast<std::uint8_t>(
        static_cast<std::uint16_t>(smb::native_smb::Command::Read) & 0xFF);

    ScriptedTransport transport({framedSuccess(createResponsePayload()),
                                 framedSuccess(badWrite),
                                 framedSuccess(closeResponsePayload())});

    const smb::native_smb::FileWriter writer;
    const auto result =
        writer.writeOnce(transport, "report.txt", {'t', 'e', 'x', 't'}, 0, 80,
                         77, 34, {});

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

    const smb::native_smb::FileWriter writer;
    const auto result =
        writer.writeOnce(transport, "report.txt", {'t'}, 0, 80, 77, 34,
                         context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{0});
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }
};

QTEST_MAIN(NativeSmbFileWriterTest)

#include "test_native_smb_file_writer.moc"
