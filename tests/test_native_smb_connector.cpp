#include "NativeSmbConnector.h"
#include "Smb2Signing.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>

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
    auto response = std::move(m_responses.front());
    m_responses.pop_front();
    return response;
  }

  std::vector<smb::native_smb::ByteVector> requestFrames;

private:
  std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>
      m_responses;
};

class ScriptedTokenProvider final
    : public smb::native_smb::SessionSetupTokenProvider {
public:
  smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
  initialToken(const smb::native_smb::NegotiatedConnection &,
               const smb::native_smb::ConnectionConfig &) override {
    ++initialCalls;
    if (failInitial) {
      return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::
          failure(smb::native_smb::ErrorCode::AuthenticationFailed,
                  "Synthetic auth failure.");
    }
    return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
        smb::native_smb::ByteVector{'i'});
  }

  smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
  nextToken(const smb::native_smb::SessionSetupResponse &challenge,
            const smb::native_smb::ConnectionConfig &) override {
    ++nextCalls;
    lastChallenge = challenge.securityBuffer;
    return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
        smb::native_smb::ByteVector{'n'});
  }

  smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
  sessionBaseKey() const override {
    return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::
        success(sessionKey);
  }

  int initialCalls = 0;
  int nextCalls = 0;
  bool failInitial = false;
  smb::native_smb::ByteVector sessionKey = smb::native_smb::ByteVector(16, 0x42);
  smb::native_smb::ByteVector lastChallenge;
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
    smb::native_smb::Dialect dialect = smb::native_smb::Dialect::Smb302,
    std::uint16_t securityMode = 0x0001) {
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
  appendU32Le(bytes, smb::native_smb::capabilityMask(
                         {smb::native_smb::GlobalCapability::Dfs}));
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

smb::native_smb::DecodeResult<smb::native_smb::ByteVector> signedFrame(
    const smb::native_smb::ByteVector &payload,
    const smb::native_smb::ByteVector &sessionKey,
    smb::native_smb::Dialect dialect) {
  return smb::native_smb::signSmb2DirectTcpFrame(
      smb::native_smb::encodeDirectTcpFrame(payload), sessionKey, dialect);
}

smb::native_smb::ByteVector sessionSetupResponsePayload(
    std::uint32_t status, std::uint64_t sessionId,
    smb::native_smb::ByteVector securityBuffer,
    std::uint16_t sessionFlags = 0) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::SessionSetup;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.status = status;
  header.sessionId = sessionId;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kSessionSetupResponseStructureSize);
  appendU16Le(bytes, sessionFlags);
  appendU16Le(bytes, securityBuffer.empty() ? 0 : 72);
  appendU16Le(bytes, static_cast<std::uint16_t>(securityBuffer.size()));
  bytes.insert(bytes.end(), securityBuffer.begin(), securityBuffer.end());
  return bytes;
}

smb::native_smb::ByteVector treeConnectResponsePayload(
    std::uint32_t shareFlags = smb::native_smb::kShareFlagDfs,
    std::uint32_t capabilities = smb::native_smb::kShareCapabilityDfs) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = smb::native_smb::Command::TreeConnect;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 0x0102030405060708ULL;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, smb::native_smb::kTreeConnectResponseStructureSize);
  bytes.push_back(static_cast<std::uint8_t>(
      smb::native_smb::ShareType::Disk));
  bytes.push_back(0);
  appendU32Le(bytes, shareFlags);
  appendU32Le(bytes, capabilities);
  appendU32Le(bytes, 0x001F01FF);
  return bytes;
}

smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
preauthHashForSmb311ConnectorFlow(
    const smb::native_smb::NativeSmbConnectorOptions &options,
    const smb::native_smb::ByteVector &negotiateResponse,
    const smb::native_smb::ByteVector &challengeResponse) {
  smb::native_smb::NegotiateRequestOptions negotiateOptions =
      options.negotiateOptions;
  negotiateOptions.signing = options.config.signing;
  negotiateOptions.capabilities |= smb::native_smb::capabilityMask(
      {smb::native_smb::GlobalCapability::Dfs,
       smb::native_smb::GlobalCapability::LargeMtu});
  if (options.config.encryption != smb::native_smb::SecurityPolicy::Disabled) {
    negotiateOptions.capabilities |= smb::native_smb::capabilityMask(
        {smb::native_smb::GlobalCapability::Encryption});
  }

  auto hash = smb::native_smb::initialSmb311PreauthHash();
  auto updated = smb::native_smb::updateSmb311PreauthHash(
      hash, smb::native_smb::buildNegotiateRequest(negotiateOptions, 0));
  if (!updated.ok) {
    return updated;
  }
  updated = smb::native_smb::updateSmb311PreauthHash(updated.value,
                                                     negotiateResponse);
  if (!updated.ok) {
    return updated;
  }

  smb::native_smb::SessionSetupRequestOptions firstSetup;
  firstSetup.signing = options.config.signing;
  firstSetup.capabilities = smb::native_smb::capabilityMask(
      {smb::native_smb::GlobalCapability::Dfs,
       smb::native_smb::GlobalCapability::LargeMtu,
       smb::native_smb::GlobalCapability::Encryption});
  firstSetup.securityBuffer = {'i'};
  updated = smb::native_smb::updateSmb311PreauthHash(
      updated.value,
      smb::native_smb::buildSessionSetupRequest(
          firstSetup, options.firstSessionMessageId, 0));
  if (!updated.ok) {
    return updated;
  }
  updated = smb::native_smb::updateSmb311PreauthHash(updated.value,
                                                     challengeResponse);
  if (!updated.ok) {
    return updated;
  }

  smb::native_smb::SessionSetupRequestOptions secondSetup = firstSetup;
  secondSetup.securityBuffer = {'n'};
  updated = smb::native_smb::updateSmb311PreauthHash(
      updated.value,
      smb::native_smb::buildSessionSetupRequest(
          secondSetup, options.firstSessionMessageId + 1,
          0x0102030405060708ULL));
  if (!updated.ok) {
    return updated;
  }
  return updated;
}

smb::native_smb::ByteVector emptyStructureResponsePayload(
    smb::native_smb::Command command, std::uint16_t structureSize) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.flags = smb::native_smb::kFlagServerToRedir;
  header.treeId = 77;
  header.sessionId = 0x0102030405060708ULL;

  auto bytes = smb::native_smb::encodeSmb2SyncHeader(header);
  appendU16Le(bytes, structureSize);
  appendU16Le(bytes, 0);
  return bytes;
}

smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
framedSuccess(const smb::native_smb::ByteVector &payload) {
  return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
      smb::native_smb::encodeDirectTcpFrame(payload));
}

smb::native_smb::Smb2SyncHeader requestHeader(
    const smb::native_smb::ByteVector &frame) {
  const auto payload = smb::native_smb::decodeDirectTcpPayload(frame);
  Q_ASSERT(payload.ok);
  const auto header = smb::native_smb::decodeSmb2SyncHeader(payload.value);
  Q_ASSERT(header.ok);
  return header.value;
}

} // namespace

class NativeSmbConnectorTest final : public QObject {
  Q_OBJECT

private slots:
  void performsNegotiateSessionSetupAndTreeConnect() {
    auto transport = std::make_unique<ScriptedTransport>(
        std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>{
            framedSuccess(negotiateResponsePayload()),
            framedSuccess(sessionSetupResponsePayload(
                smb::native_smb::kStatusMoreProcessingRequired,
                0x0102030405060708ULL, {'c'})),
            framedSuccess(sessionSetupResponsePayload(
                smb::native_smb::kStatusSuccess, 0x0102030405060708ULL, {})),
            framedSuccess(treeConnectResponsePayload()),
            framedSuccess(emptyStructureResponsePayload(
                smb::native_smb::Command::TreeDisconnect,
                smb::native_smb::kTreeDisconnectResponseStructureSize)),
            framedSuccess(emptyStructureResponsePayload(
                smb::native_smb::Command::Logoff,
                smb::native_smb::kLogoffResponseStructureSize)),
        });
    auto *transportPtr = transport.get();
    ScriptedTokenProvider tokenProvider;

    smb::native_smb::NativeSmbConnectorOptions options;
    options.config.server = "server";
    options.config.share = "share";
    options.config.signing = smb::native_smb::SecurityPolicy::Disabled;
    options.firstSessionMessageId = 10;

    const smb::native_smb::NativeSmbConnector connector;
    auto result =
        connector.connect(std::move(transport), tokenProvider, options, {});

    QVERIFY(result.ok);
    QVERIFY(result.value.connection != nullptr);
    QCOMPARE(tokenProvider.initialCalls, 1);
    QCOMPARE(tokenProvider.nextCalls, 1);
    QCOMPARE(tokenProvider.lastChallenge.size(), std::size_t{1});
    QCOMPARE(result.value.session.sessionId,
             std::uint64_t{0x0102030405060708ULL});
    QCOMPARE(result.value.tree.treeId, std::uint32_t{77});
    QVERIFY(result.value.tree.isDfs);
    QCOMPARE(result.value.connection->session().nextMessageIdForTests(),
             std::uint64_t{13});

    QCOMPARE(transportPtr->requestFrames.size(), std::size_t{4});
    QCOMPARE(static_cast<int>(requestHeader(transportPtr->requestFrames[0]).command),
             static_cast<int>(smb::native_smb::Command::Negotiate));
    QCOMPARE(static_cast<int>(requestHeader(transportPtr->requestFrames[1]).command),
             static_cast<int>(smb::native_smb::Command::SessionSetup));
    QCOMPARE(static_cast<int>(requestHeader(transportPtr->requestFrames[3]).command),
             static_cast<int>(smb::native_smb::Command::TreeConnect));
    QCOMPARE(requestHeader(transportPtr->requestFrames[1]).messageId,
             std::uint64_t{10});
    QCOMPARE(requestHeader(transportPtr->requestFrames[2]).messageId,
             std::uint64_t{11});
    QCOMPARE(requestHeader(transportPtr->requestFrames[3]).messageId,
             std::uint64_t{12});

    const auto disconnected = result.value.connection->disconnect({});
    QVERIFY(disconnected.ok);
    QCOMPARE(transportPtr->requestFrames.size(), std::size_t{6});
    QCOMPARE(static_cast<int>(requestHeader(transportPtr->requestFrames[4]).command),
             static_cast<int>(smb::native_smb::Command::TreeDisconnect));
    QCOMPARE(static_cast<int>(requestHeader(transportPtr->requestFrames[5]).command),
             static_cast<int>(smb::native_smb::Command::Logoff));
    QCOMPARE(requestHeader(transportPtr->requestFrames[4]).messageId,
             std::uint64_t{13});
    QCOMPARE(requestHeader(transportPtr->requestFrames[5]).messageId,
             std::uint64_t{14});
  }

  void signsTreeConnectWhenSigningIsRequired() {
    ScriptedTokenProvider tokenProvider;
    const auto signedTreeResponse =
        signedFrame(treeConnectResponsePayload(), tokenProvider.sessionKey,
                    smb::native_smb::Dialect::Smb210);
    QVERIFY(signedTreeResponse.ok);
    const auto finalSessionResponse = signedFrame(
        sessionSetupResponsePayload(smb::native_smb::kStatusSuccess,
                                    0x0102030405060708ULL, {}),
        tokenProvider.sessionKey, smb::native_smb::Dialect::Smb210);
    QVERIFY(finalSessionResponse.ok);

    auto transport = std::make_unique<ScriptedTransport>(
        std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>{
            framedSuccess(negotiateResponsePayload(
                smb::native_smb::Dialect::Smb210, 0x0003)),
            framedSuccess(sessionSetupResponsePayload(
                smb::native_smb::kStatusMoreProcessingRequired,
                0x0102030405060708ULL, {'c'})),
            smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::
                success(finalSessionResponse.value),
            smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::
                success(signedTreeResponse.value),
        });
    auto *transportPtr = transport.get();

    smb::native_smb::NativeSmbConnectorOptions options;
    options.config.server = "server";
    options.config.share = "share";
    options.config.signing = smb::native_smb::SecurityPolicy::Required;
    options.firstSessionMessageId = 10;

    const smb::native_smb::NativeSmbConnector connector;
    auto result =
        connector.connect(std::move(transport), tokenProvider, options, {});

    QVERIFY(result.ok);
    QCOMPARE(transportPtr->requestFrames.size(), std::size_t{4});
    const auto treeHeader = requestHeader(transportPtr->requestFrames[3]);
    QCOMPARE(static_cast<int>(treeHeader.command),
             static_cast<int>(smb::native_smb::Command::TreeConnect));
    QVERIFY((treeHeader.flags & smb::native_smb::kFlagSigned) != 0);
    QVERIFY(std::any_of(treeHeader.signature.begin(),
                       treeHeader.signature.end(),
                       [](std::uint8_t byte) { return byte != 0; }));
  }

  void signsTreeConnectWhenSigningIsPreferred() {
    ScriptedTokenProvider tokenProvider;
    const auto signedTreeResponse =
        signedFrame(treeConnectResponsePayload(), tokenProvider.sessionKey,
                    smb::native_smb::Dialect::Smb210);
    QVERIFY(signedTreeResponse.ok);

    auto transport = std::make_unique<ScriptedTransport>(
        std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>{
            framedSuccess(negotiateResponsePayload(
                smb::native_smb::Dialect::Smb210, 0x0001)),
            framedSuccess(sessionSetupResponsePayload(
                smb::native_smb::kStatusMoreProcessingRequired,
                0x0102030405060708ULL, {'c'})),
            framedSuccess(sessionSetupResponsePayload(
                smb::native_smb::kStatusSuccess, 0x0102030405060708ULL, {})),
            smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::
                success(signedTreeResponse.value),
        });
    auto *transportPtr = transport.get();

    smb::native_smb::NativeSmbConnectorOptions options;
    options.config.server = "server";
    options.config.share = "share";
    options.config.signing = smb::native_smb::SecurityPolicy::Preferred;
    options.firstSessionMessageId = 10;

    const smb::native_smb::NativeSmbConnector connector;
    auto result =
        connector.connect(std::move(transport), tokenProvider, options, {});

    QVERIFY(result.ok);
    QCOMPARE(transportPtr->requestFrames.size(), std::size_t{4});
    const auto treeHeader = requestHeader(transportPtr->requestFrames[3]);
    QCOMPARE(static_cast<int>(treeHeader.command),
             static_cast<int>(smb::native_smb::Command::TreeConnect));
    QVERIFY((treeHeader.flags & smb::native_smb::kFlagSigned) != 0);
    QVERIFY(std::any_of(treeHeader.signature.begin(),
                       treeHeader.signature.end(),
                       [](std::uint8_t byte) { return byte != 0; }));
  }

  void derivesSmb3SigningKeyWhenSigningIsRequired() {
    ScriptedTokenProvider tokenProvider;
    const auto signingKey = smb::native_smb::deriveSmb3SigningKey(
        tokenProvider.sessionKey, smb::native_smb::Dialect::Smb302);
    QVERIFY(signingKey.ok);
    const auto signedTreeResponse =
        signedFrame(treeConnectResponsePayload(), signingKey.value,
                    smb::native_smb::Dialect::Smb302);
    QVERIFY(signedTreeResponse.ok);
    const auto finalSessionResponse = signedFrame(
        sessionSetupResponsePayload(smb::native_smb::kStatusSuccess,
                                    0x0102030405060708ULL, {}),
        signingKey.value, smb::native_smb::Dialect::Smb302);
    QVERIFY(finalSessionResponse.ok);

    auto transport = std::make_unique<ScriptedTransport>(
        std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>{
            framedSuccess(negotiateResponsePayload(
                smb::native_smb::Dialect::Smb302, 0x0003)),
            framedSuccess(sessionSetupResponsePayload(
                smb::native_smb::kStatusMoreProcessingRequired,
                0x0102030405060708ULL, {'c'})),
            smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::
                success(finalSessionResponse.value),
            smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::
                success(signedTreeResponse.value),
        });
    auto *transportPtr = transport.get();

    smb::native_smb::NativeSmbConnectorOptions options;
    options.config.server = "server";
    options.config.share = "share";
    options.config.signing = smb::native_smb::SecurityPolicy::Required;
    options.firstSessionMessageId = 10;

    const smb::native_smb::NativeSmbConnector connector;
    auto result =
        connector.connect(std::move(transport), tokenProvider, options, {});

    QVERIFY(result.ok);
    QCOMPARE(transportPtr->requestFrames.size(), std::size_t{4});
    const auto treeHeader = requestHeader(transportPtr->requestFrames[3]);
    QCOMPARE(static_cast<int>(treeHeader.command),
             static_cast<int>(smb::native_smb::Command::TreeConnect));
    QVERIFY((treeHeader.flags & smb::native_smb::kFlagSigned) != 0);
    QVERIFY(std::any_of(treeHeader.signature.begin(),
                       treeHeader.signature.end(),
                       [](std::uint8_t byte) { return byte != 0; }));
  }

  void derivesSmb311SigningKeyWhenSigningIsRequired() {
    ScriptedTokenProvider tokenProvider;

    smb::native_smb::NativeSmbConnectorOptions options;
    options.config.server = "server";
    options.config.share = "share";
    options.config.signing = smb::native_smb::SecurityPolicy::Required;
    options.firstSessionMessageId = 10;

    const auto negotiateResponse =
        negotiateResponsePayload(smb::native_smb::Dialect::Smb311, 0x0003);
    const auto challengeResponse = sessionSetupResponsePayload(
        smb::native_smb::kStatusMoreProcessingRequired,
        0x0102030405060708ULL, {'c'});
    const auto finalSessionResponse = sessionSetupResponsePayload(
        smb::native_smb::kStatusSuccess, 0x0102030405060708ULL, {});
    const auto preauthHash = preauthHashForSmb311ConnectorFlow(
        options, negotiateResponse, challengeResponse);
    QVERIFY(preauthHash.ok);
    const auto signingKey = smb::native_smb::deriveSmb311SigningKey(
        tokenProvider.sessionKey, preauthHash.value);
    QVERIFY(signingKey.ok);
    const auto signedTreeResponse =
        signedFrame(treeConnectResponsePayload(), signingKey.value,
                    smb::native_smb::Dialect::Smb311);
    QVERIFY(signedTreeResponse.ok);
    const auto signedFinalSessionResponse = signedFrame(
        finalSessionResponse, signingKey.value, smb::native_smb::Dialect::Smb311);
    QVERIFY(signedFinalSessionResponse.ok);

    auto transport = std::make_unique<ScriptedTransport>(
        std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>{
            framedSuccess(negotiateResponse),
            framedSuccess(challengeResponse),
            smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::
                success(signedFinalSessionResponse.value),
            smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::
                success(signedTreeResponse.value),
        });
    auto *transportPtr = transport.get();

    const smb::native_smb::NativeSmbConnector connector;
    auto result =
        connector.connect(std::move(transport), tokenProvider, options, {});

    QVERIFY(result.ok);
    QCOMPARE(transportPtr->requestFrames.size(), std::size_t{4});
    QCOMPARE(static_cast<int>(result.value.negotiated.dialect),
             static_cast<int>(smb::native_smb::Dialect::Smb311));
    const auto treeHeader = requestHeader(transportPtr->requestFrames[3]);
    QCOMPARE(static_cast<int>(treeHeader.command),
             static_cast<int>(smb::native_smb::Command::TreeConnect));
    QVERIFY((treeHeader.flags & smb::native_smb::kFlagSigned) != 0);
    QVERIFY(std::any_of(treeHeader.signature.begin(),
                       treeHeader.signature.end(),
                       [](std::uint8_t byte) { return byte != 0; }));
  }

  void rejectsRequiredEncryptionPolicyUntilEncryptionIsImplemented() {
    auto transport = std::make_unique<ScriptedTransport>(
        std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>{
            framedSuccess(negotiateResponsePayload()),
        });
    ScriptedTokenProvider tokenProvider;

    smb::native_smb::NativeSmbConnectorOptions options;
    options.config.server = "server";
    options.config.share = "share";
    options.config.encryption = smb::native_smb::SecurityPolicy::Required;

    const smb::native_smb::NativeSmbConnector connector;
    auto result =
        connector.connect(std::move(transport), tokenProvider, options, {});

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::UnsupportedCapability));
  }

  void rejectsServerRequiredSessionEncryptionUntilImplemented() {
    auto transport = std::make_unique<ScriptedTransport>(
        std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>{
            framedSuccess(negotiateResponsePayload()),
            framedSuccess(sessionSetupResponsePayload(
                smb::native_smb::kStatusSuccess, 0x0102030405060708ULL, {},
                smb::native_smb::kSessionFlagEncryptData)),
        });
    ScriptedTokenProvider tokenProvider;

    smb::native_smb::NativeSmbConnectorOptions options;
    options.config.server = "server";
    options.config.share = "share";
    options.config.encryption = smb::native_smb::SecurityPolicy::Preferred;
    options.config.signing = smb::native_smb::SecurityPolicy::Disabled;

    const smb::native_smb::NativeSmbConnector connector;
    auto result =
        connector.connect(std::move(transport), tokenProvider, options, {});

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::UnsupportedCapability));
  }

  void rejectsShareRequiredEncryptionUntilImplemented() {
    auto transport = std::make_unique<ScriptedTransport>(
        std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>{
            framedSuccess(negotiateResponsePayload()),
            framedSuccess(sessionSetupResponsePayload(
                smb::native_smb::kStatusSuccess, 0x0102030405060708ULL, {})),
            framedSuccess(treeConnectResponsePayload(
                smb::native_smb::kShareFlagDfs |
                    smb::native_smb::kShareFlagEncryptData,
                smb::native_smb::kShareCapabilityDfs)),
        });
    ScriptedTokenProvider tokenProvider;

    smb::native_smb::NativeSmbConnectorOptions options;
    options.config.server = "server";
    options.config.share = "share";
    options.config.encryption = smb::native_smb::SecurityPolicy::Preferred;
    options.config.signing = smb::native_smb::SecurityPolicy::Disabled;

    const smb::native_smb::NativeSmbConnector connector;
    auto result =
        connector.connect(std::move(transport), tokenProvider, options, {});

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::UnsupportedCapability));
  }

  void returnsTokenProviderFailure() {
    auto transport = std::make_unique<ScriptedTransport>(
        std::deque<smb::native_smb::DecodeResult<smb::native_smb::ByteVector>>{
            framedSuccess(negotiateResponsePayload()),
        });
    ScriptedTokenProvider tokenProvider;
    tokenProvider.failInitial = true;

    smb::native_smb::NativeSmbConnectorOptions options;
    options.config.server = "server";
    options.config.share = "share";

    const smb::native_smb::NativeSmbConnector connector;
    auto result =
        connector.connect(std::move(transport), tokenProvider, options, {});

    QVERIFY(!result.ok);
    QCOMPARE(static_cast<int>(result.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::AuthenticationFailed));
    QCOMPARE(tokenProvider.initialCalls, 1);
    QCOMPARE(tokenProvider.nextCalls, 0);
  }
};

QTEST_MAIN(NativeSmbConnectorTest)

#include "test_native_smb_connector.moc"
