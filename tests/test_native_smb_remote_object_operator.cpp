#include "RemoteObjectOperator.h"

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

std::uint32_t readU32Le(const smb::native_smb::ByteVector &bytes,
                        std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

smb::native_smb::ByteVector createResponsePayload(
    smb::native_smb::Command command = smb::native_smb::Command::Create) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
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
  appendU64Le(bytes, 0);
  appendU32Le(bytes, smb::native_smb::kFileAttributeNormal);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  return bytes;
}

smb::native_smb::ByteVector setInfoResponsePayload(
    smb::native_smb::Command command = smb::native_smb::Command::SetInfo) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 61;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kSetInfoResponseStructureSize);
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

class NativeSmbRemoteObjectOperatorTest final : public QObject {
  Q_OBJECT

private slots:
  void createsDirectoryWithCreateAndClose() {
    ScriptedTransport transport({framedSuccess(createResponsePayload()),
                                 framedSuccess(closeResponsePayload())});

    const smb::native_smb::RemoteObjectOperator objects;
    const auto result =
        objects.createDirectory(transport, "new-folder", 60, 77, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{2});
    QCOMPARE(static_cast<int>(requestCommand(transport.requestFrames[0])),
             static_cast<int>(smb::native_smb::Command::Create));
    QCOMPARE(static_cast<int>(requestCommand(transport.requestFrames[1])),
             static_cast<int>(smb::native_smb::Command::Close));

    const auto createPayload = requestPayload(transport.requestFrames[0]);
    QCOMPARE(readU32Le(createPayload, 100), smb::native_smb::kFileCreate);
    QCOMPARE(readU32Le(createPayload, 104),
             smb::native_smb::kFileDirectoryFile);
  }

  void deletesObjectWithDispositionAndClose() {
    ScriptedTransport transport({framedSuccess(createResponsePayload()),
                                 framedSuccess(setInfoResponsePayload()),
                                 framedSuccess(closeResponsePayload())});

    const smb::native_smb::RemoteObjectOperator objects;
    const auto result =
        objects.deleteObject(transport, "old.txt", false, 60, 77, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{3});
    QCOMPARE(static_cast<int>(requestCommand(transport.requestFrames[0])),
             static_cast<int>(smb::native_smb::Command::Create));
    QCOMPARE(static_cast<int>(requestCommand(transport.requestFrames[1])),
             static_cast<int>(smb::native_smb::Command::SetInfo));
    QCOMPARE(static_cast<int>(requestCommand(transport.requestFrames[2])),
             static_cast<int>(smb::native_smb::Command::Close));

    const auto setInfoPayload = requestPayload(transport.requestFrames[1]);
    QCOMPARE(setInfoPayload[66], smb::native_smb::kInfoTypeFile);
    QCOMPARE(setInfoPayload[67],
             smb::native_smb::kFileDispositionInformation);
    QCOMPARE(readU32Le(setInfoPayload, 68), std::uint32_t{1});
    QCOMPARE(readU16Le(setInfoPayload, 72), std::uint16_t{96});
    QCOMPARE(setInfoPayload[96], std::uint8_t{1});
  }

  void renamesObjectWithRenameInformationAndClose() {
    ScriptedTransport transport({framedSuccess(createResponsePayload()),
                                 framedSuccess(setInfoResponsePayload()),
                                 framedSuccess(closeResponsePayload())});

    const smb::native_smb::RemoteObjectOperator objects;
    const auto result =
        objects.renameObject(transport, "old.txt", "new.txt", true, 60, 77,
                             34, {});

    QVERIFY(result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{3});

    const auto setInfoPayload = requestPayload(transport.requestFrames[1]);
    QCOMPARE(setInfoPayload[67], smb::native_smb::kFileRenameInformation);
    QCOMPARE(readU32Le(setInfoPayload, 68), std::uint32_t{34});
    QCOMPARE(readU16Le(setInfoPayload, 72), std::uint16_t{96});
    QCOMPARE(setInfoPayload[96], std::uint8_t{1});
    QCOMPARE(readU32Le(setInfoPayload, 112), std::uint32_t{14});
    QCOMPARE(readU16Le(setInfoPayload, 116), std::uint16_t{'n'});
  }

  void stopsWhenSetInfoResponseIsInvalid() {
    ScriptedTransport transport(
        {framedSuccess(createResponsePayload()),
         framedSuccess(setInfoResponsePayload(smb::native_smb::Command::Read)),
         framedSuccess(closeResponsePayload())});

    const smb::native_smb::RemoteObjectOperator objects;
    const auto result =
        objects.deleteObject(transport, "old.txt", false, 60, 77, 34, {});

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

    const smb::native_smb::RemoteObjectOperator objects;
    const auto result =
        objects.createDirectory(transport, "new-folder", 60, 77, 34, context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{0});
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }
};

QTEST_MAIN(NativeSmbRemoteObjectOperatorTest)

#include "test_native_smb_remote_object_operator.moc"
