#include "Negotiator.h"

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

smb::native_smb::ByteVector negotiateResponsePayload(
    std::uint16_t securityMode = 0x0002,
    smb::native_smb::Dialect dialect = smb::native_smb::Dialect::Smb302,
    std::uint32_t capabilities = smb::native_smb::capabilityMask(
        {smb::native_smb::GlobalCapability::Dfs,
         smb::native_smb::GlobalCapability::Encryption})) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Negotiate;
  header.flags = smb::native_smb::kFlagServerToRedir;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kNegotiateResponseStructureSize);
  appendU16Le(bytes, securityMode);
  appendU16Le(bytes, static_cast<std::uint16_t>(dialect));
  appendU16Le(bytes, 0);
  for (int i = 0; i < 16; ++i) {
    bytes.push_back(static_cast<std::uint8_t>(0xA0 + i));
  }
  appendU32Le(bytes, capabilities);
  appendU32Le(bytes, 0x00100000);
  appendU32Le(bytes, 0x00200000);
  appendU32Le(bytes, 0x00300000);
  appendU64Le(bytes, 0);
  appendU64Le(bytes, 0);
  appendU16Le(bytes, 128);
  appendU16Le(bytes, 3);
  appendU32Le(bytes, 0);
  bytes.push_back('S');
  bytes.push_back('P');
  bytes.push_back('N');
  return bytes;
}

} // namespace

class NativeSmbNegotiatorTest final : public QObject {
  Q_OBJECT

private slots:
  void negotiatesOverScriptedTransport() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(
                negotiateResponsePayload())));

    smb::native_smb::NegotiateRequestOptions options;
    options.signing = smb::native_smb::SecurityPolicy::Preferred;
    smb::native_smb::OperationContext context;

    const smb::native_smb::Negotiator negotiator;
    const auto result = negotiator.negotiate(transport, options, context);

    QVERIFY(result.ok);
    QCOMPARE(transport.calls, 1);
    QVERIFY(result.value.signingRequired);
    QVERIFY(result.value.encryptionSupported);
    QCOMPARE(static_cast<int>(result.value.dialect),
             static_cast<int>(smb::native_smb::Dialect::Smb302));
    QCOMPARE(result.value.maxReadSize, std::uint32_t{0x00200000});
    QCOMPARE(result.value.maxWriteSize, std::uint32_t{0x00300000});
    QCOMPARE(result.value.securityBuffer.size(), std::size_t{3});

    const auto requestLength =
        smb::native_smb::decodeDirectTcpPayloadLength(transport.lastRequestFrame);
    QVERIFY(requestLength.ok);
    QCOMPARE(requestLength.value,
             static_cast<std::uint32_t>(transport.lastRequestFrame.size() - 4));

    smb::native_smb::ByteVector requestPayload(
        transport.lastRequestFrame.begin() + smb::native_smb::kDirectTcpHeaderSize,
        transport.lastRequestFrame.end());
    const auto requestHeader =
        smb::native_smb::decodeSmb2SyncHeader(requestPayload);
    QVERIFY(requestHeader.ok);
    QCOMPARE(static_cast<int>(requestHeader.value.command),
             static_cast<int>(smb::native_smb::Command::Negotiate));
  }

  void returnsCancelledBeforeTransportCall() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::encodeDirectTcpFrame(
                negotiateResponsePayload())));
    smb::native_smb::CancellationToken token;
    token.cancel();

    smb::native_smb::OperationContext context;
    context.cancellationToken = &token;

    const smb::native_smb::Negotiator negotiator;
    const auto result =
        negotiator.negotiate(transport, {}, context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.calls, 0);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }

  void rejectsInvalidTransportFrame() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::ByteVector{1, 0, 0, 0}));

    const smb::native_smb::Negotiator negotiator;
    const auto result =
        negotiator.negotiate(transport, {}, {});

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }

  void rejectsShortTransportFramePayload() {
    ScriptedTransport transport(
        smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
            smb::native_smb::ByteVector{0, 0, 0, 64}));

    const smb::native_smb::Negotiator negotiator;
    const auto result =
        negotiator.negotiate(transport, {}, {});

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::IoError));
  }
};

QTEST_MAIN(NativeSmbNegotiatorTest)

#include "test_native_smb_negotiator.moc"
