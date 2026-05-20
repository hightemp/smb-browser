#include "NativeSmbSession.h"

#include <QElapsedTimer>
#include <QtTest/QtTest>

#include <cstdint>
#include <deque>
#include <iomanip>
#include <sstream>
#include <vector>

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

void writeU32Le(smb::native_smb::ByteVector &bytes, std::size_t offset,
                std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
  bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

void appendZeros(smb::native_smb::ByteVector &bytes, std::size_t count) {
  bytes.insert(bytes.end(), count, 0);
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
  appendZeros(bytes, 48);
  appendU32Le(bytes, smb::native_smb::kFileAttributeNormal);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
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
  appendZeros(bytes, 58);
  return bytes;
}

smb::native_smb::ByteVector directoryEntryFor(const std::string &name,
                                              std::uint64_t size) {
  const auto encodedName = smb::native_smb::encodeUtf16Le(name);
  smb::native_smb::ByteVector bytes;
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  appendU64Le(bytes, 11);
  appendU64Le(bytes, 12);
  appendU64Le(bytes, 13);
  appendU64Le(bytes, 14);
  appendU64Le(bytes, size);
  appendU64Le(bytes, size);
  appendU32Le(bytes, smb::native_smb::kFileAttributeNormal);
  appendU32Le(bytes, static_cast<std::uint32_t>(encodedName.size()));
  appendU32Le(bytes, 0);
  bytes.push_back(0);
  bytes.push_back(0);
  appendZeros(bytes, 24);
  appendU16Le(bytes, 0);
  appendU64Le(bytes, 100);
  bytes.insert(bytes.end(), encodedName.begin(), encodedName.end());
  return bytes;
}

smb::native_smb::ByteVector queryDirectoryResponsePayload(
    std::vector<smb::native_smb::ByteVector> entries) {
  smb::native_smb::ByteVector payload;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    if (index + 1 < entries.size()) {
      writeU32Le(entries[index], 0,
                 static_cast<std::uint32_t>(entries[index].size()));
    }
    payload.insert(payload.end(), entries[index].begin(),
                   entries[index].end());
  }

  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::QueryDirectory;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kQueryDirectoryResponseStructureSize);
  appendU16Le(bytes, 72);
  appendU32Le(bytes, static_cast<std::uint32_t>(payload.size()));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

smb::native_smb::ByteVector queryDirectoryNoMoreFilesPayload() {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::QueryDirectory;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.status = smb::native_smb::kStatusNoMoreFiles;
  header.treeId = 77;
  header.sessionId = 34;
  return smb::native_smb::encodeSmb2SyncHeader(header);
}

smb::native_smb::ByteVector ioctlResponsePayload(
    std::uint32_t ctlCode, const smb::native_smb::ByteVector &output) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::Ioctl;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 34;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kIoctlResponseStructureSize);
  appendU16Le(bytes, 0);
  appendU32Le(bytes, ctlCode);
  appendU64Le(bytes, 0x0102030405060708ULL);
  appendU64Le(bytes, 0x1112131415161718ULL);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 112);
  appendU32Le(bytes, static_cast<std::uint32_t>(output.size()));
  appendU32Le(bytes, smb::native_smb::kIoctlIsFsctl);
  appendU32Le(bytes, 0);
  bytes.insert(bytes.end(), output.begin(), output.end());
  return bytes;
}

smb::native_smb::ByteVector resumeKeyOutput() {
  return smb::native_smb::ByteVector(24, 0xAB);
}

smb::native_smb::ByteVector copyChunkResponseOutput(std::uint32_t bytesCopied) {
  smb::native_smb::ByteVector output;
  appendU32Le(output, 1);
  appendU32Le(output, bytesCopied);
  appendU32Le(output, bytesCopied);
  return output;
}

smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
framedSuccess(const smb::native_smb::ByteVector &payload) {
  return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
      smb::native_smb::encodeDirectTcpFrame(payload));
}

smb::native_smb::Smb2SyncHeader requestHeader(
    const smb::native_smb::ByteVector &frame) {
  const auto payload = smb::native_smb::decodeDirectTcpPayload(frame);
  return smb::native_smb::decodeSmb2SyncHeader(payload.value).value;
}

} // namespace

class NativeSmbPerfStressTest final : public QObject {
  Q_OBJECT

private slots:
  void parsesLargeDirectoryListingWithinPerfBudget() {
    std::vector<smb::native_smb::ByteVector> entries;
    entries.reserve(3000);
    for (int index = 0; index < 3000; ++index) {
      std::ostringstream name;
      name << "file-" << std::setw(5) << std::setfill('0') << index << ".txt";
      entries.push_back(directoryEntryFor(name.str(),
                                          static_cast<std::uint64_t>(index)));
    }

    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(queryDirectoryResponsePayload(std::move(entries))),
        framedSuccess(queryDirectoryNoMoreFilesPayload()),
        framedSuccess(closeResponsePayload()),
    });
    smb::native_smb::NativeSmbSessionConfig config;
    config.treeId = 77;
    config.sessionId = 34;
    smb::native_smb::NativeSmbSession session(transport, config);

    QElapsedTimer timer;
    timer.start();
    const auto listing = session.listDirectory("", {});
    const auto elapsedMs = timer.elapsed();

    QVERIFY(listing.ok);
    QCOMPARE(listing.value.entries.size(), std::size_t{3000});
    QVERIFY2(elapsedMs < 5000, "Large directory parse exceeded 5 seconds.");
  }

  void serverSideCopyChunksLargeFileAndReportsProgress() {
    constexpr std::uint32_t chunkSize = 1024 * 1024;
    std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>
        responses;
    responses.push_back(framedSuccess(createResponsePayload()));
    responses.push_back(framedSuccess(createResponsePayload()));
    responses.push_back(framedSuccess(ioctlResponsePayload(
        smb::native_smb::kFsctlSrvRequestResumeKey, resumeKeyOutput())));
    for (int index = 0; index < 5; ++index) {
      responses.push_back(framedSuccess(ioctlResponsePayload(
          smb::native_smb::kFsctlSrvCopyChunk,
          copyChunkResponseOutput(chunkSize))));
    }
    responses.push_back(framedSuccess(closeResponsePayload()));
    responses.push_back(framedSuccess(closeResponsePayload()));

    ScriptedTransport transport(std::move(responses));
    smb::native_smb::NativeSmbSessionConfig config;
    config.treeId = 77;
    config.sessionId = 34;
    smb::native_smb::NativeSmbSession session(transport, config);

    std::vector<smb::native_smb::TransferProgress> progress;
    smb::native_smb::OperationContext context;
    context.progressCallback =
        [&progress](const smb::native_smb::TransferProgress &event) {
          progress.push_back(event);
        };

    const auto copied = session.copyFileServerSide(
        "source.bin", "target.bin", static_cast<std::uint64_t>(5) * chunkSize,
        context);

    QVERIFY(copied.ok);
    QCOMPARE(progress.size(), std::size_t{5});
    QCOMPARE(progress.back().bytesTransferred,
             static_cast<std::uint64_t>(5) * chunkSize);
    QCOMPARE(progress.back().totalBytes,
             static_cast<std::uint64_t>(5) * chunkSize);
    QCOMPARE(transport.requestFrames.size(), std::size_t{10});
    QCOMPARE(static_cast<int>(requestHeader(transport.requestFrames[2]).command),
             static_cast<int>(smb::native_smb::Command::Ioctl));
    QCOMPARE(static_cast<int>(requestHeader(transport.requestFrames[7]).command),
             static_cast<int>(smb::native_smb::Command::Ioctl));
  }

  void cancellationStopsServerSideCopyBetweenChunks() {
    constexpr std::uint32_t chunkSize = 1024 * 1024;
    ScriptedTransport transport({
        framedSuccess(createResponsePayload()),
        framedSuccess(createResponsePayload()),
        framedSuccess(ioctlResponsePayload(
            smb::native_smb::kFsctlSrvRequestResumeKey, resumeKeyOutput())),
        framedSuccess(ioctlResponsePayload(
            smb::native_smb::kFsctlSrvCopyChunk,
            copyChunkResponseOutput(chunkSize))),
        framedSuccess(closeResponsePayload()),
        framedSuccess(closeResponsePayload()),
    });
    smb::native_smb::NativeSmbSessionConfig config;
    config.treeId = 77;
    config.sessionId = 34;
    smb::native_smb::NativeSmbSession session(transport, config);

    smb::native_smb::CancellationToken token;
    smb::native_smb::OperationContext context;
    context.cancellationToken = &token;
    context.progressCallback =
        [&token](const smb::native_smb::TransferProgress &) {
          token.cancel();
        };

    const auto copied = session.copyFileServerSide(
        "source.bin", "target.bin", static_cast<std::uint64_t>(2) * chunkSize,
        context);

    QVERIFY(!copied.ok);
    QCOMPARE(static_cast<int>(copied.error.code),
             static_cast<int>(smb::native_smb::ErrorCode::Cancelled));
    QCOMPARE(transport.requestFrames.size(), std::size_t{6});
    QCOMPARE(static_cast<int>(requestHeader(transport.requestFrames[4]).command),
             static_cast<int>(smb::native_smb::Command::Close));
  }
};

QTEST_MAIN(NativeSmbPerfStressTest)

#include "test_native_smb_perf_stress.moc"
