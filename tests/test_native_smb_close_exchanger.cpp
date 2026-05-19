#include "CloseExchanger.h"

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

void appendU64Le(smb::native_smb::ByteVector &bytes, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
  }
}

smb::native_smb::ByteVector closeResponsePayload(
    smb::native_smb::Command command = smb::native_smb::Command::Close) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 40;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kCloseResponseStructureSize);
  appendU16Le(bytes, smb::native_smb::kCloseFlagPostQueryAttrib);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 1);
  appendU64Le(bytes, 2);
  appendU64Le(bytes, 3);
  appendU64Le(bytes, 4);
  appendU64Le(bytes, 5);
  appendU64Le(bytes, 6);
  appendU32Le(bytes, smb::native_smb::kFileAttributeDirectory);
  return bytes;
}

} // namespace

class NativeSmbCloseExchangerTest final : public QObject {
  Q_OBJECT

private slots:
  void closesHandleOverScriptedTransport() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(closeResponsePayload())));

    smb::native_smb::CloseRequestOptions options;
    options.fileId.persistent = 0x0102030405060708ULL;
    options.fileId.volatileId = 0x1112131415161718ULL;

    const smb::native_smb::CloseExchanger closer;
    const auto result = closer.close(transport, options, 40, 77, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(transport.calls, 1);
    QVERIFY(result.value.hasPostQueryAttributes);
    QCOMPARE(result.value.endOfFile, std::uint64_t{6});
    QCOMPARE(result.value.fileAttributes,
             smb::native_smb::kFileAttributeDirectory);

    const auto payload =
        smb::native_smb::decodeDirectTcpPayload(transport.lastRequestFrame);
    QVERIFY(payload.ok);
    const auto header = smb::native_smb::decodeSmb2SyncHeader(payload.value);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::Close));
    QCOMPARE(header.value.treeId, std::uint32_t{77});
    QCOMPARE(header.value.sessionId, std::uint64_t{34});
  }

  void returnsCancelledBeforeTransportCall() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(closeResponsePayload())));
    smb::native_smb::CancellationToken token;
    token.cancel();
    smb::native_smb::OperationContext context;
    context.cancellationToken = &token;

    const smb::native_smb::CloseExchanger closer;
    const auto result = closer.close(transport, {}, 1, 2, 3, context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.calls, 0);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }

  void rejectsUnexpectedResponseCommand() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(
                closeResponsePayload(smb::native_smb::Command::Create))));

    const smb::native_smb::CloseExchanger closer;
    const auto result = closer.close(transport, {}, 1, 2, 3, {});

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }
};

QTEST_MAIN(NativeSmbCloseExchangerTest)

#include "test_native_smb_close_exchanger.moc"
