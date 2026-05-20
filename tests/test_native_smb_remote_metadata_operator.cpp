#include "RemoteMetadataOperator.h"

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

smb::native_smb::ByteVector createResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Create;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 90;
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

smb::native_smb::ByteVector setInfoResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::SetInfo;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 91;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kSetInfoResponseStructureSize);
  return bytes;
}

smb::native_smb::ByteVector queryInfoResponsePayload(
    const smb::native_smb::ByteVector &buffer) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::QueryInfo;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 91;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kQueryInfoResponseStructureSize);
  appendU16Le(bytes, 72);
  appendU32Le(bytes, static_cast<std::uint32_t>(buffer.size()));
  bytes.insert(bytes.end(), buffer.begin(), buffer.end());
  return bytes;
}

smb::native_smb::ByteVector closeResponsePayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Close;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.messageId = 92;
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

class NativeSmbRemoteMetadataOperatorTest final : public QObject {
  Q_OBJECT

private slots:
  void setsBasicInformationWithSetInfoFileClass() {
    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(setInfoResponsePayload()),
        framedSuccess(closeResponsePayload()),
    });

    smb::native_smb::FileBasicInformation info;
    info.creationTime = 10;
    info.lastAccessTime = 20;
    info.lastWriteTime = 30;
    info.changeTime = 40;
    info.fileAttributes = smb::native_smb::kFileAttributeNormal;

    const smb::native_smb::RemoteMetadataOperator metadata;
    const auto result =
        metadata.setBasicInformation(transport, "file.txt", info, 90, 77, 34,
                                     {});

    QVERIFY(result.ok);
    QCOMPARE(result.value.messagesUsed, std::uint64_t{3});
    QCOMPARE(transport.requestFrames.size(), std::size_t{3});
    QCOMPARE(static_cast<int>(requestCommand(transport.requestFrames[1])),
             static_cast<int>(smb::native_smb::Command::SetInfo));

    const auto setPayload = requestPayload(transport.requestFrames[1]);
    QCOMPARE(setPayload[66], smb::native_smb::kInfoTypeFile);
    QCOMPARE(setPayload[67], smb::native_smb::kFileBasicInformation);
    QCOMPARE(readU32Le(setPayload, 68), std::uint32_t{40});
    QCOMPARE(readU16Le(setPayload, 72), std::uint16_t{96});
    QCOMPARE(readU32Le(setPayload, 128),
             smb::native_smb::kFileAttributeNormal);
  }

  void listsExtendedAttributesWithQueryInfo() {
    smb::native_smb::FileFullEaInformation ea;
    ea.name = "user.comment";
    ea.value = {'o', 'k'};
    const auto eaBuffer = smb::native_smb::buildFileFullEaInformation({ea});
    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(queryInfoResponsePayload(eaBuffer)),
        framedSuccess(closeResponsePayload()),
    });

    const smb::native_smb::RemoteMetadataOperator metadata;
    const auto result =
        metadata.listExtendedAttributes(transport, "file.txt", 90, 77, 34, {});

    QVERIFY(result.ok);
    QCOMPARE(result.value.entries.size(), std::size_t{1});
    QCOMPARE(result.value.entries[0].name, std::string{"user.comment"});
    QCOMPARE(result.value.entries[0].value,
             smb::native_smb::ByteVector({'o', 'k'}));

    const auto queryPayload = requestPayload(transport.requestFrames[1]);
    QCOMPARE(queryPayload[66], smb::native_smb::kInfoTypeFile);
    QCOMPARE(queryPayload[67], smb::native_smb::kFileFullEaInformation);
  }

  void setsAndRemovesExtendedAttributesWithSetInfo() {
    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(setInfoResponsePayload()),
        framedSuccess(closeResponsePayload()),
        framedSuccess(createResponsePayload()),
        framedSuccess(setInfoResponsePayload()),
        framedSuccess(closeResponsePayload()),
    });

    smb::native_smb::FileFullEaInformation ea;
    ea.name = "user.comment";
    ea.value = {'o', 'k'};

    const smb::native_smb::RemoteMetadataOperator metadata;
    QVERIFY(metadata
                .setExtendedAttributes(transport, "file.txt", {ea}, 90, 77,
                                       34, {})
                .ok);
    QVERIFY(metadata
                .removeExtendedAttribute(transport, "file.txt",
                                         "user.comment", 93, 77, 34, {})
                .ok);

    const auto setPayload = requestPayload(transport.requestFrames[1]);
    QCOMPARE(setPayload[66], smb::native_smb::kInfoTypeFile);
    QCOMPARE(setPayload[67], smb::native_smb::kFileFullEaInformation);
    QCOMPARE(readU32Le(setPayload, 68), std::uint32_t{23});

    const auto removePayload = requestPayload(transport.requestFrames[4]);
    QCOMPARE(removePayload[66], smb::native_smb::kInfoTypeFile);
    QCOMPARE(removePayload[67], smb::native_smb::kFileFullEaInformation);
    QCOMPARE(readU16Le(removePayload, 102), std::uint16_t{0});
  }

  void queriesAndSetsSecurityDescriptors() {
    const smb::native_smb::ByteVector descriptor = {1, 0, 4, 128, 0, 0, 0, 0};
    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(queryInfoResponsePayload(descriptor)),
        framedSuccess(closeResponsePayload()),
        framedSuccess(createResponsePayload()),
        framedSuccess(setInfoResponsePayload()),
        framedSuccess(closeResponsePayload()),
    });

    const smb::native_smb::RemoteMetadataOperator metadata;
    const auto query = metadata.querySecurityDescriptor(
        transport, "file.txt", smb::native_smb::kDaclSecurityInformation, 90,
        77, 34, {});
    QVERIFY(query.ok);
    QCOMPARE(query.value.descriptor, descriptor);
    QVERIFY(metadata
                .setSecurityDescriptor(
                    transport, "file.txt",
                    smb::native_smb::kDaclSecurityInformation, descriptor, 93,
                    77, 34, {})
                .ok);

    const auto queryPayload = requestPayload(transport.requestFrames[1]);
    QCOMPARE(queryPayload[66], smb::native_smb::kInfoTypeSecurity);
    QCOMPARE(readU32Le(queryPayload, 80),
             smb::native_smb::kDaclSecurityInformation);

    const auto setPayload = requestPayload(transport.requestFrames[4]);
    QCOMPARE(setPayload[66], smb::native_smb::kInfoTypeSecurity);
    QCOMPARE(readU32Le(setPayload, 76),
             smb::native_smb::kDaclSecurityInformation);
    QCOMPARE(readU32Le(setPayload, 68),
             static_cast<std::uint32_t>(descriptor.size()));
  }

  void returnsCancelledBeforeTransportCall() {
    ScriptedTransport transport({framedSuccess(createResponsePayload())});
    smb::native_smb::CancellationToken token;
    token.cancel();
    smb::native_smb::OperationContext context;
    context.cancellationToken = &token;

    smb::native_smb::FileBasicInformation info;
    const smb::native_smb::RemoteMetadataOperator metadata;
    const auto result =
        metadata.setBasicInformation(transport, "file.txt", info, 90, 77, 34,
                                     context);

    QVERIFY(!result.ok);
    QCOMPARE(transport.requestFrames.size(), std::size_t{0});
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
  }
};

QTEST_MAIN(NativeSmbRemoteMetadataOperatorTest)

#include "test_native_smb_remote_metadata_operator.moc"
