#include "TreeConnector.h"

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

std::uint16_t readU16Le(const smb::native_smb::ByteVector &bytes,
                        std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

smb::native_smb::ByteVector treeConnectResponsePayload(
    smb::native_smb::Command command =
        smb::native_smb::Command::TreeConnect) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 15;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kTreeConnectResponseStructureSize);
  bytes.push_back(
      static_cast<std::uint8_t>(smb::native_smb::ShareType::Disk));
  bytes.push_back(0);
  appendU32Le(bytes, smb::native_smb::kShareFlagDfs |
                       smb::native_smb::kShareFlagEncryptData);
  appendU32Le(bytes, smb::native_smb::kShareCapabilityDfs);
  appendU32Le(bytes, 0x001F01FF);
  return bytes;
}

} // namespace

class NativeSmbTreeConnectorTest final : public QObject {
  Q_OBJECT

private slots:
  void connectsTreeOverScriptedTransport() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(
                treeConnectResponsePayload())));

    smb::native_smb::TreeConnectRequestOptions options;
    options.server = "server";
    options.share = "share";

    const smb::native_smb::TreeConnector connector;
    const auto result = connector.connect(transport, options, 15, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(transport.calls, 1);
    QCOMPARE(result.value.treeId, std::uint32_t{77});
    QCOMPARE(static_cast<int>(result.value.shareType),
             static_cast<int>(smb::native_smb::ShareType::Disk));
    QVERIFY(result.value.isDfs);
    QVERIFY(!result.value.isDfsRoot);
    QVERIFY(result.value.requiresEncryption);
    QCOMPARE(result.value.maximalAccess, std::uint32_t{0x001F01FF});

    const auto payload =
        smb::native_smb::decodeDirectTcpPayload(transport.lastRequestFrame);
    QVERIFY(payload.ok);
    const auto header = smb::native_smb::decodeSmb2SyncHeader(payload.value);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::TreeConnect));
    QCOMPARE(header.value.messageId, std::uint64_t{15});
    QCOMPARE(header.value.sessionId, std::uint64_t{34});
    QCOMPARE(readU16Le(payload.value, 64),
             smb::native_smb::kTreeConnectRequestStructureSize);
    QCOMPARE(readU16Le(payload.value, 68), std::uint16_t{72});
  }

  void returnsCancelledBeforeTransportCall() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(
                treeConnectResponsePayload())));
    smb::native_smb::CancellationToken token;
    token.cancel();
    smb::native_smb::OperationContext context;
    context.cancellationToken = &token;

    const smb::native_smb::TreeConnector connector;
    const auto result = connector.connect(transport, {}, 1, 2, context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.calls, 0);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }

  void rejectsUnexpectedResponseCommand() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(
                treeConnectResponsePayload(
                    smb::native_smb::Command::Negotiate))));

    smb::native_smb::TreeConnectRequestOptions options;
    options.server = "server";
    options.share = "share";

    const smb::native_smb::TreeConnector connector;
    const auto result = connector.connect(transport, options, 1, 2, {});

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }
};

QTEST_MAIN(NativeSmbTreeConnectorTest)

#include "test_native_smb_tree_connector.moc"
