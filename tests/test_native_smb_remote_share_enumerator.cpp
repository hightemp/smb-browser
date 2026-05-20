#include "Dcerpc.h"
#include "RemoteShareEnumerator.h"
#include "SrvsRpc.h"

#include <QtTest/QtTest>

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

void align4(smb::native_smb::ByteVector &bytes) {
  while ((bytes.size() % 4) != 0) {
    bytes.push_back(0);
  }
}

void appendCommonRpcHeader(smb::native_smb::ByteVector &bytes,
                           std::uint8_t packetType,
                           std::uint16_t fragLength,
                           std::uint32_t callId) {
  bytes.push_back(smb::native_smb::kDcerpcVersion);
  bytes.push_back(0);
  bytes.push_back(packetType);
  bytes.push_back(smb::native_smb::kDcerpcFlagFirstFragment |
                  smb::native_smb::kDcerpcFlagLastFragment);
  bytes.push_back(0x10);
  bytes.push_back(0);
  bytes.push_back(0);
  bytes.push_back(0);
  appendU16Le(bytes, fragLength);
  appendU16Le(bytes, 0);
  appendU32Le(bytes, callId);
}

void appendNdrUtf16String(smb::native_smb::ByteVector &bytes,
                          std::string_view text) {
  const auto utf16 = smb::native_smb::encodeUtf16Le(text);
  const auto characters = static_cast<std::uint32_t>(utf16.size() / 2 + 1);
  appendU32Le(bytes, characters);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, characters);
  bytes.insert(bytes.end(), utf16.begin(), utf16.end());
  appendU16Le(bytes, 0);
  align4(bytes);
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
  appendU32Le(bytes, smb::native_smb::kFileAttributeNormal);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector writeResponsePayload(std::uint32_t count) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Write;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kWriteResponseStructureSize);
  appendU16Le(bytes, 0);
  appendU32Le(bytes, count);
  appendU32Le(bytes, 0);
  appendU16Le(bytes, 0);
  appendU16Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector readResponsePayload(
    const smb::native_smb::ByteVector &data) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Read;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kReadResponseStructureSize);
  bytes.push_back(80);
  bytes.push_back(0);
  appendU32Le(bytes, static_cast<std::uint32_t>(data.size()));
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  bytes.insert(bytes.end(), data.begin(), data.end());
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

smb::native_smb::ByteVector bindAckPdu() {
  smb::native_smb::ByteVector bytes;
  appendCommonRpcHeader(bytes, smb::native_smb::kDcerpcPacketTypeBindAck, 56,
                        1);
  appendU16Le(bytes, 4280);
  appendU16Le(bytes, 4280);
  appendU32Le(bytes, 1);
  appendU16Le(bytes, 0);
  appendU16Le(bytes, 0);
  bytes.push_back(1);
  bytes.push_back(0);
  appendU16Le(bytes, 0);
  appendU16Le(bytes, 0);
  appendU16Le(bytes, 0);
  const auto &syntax = smb::native_smb::ndr32TransferSyntax();
  bytes.insert(bytes.end(), syntax.uuid.begin(), syntax.uuid.end());
  appendU16Le(bytes, syntax.majorVersion);
  appendU16Le(bytes, syntax.minorVersion);
  return bytes;
}

smb::native_smb::ByteVector shareEnumStub() {
  smb::native_smb::ByteVector bytes;
  appendU32Le(bytes, 1);
  appendU32Le(bytes, 1);
  appendU32Le(bytes, 0x00020000);
  appendU32Le(bytes, 2);
  appendU32Le(bytes, 0x00020004);
  appendU32Le(bytes, 2);

  appendU32Le(bytes, 0x00020008);
  appendU32Le(bytes, smb::native_smb::kShareTypeDisk);
  appendU32Le(bytes, 0x0002000C);

  appendU32Le(bytes, 0x00020010);
  appendU32Le(bytes,
              smb::native_smb::kShareTypeIpc |
                  smb::native_smb::kShareTypeSpecial);
  appendU32Le(bytes, 0);

  appendNdrUtf16String(bytes, "public");
  appendNdrUtf16String(bytes, "Public files");
  appendNdrUtf16String(bytes, "IPC$");

  appendU32Le(bytes, 2);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, smb::native_smb::kNetApiStatusSuccess);
  return bytes;
}

smb::native_smb::ByteVector dcerpcResponsePdu(
    const smb::native_smb::ByteVector &stub) {
  smb::native_smb::ByteVector bytes;
  appendCommonRpcHeader(
      bytes, smb::native_smb::kDcerpcPacketTypeResponse,
      static_cast<std::uint16_t>(16 + 8 + stub.size()), 2);
  appendU32Le(bytes, static_cast<std::uint32_t>(stub.size()));
  appendU16Le(bytes, 0);
  bytes.push_back(0);
  bytes.push_back(0);
  bytes.insert(bytes.end(), stub.begin(), stub.end());
  return bytes;
}

smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
framedSuccess(const smb::native_smb::ByteVector &payload) {
  return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
      smb::native_smb::encodeDirectTcpFrame(payload));
}

smb::native_smb::Command commandForFrame(
    const smb::native_smb::ByteVector &frame) {
  const auto payload = smb::native_smb::decodeDirectTcpPayload(frame);
  if (!payload.ok) {
    return smb::native_smb::Command::Negotiate;
  }
  const auto header = smb::native_smb::decodeSmb2SyncHeader(payload.value);
  if (!header.ok) {
    return smb::native_smb::Command::Negotiate;
  }
  return header.value.command;
}

} // namespace

class NativeSmbRemoteShareEnumeratorTest final : public QObject {
  Q_OBJECT

private slots:
  void listsSharesOverSrvsvcNamedPipe() {
    const auto bindPdu =
        smb::native_smb::buildDcerpcBindPdu(smb::native_smb::srvsRpcSyntax(),
                                            1);
    const auto requestPdu = smb::native_smb::buildDcerpcRequestPdu(
        smb::native_smb::kSrvsNetrShareEnumOpnum,
        smb::native_smb::buildNetrShareEnumRequestStub("server"), 2);

    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(writeResponsePayload(static_cast<std::uint32_t>(
            bindPdu.size()))),
        framedSuccess(readResponsePayload(bindAckPdu())),
        framedSuccess(writeResponsePayload(static_cast<std::uint32_t>(
            requestPdu.size()))),
        framedSuccess(readResponsePayload(dcerpcResponsePdu(shareEnumStub()))),
        framedSuccess(closeResponsePayload()),
    });

    const smb::native_smb::RemoteShareEnumerator enumerator;
    const auto listed =
        enumerator.listShares(transport, "server", 10, 77, 34, {});

    QVERIFY2(listed.ok, listed.error.message.c_str());
    QCOMPARE(static_cast<int>(listed.value.shares.size()), 2);
    QCOMPARE(QString::fromStdString(listed.value.shares[0].name),
             QStringLiteral("public"));
    QCOMPARE(QString::fromStdString(listed.value.shares[0].type),
             QStringLiteral("disk"));
    QCOMPARE(QString::fromStdString(listed.value.shares[0].comment),
             QStringLiteral("Public files"));
    QCOMPARE(QString::fromStdString(listed.value.shares[1].name),
             QStringLiteral("IPC$"));
    QCOMPARE(QString::fromStdString(listed.value.shares[1].type),
             QStringLiteral("ipc"));
    QVERIFY(listed.value.shares[1].hidden);
    QCOMPARE(listed.value.messagesUsed, 6ULL);

    QCOMPARE(static_cast<int>(transport.requestFrames.size()), 6);
    QCOMPARE(static_cast<int>(commandForFrame(transport.requestFrames[0])),
             static_cast<int>(smb::native_smb::Command::Create));
    QCOMPARE(static_cast<int>(commandForFrame(transport.requestFrames[1])),
             static_cast<int>(smb::native_smb::Command::Write));
    QCOMPARE(static_cast<int>(commandForFrame(transport.requestFrames[2])),
             static_cast<int>(smb::native_smb::Command::Read));
    QCOMPARE(static_cast<int>(commandForFrame(transport.requestFrames[3])),
             static_cast<int>(smb::native_smb::Command::Write));
    QCOMPARE(static_cast<int>(commandForFrame(transport.requestFrames[4])),
             static_cast<int>(smb::native_smb::Command::Read));
    QCOMPARE(static_cast<int>(commandForFrame(transport.requestFrames[5])),
             static_cast<int>(smb::native_smb::Command::Close));
  }

  void closesPipeAfterRpcDecodeFailure() {
    const auto bindPdu =
        smb::native_smb::buildDcerpcBindPdu(smb::native_smb::srvsRpcSyntax(),
                                            1);
    const auto requestPdu = smb::native_smb::buildDcerpcRequestPdu(
        smb::native_smb::kSrvsNetrShareEnumOpnum,
        smb::native_smb::buildNetrShareEnumRequestStub("server"), 2);

    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(writeResponsePayload(static_cast<std::uint32_t>(
            bindPdu.size()))),
        framedSuccess(readResponsePayload(bindAckPdu())),
        framedSuccess(writeResponsePayload(static_cast<std::uint32_t>(
            requestPdu.size()))),
        framedSuccess(readResponsePayload({'b', 'a', 'd'})),
        framedSuccess(closeResponsePayload()),
    });

    const smb::native_smb::RemoteShareEnumerator enumerator;
    const auto listed =
        enumerator.listShares(transport, "server", 10, 77, 34, {});

    QVERIFY(!listed.ok);
    QCOMPARE(static_cast<int>(transport.requestFrames.size()), 6);
    QCOMPARE(static_cast<int>(commandForFrame(transport.requestFrames[5])),
             static_cast<int>(smb::native_smb::Command::Close));
  }
};

QTEST_MAIN(NativeSmbRemoteShareEnumeratorTest)

#include "test_native_smb_remote_share_enumerator.moc"
