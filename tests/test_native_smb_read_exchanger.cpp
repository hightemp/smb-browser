#include "ReadExchanger.h"

#include <QtTest/QtTest>

#include <cstdint>

namespace {

class ScriptedTransport final : public smb::native_smb::Transport {
public:
  explicit ScriptedTransport(smb::native_smb::DecodeResult<
                             smb::native_smb::ByteVector> response)
      : m_response(std::move(response)) {}

  smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
  exchange(const smb::native_smb::ByteVector &requestFrame,
           const smb::native_smb::OperationContext &) override {
    ++calls;
    lastRequestFrame = requestFrame;
    return m_response;
  }

  int calls = 0;
  smb::native_smb::ByteVector lastRequestFrame;

private:
  smb::native_smb::DecodeResult<smb::native_smb::ByteVector> m_response;
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

smb::native_smb::ByteVector readResponsePayload(
    smb::native_smb::Command command = smb::native_smb::Command::Read) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 50;
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

} // namespace

class NativeSmbReadExchangerTest final : public QObject {
  Q_OBJECT

private slots:
  void readsOverScriptedTransport() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(readResponsePayload())));

    smb::native_smb::ReadRequestOptions options;
    options.fileId.persistent = 1;
    options.fileId.volatileId = 2;
    options.length = 4;

    const smb::native_smb::ReadExchanger reader;
    const auto result = reader.read(transport, options, 50, 77, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(transport.calls, 1);
    QCOMPARE(result.value.data.size(), std::size_t{4});
    QCOMPARE(result.value.data[0], std::uint8_t{'d'});

    const auto payload =
        smb::native_smb::decodeDirectTcpPayload(transport.lastRequestFrame);
    QVERIFY(payload.ok);
    const auto header = smb::native_smb::decodeSmb2SyncHeader(payload.value);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::Read));
  }

  void returnsCancelledBeforeTransportCall() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(readResponsePayload())));
    smb::native_smb::CancellationToken token;
    token.cancel();
    smb::native_smb::OperationContext context;
    context.cancellationToken = &token;

    const smb::native_smb::ReadExchanger reader;
    const auto result = reader.read(transport, {}, 1, 2, 3, context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.calls, 0);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }

  void rejectsUnexpectedResponseCommand() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(
                readResponsePayload(smb::native_smb::Command::Close))));

    const smb::native_smb::ReadExchanger reader;
    const auto result = reader.read(transport, {}, 1, 2, 3, {});

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }
};

QTEST_MAIN(NativeSmbReadExchangerTest)

#include "test_native_smb_read_exchanger.moc"
