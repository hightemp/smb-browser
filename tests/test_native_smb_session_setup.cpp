#include "SessionSetupExchanger.h"

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

std::uint16_t readU16Le(const smb::native_smb::ByteVector &bytes,
                        std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

smb::native_smb::ByteVector sessionSetupResponsePayload(
    smb::native_smb::Command command =
        smb::native_smb::Command::SessionSetup) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.status = smb::native_smb::kStatusMoreProcessingRequired;
  header.messageId = 2;
  header.sessionId = 0xAABBCCDDEEFF0011ULL;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kSessionSetupResponseStructureSize);
  appendU16Le(bytes, smb::native_smb::kSessionFlagEncryptData);
  appendU16Le(bytes, 72);
  appendU16Le(bytes, 4);
  bytes.push_back('T');
  bytes.push_back('O');
  bytes.push_back('K');
  bytes.push_back('2');
  return bytes;
}

} // namespace

class NativeSmbSessionSetupTest final : public QObject {
  Q_OBJECT

private slots:
  void exchangesSessionSetupOverScriptedTransport() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(
                sessionSetupResponsePayload())));

    smb::native_smb::SessionSetupRequestOptions options;
    options.signing = smb::native_smb::SecurityPolicy::Required;
    options.securityBuffer = {'T', 'O', 'K', '1'};

    const smb::native_smb::SessionSetupExchanger exchanger;
    const auto result = exchanger.exchange(transport, options, 2, 0, {});

    QVERIFY(result.ok);
    QCOMPARE(transport.calls, 1);
    QCOMPARE(result.value.status,
             smb::native_smb::kStatusMoreProcessingRequired);
    QCOMPARE(result.value.sessionId, std::uint64_t{0xAABBCCDDEEFF0011ULL});
    QVERIFY(result.value.moreProcessingRequired);
    QVERIFY(result.value.encryptData);
    QCOMPARE(result.value.securityBuffer.size(), std::size_t{4});
    QCOMPARE(result.value.securityBuffer[3], std::uint8_t{'2'});

    const auto payload =
        smb::native_smb::decodeDirectTcpPayload(transport.lastRequestFrame);
    QVERIFY(payload.ok);
    const auto header = smb::native_smb::decodeSmb2SyncHeader(payload.value);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::SessionSetup));
    QCOMPARE(readU16Le(payload.value, 64),
             smb::native_smb::kSessionSetupRequestStructureSize);
    QCOMPARE(readU16Le(payload.value, 76), std::uint16_t{88});
    QCOMPARE(readU16Le(payload.value, 78), std::uint16_t{4});
  }

  void returnsCancelledBeforeTransportCall() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(
                sessionSetupResponsePayload())));
    smb::native_smb::CancellationToken token;
    token.cancel();
    smb::native_smb::OperationContext context;
    context.cancellationToken = &token;

    const smb::native_smb::SessionSetupExchanger exchanger;
    const auto result = exchanger.exchange(transport, {}, 1, 0, context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.calls, 0);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }

  void rejectsUnexpectedResponseCommand() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(
                sessionSetupResponsePayload(
                    smb::native_smb::Command::TreeConnect))));

    const smb::native_smb::SessionSetupExchanger exchanger;
    const auto result = exchanger.exchange(transport, {}, 1, 0, {});

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }
};

QTEST_MAIN(NativeSmbSessionSetupTest)

#include "test_native_smb_session_setup.moc"
