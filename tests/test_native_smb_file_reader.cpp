#include "FileReader.h"

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
  header.messageId = 60;
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
  appendU64Le(bytes, 4);
  appendU32Le(bytes, smb::native_smb::kFileAttributeNormal);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector readResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Read;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 61;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kReadResponseStructureSize);
  bytes.push_back(80);
  bytes.push_back(0);
  appendU32Le(bytes, 4);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  bytes.push_back('t');
  bytes.push_back('e');
  bytes.push_back('x');
  bytes.push_back('t');
  return bytes;
}

smb::native_smb::ByteVector closeResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Close;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 62;
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

class NativeSmbFileReaderTest final : public QObject {
  Q_OBJECT

private slots:
  void readsFileOverScriptedTransport() {
    ScriptedTransport transport({framedSuccess(createResponsePayload()),
                                 framedSuccess(readResponsePayload()),
                                 framedSuccess(closeResponsePayload())});

    const smb::native_smb::FileReader reader;
    const auto result = reader.readOnce(transport, "report.txt", 4, 0, 60, 77,
                                        34, {});

    QVERIFY(result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{3});
    QCOMPARE(result.value.fileId.persistent,
             std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(result.value.data.size(), std::size_t{4});
    QCOMPARE(QString::fromUtf8(
                 reinterpret_cast<const char *>(result.value.data.data()),
                 static_cast<int>(result.value.data.size())),
             QStringLiteral("text"));

    const auto createPayload =
        smb::native_smb::decodeDirectTcpPayload(transport.requestFrames[0]);
    QVERIFY(createPayload.ok);
    const auto createHeader =
        smb::native_smb::decodeSmb2SyncHeader(createPayload.value);
    QVERIFY(createHeader.ok);
    QCOMPARE(static_cast<int>(createHeader.value.command),
             static_cast<int>(smb::native_smb::Command::Create));

    const auto readPayload =
        smb::native_smb::decodeDirectTcpPayload(transport.requestFrames[1]);
    QVERIFY(readPayload.ok);
    const auto readHeader =
        smb::native_smb::decodeSmb2SyncHeader(readPayload.value);
    QVERIFY(readHeader.ok);
    QCOMPARE(static_cast<int>(readHeader.value.command),
             static_cast<int>(smb::native_smb::Command::Read));

    const auto closePayload =
        smb::native_smb::decodeDirectTcpPayload(transport.requestFrames[2]);
    QVERIFY(closePayload.ok);
    const auto closeHeader =
        smb::native_smb::decodeSmb2SyncHeader(closePayload.value);
    QVERIFY(closeHeader.ok);
    QCOMPARE(static_cast<int>(closeHeader.value.command),
             static_cast<int>(smb::native_smb::Command::Close));
  }

  void stopsWhenReadResponseIsInvalid() {
    auto badRead = readResponsePayload();
    badRead[12] = static_cast<std::uint8_t>(
        static_cast<std::uint16_t>(smb::native_smb::Command::Close) & 0xFF);

    ScriptedTransport transport({framedSuccess(createResponsePayload()),
                                 framedSuccess(badRead),
                                 framedSuccess(closeResponsePayload())});

    const smb::native_smb::FileReader reader;
    const auto result = reader.readOnce(transport, "report.txt", 4, 0, 60, 77,
                                        34, {});

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

    const smb::native_smb::FileReader reader;
    const auto result = reader.readOnce(transport, "report.txt", 4, 0, 60, 77,
                                        34, context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{0});
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }
};

QTEST_MAIN(NativeSmbFileReaderTest)

#include "test_native_smb_file_reader.moc"
