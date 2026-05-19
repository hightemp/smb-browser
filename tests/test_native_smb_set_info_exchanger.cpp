#include "SetInfoExchanger.h"

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

smb::native_smb::ByteVector setInfoResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::SetInfo;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 50;
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

} // namespace

class NativeSmbSetInfoExchangerTest final : public QObject {
  Q_OBJECT

private slots:
  void sendsSetInfoOverScriptedTransport() {
    ScriptedTransport transport({framedSuccess(setInfoResponsePayload())});

    smb::native_smb::SetInfoRequestOptions options;
    options.fileId.persistent = 0x0102030405060708ULL;
    options.fileId.volatileId = 0x1112131415161718ULL;
    options.infoType = smb::native_smb::kInfoTypeFile;
    options.fileInfoClass = smb::native_smb::kFileDispositionInformation;
    options.buffer = smb::native_smb::buildFileDispositionInformation(true);

    const smb::native_smb::SetInfoExchanger exchanger;
    const auto result =
        exchanger.setInfo(transport, options, 50, 77, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(result.value.status, smb::native_smb::kStatusSuccess);
    QCOMPARE(transport.requestFrames.size(), std::size_t{1});

    const auto payload =
        smb::native_smb::decodeDirectTcpPayload(transport.requestFrames[0]);
    QVERIFY(payload.ok);
    const auto header =
        smb::native_smb::decodeSmb2SyncHeader(payload.value);
    QVERIFY(header.ok);
    QCOMPARE(static_cast<int>(header.value.command),
             static_cast<int>(smb::native_smb::Command::SetInfo));
  }

  void rejectsUnexpectedResponseCommand() {
    auto badResponse = setInfoResponsePayload();
    badResponse[12] = static_cast<std::uint8_t>(
        static_cast<std::uint16_t>(smb::native_smb::Command::Write) & 0xFF);

    ScriptedTransport transport({framedSuccess(badResponse)});
    smb::native_smb::SetInfoRequestOptions options;
    options.fileId.persistent = 1;
    options.fileInfoClass = smb::native_smb::kFileDispositionInformation;
    options.buffer = smb::native_smb::buildFileDispositionInformation(true);

    const smb::native_smb::SetInfoExchanger exchanger;
    const auto result =
        exchanger.setInfo(transport, options, 50, 77, 34, {});

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }

  void returnsCancelledBeforeTransportCall() {
    ScriptedTransport transport({framedSuccess(setInfoResponsePayload())});
    smb::native_smb::CancellationToken token;
    token.cancel();
    smb::native_smb::OperationContext context;
    context.cancellationToken = &token;

    smb::native_smb::SetInfoRequestOptions options;
    options.fileId.persistent = 1;
    options.fileInfoClass = smb::native_smb::kFileDispositionInformation;
    options.buffer = smb::native_smb::buildFileDispositionInformation(true);

    const smb::native_smb::SetInfoExchanger exchanger;
    const auto result =
        exchanger.setInfo(transport, options, 50, 77, 34, context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{0});
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }
};

QTEST_MAIN(NativeSmbSetInfoExchangerTest)

#include "test_native_smb_set_info_exchanger.moc"
