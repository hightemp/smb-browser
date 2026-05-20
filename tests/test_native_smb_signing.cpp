#include "Smb2Signing.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <utility>

namespace {

smb::native_smb::ByteVector bytes(const char *text) {
  const auto *begin = reinterpret_cast<const std::uint8_t *>(text);
  return smb::native_smb::ByteVector(begin, begin + std::strlen(text));
}

int hexNibble(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  if (ch >= 'A' && ch <= 'F') {
    return ch - 'A' + 10;
  }
  return -1;
}

smb::native_smb::ByteVector hexBytes(const char *hex) {
  smb::native_smb::ByteVector result;
  int high = -1;
  while (*hex != '\0') {
    if (std::isspace(static_cast<unsigned char>(*hex)) != 0) {
      ++hex;
      continue;
    }

    const auto nibble = hexNibble(*hex++);
    Q_ASSERT(nibble >= 0);
    if (high < 0) {
      high = nibble;
      continue;
    }

    result.push_back(static_cast<std::uint8_t>((high << 4) | nibble));
    high = -1;
  }
  Q_ASSERT(high < 0);
  return result;
}

smb::native_smb::Block16 block16FromHex(const char *hex) {
  const auto bytes = hexBytes(hex);
  Q_ASSERT(bytes.size() == 16);
  smb::native_smb::Block16 block{};
  std::copy_n(bytes.begin(), block.size(), block.begin());
  return block;
}

std::uint32_t readU32Le(const smb::native_smb::ByteVector &bytes,
                        std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

smb::native_smb::ByteVector smb2Frame(smb::native_smb::Command command,
                                      std::uint64_t messageId) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.messageId = messageId;
  header.sessionId = 0x0102030405060708ULL;
  return smb::native_smb::encodeDirectTcpFrame(
      smb::native_smb::encodeSmb2SyncHeader(header));
}

bool hasNonZeroSignature(const smb::native_smb::ByteVector &payload) {
  for (std::size_t i = 48; i < 64; ++i) {
    if (payload[i] != 0) {
      return true;
    }
  }
  return false;
}

class ScriptedTransport final : public smb::native_smb::Transport {
public:
  explicit ScriptedTransport(smb::native_smb::ByteVector response)
      : m_response(std::move(response)) {}

  smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
  exchange(const smb::native_smb::ByteVector &requestFrame,
           const smb::native_smb::OperationContext &) override {
    lastRequest = requestFrame;
    return smb::native_smb::DecodeResult<smb::native_smb::ByteVector>::success(
        m_response);
  }

  smb::native_smb::ByteVector lastRequest;

private:
  smb::native_smb::ByteVector m_response;
};

} // namespace

class NativeSmbSigningTest final : public QObject {
  Q_OBJECT

private slots:
  void computesSha256Vectors() {
    QCOMPARE(QString::fromStdString(
                 smb::native_smb::toHex(smb::native_smb::sha256({}))),
             QStringLiteral("e3b0c44298fc1c149afbf4c8996fb924"
                            "27ae41e4649b934ca495991b7852b855"));
    QCOMPARE(QString::fromStdString(
                 smb::native_smb::toHex(smb::native_smb::sha256(bytes("abc")))),
             QStringLiteral("ba7816bf8f01cfea414140de5dae2223"
                            "b00361a396177a9cb410ff61f20015ad"));
  }

  void computesHmacSha256Vector() {
    const smb::native_smb::ByteVector key(20, 0x0B);
    QCOMPARE(QString::fromStdString(smb::native_smb::toHex(
                 smb::native_smb::hmacSha256(key, bytes("Hi There")))),
             QStringLiteral("b0344c61d8db38535ca8afceaf0bf12b"
                            "881dc200c9833da726e9376c2e32cff7"));
  }

  void computesAes128EncryptVector() {
    const auto key = block16FromHex("000102030405060708090a0b0c0d0e0f");
    const auto plaintext = block16FromHex("00112233445566778899aabbccddeeff");
    QCOMPARE(QString::fromStdString(smb::native_smb::toHex16(
                 smb::native_smb::aes128EncryptBlock(key, plaintext))),
             QStringLiteral("69c4e0d86a7b0430d8cdb78070b4c55a"));
  }

  void computesAesCmacVectors() {
    const auto key = hexBytes("2b7e151628aed2a6abf7158809cf4f3c");
    const auto empty = smb::native_smb::aes128Cmac(key, {});
    QVERIFY(empty.ok);
    QCOMPARE(QString::fromStdString(smb::native_smb::toHex16(empty.value)),
             QStringLiteral("bb1d6929e95937287fa37d129b756746"));

    const auto oneBlock = smb::native_smb::aes128Cmac(
        key, hexBytes("6bc1bee22e409f96e93d7e117393172a"));
    QVERIFY(oneBlock.ok);
    QCOMPARE(QString::fromStdString(smb::native_smb::toHex16(oneBlock.value)),
             QStringLiteral("070a16b46b4d4144f79bdd9dd04a287c"));
  }

  void derivesSmb3SigningKey() {
    const auto key = smb::native_smb::deriveSmb3SigningKey(
        hexBytes("000102030405060708090a0b0c0d0e0f"),
        smb::native_smb::Dialect::Smb302);

    QVERIFY(key.ok);
    QVERIFY(key.value == hexBytes("6234814cbb8ea9227440ebfeb5eacbe1"));
  }

  void derivesSmb311SigningKeyFromPreauthHash() {
    auto preauthHash = smb::native_smb::initialSmb311PreauthHash();
    const auto updated =
        smb::native_smb::updateSmb311PreauthHash(preauthHash, {'N', 'E', 'G'});
    QVERIFY(updated.ok);

    const auto key = smb::native_smb::deriveSmb311SigningKey(
        hexBytes("000102030405060708090a0b0c0d0e0f"), updated.value);

    QVERIFY(key.ok);
    QCOMPARE(key.value.size(), std::size_t{16});
    QVERIFY(key.value != smb::native_smb::ByteVector(16, 0));
  }

  void signsSmb2FrameWithHmacSha256() {
    const smb::native_smb::ByteVector sessionKey(16, 0x11);
    const auto signedFrame = smb::native_smb::signSmb2DirectTcpFrame(
        smb2Frame(smb::native_smb::Command::TreeConnect, 7), sessionKey,
        smb::native_smb::Dialect::Smb210);

    QVERIFY(signedFrame.ok);
    const auto payload =
        smb::native_smb::decodeDirectTcpPayload(signedFrame.value);
    QVERIFY(payload.ok);
    QVERIFY((readU32Le(payload.value, 16) & smb::native_smb::kFlagSigned) != 0);
    QVERIFY(hasNonZeroSignature(payload.value));

    const auto verified = smb::native_smb::verifySmb2DirectTcpFrameSignature(
        signedFrame.value, sessionKey, smb::native_smb::Dialect::Smb210);
    QVERIFY(verified.ok);
    QVERIFY(verified.value);
  }

  void signingTransportSignsRequestAndVerifiesResponse() {
    const smb::native_smb::ByteVector sessionKey(16, 0x22);
    const auto signedResponse = smb::native_smb::signSmb2DirectTcpFrame(
        smb2Frame(smb::native_smb::Command::TreeConnect, 9), sessionKey,
        smb::native_smb::Dialect::Smb210);
    QVERIFY(signedResponse.ok);

    auto inner = std::make_unique<ScriptedTransport>(signedResponse.value);
    auto *innerPtr = inner.get();
    smb::native_smb::SigningTransport transport(
        std::move(inner), sessionKey, smb::native_smb::Dialect::Smb210);

    const auto response = transport.exchange(
        smb2Frame(smb::native_smb::Command::TreeConnect, 8), {});

    QVERIFY(response.ok);
    const auto signedRequest =
        smb::native_smb::decodeDirectTcpPayload(innerPtr->lastRequest);
    QVERIFY(signedRequest.ok);
    QVERIFY(hasNonZeroSignature(signedRequest.value));
  }

  void signsSmb3FrameWithAesCmac() {
    const auto signingKey = smb::native_smb::deriveSmb3SigningKey(
        smb::native_smb::ByteVector(16, 0x33),
        smb::native_smb::Dialect::Smb302);
    QVERIFY(signingKey.ok);

    const auto signedFrame = smb::native_smb::signSmb2DirectTcpFrame(
        smb2Frame(smb::native_smb::Command::TreeConnect, 7), signingKey.value,
        smb::native_smb::Dialect::Smb302);

    QVERIFY(signedFrame.ok);
    const auto payload =
        smb::native_smb::decodeDirectTcpPayload(signedFrame.value);
    QVERIFY(payload.ok);
    QVERIFY((readU32Le(payload.value, 16) & smb::native_smb::kFlagSigned) != 0);
    QVERIFY(hasNonZeroSignature(payload.value));

    const auto verified = smb::native_smb::verifySmb2DirectTcpFrameSignature(
        signedFrame.value, signingKey.value, smb::native_smb::Dialect::Smb302);
    QVERIFY(verified.ok);
    QVERIFY(verified.value);
  }

  void signsSmb311FrameWithAesCmacSigningKey() {
    auto preauthHash = smb::native_smb::initialSmb311PreauthHash();
    auto updated =
        smb::native_smb::updateSmb311PreauthHash(preauthHash, {'n'});
    QVERIFY(updated.ok);
    updated = smb::native_smb::updateSmb311PreauthHash(updated.value, {'r'});
    QVERIFY(updated.ok);
    const auto signingKey = smb::native_smb::deriveSmb311SigningKey(
        smb::native_smb::ByteVector(16, 0x44), updated.value);
    QVERIFY(signingKey.ok);

    const auto signedFrame = smb::native_smb::signSmb2DirectTcpFrame(
        smb2Frame(smb::native_smb::Command::TreeConnect, 7), signingKey.value,
        smb::native_smb::Dialect::Smb311);

    QVERIFY(signedFrame.ok);
    const auto verified = smb::native_smb::verifySmb2DirectTcpFrameSignature(
        signedFrame.value, signingKey.value, smb::native_smb::Dialect::Smb311);
    QVERIFY(verified.ok);
    QVERIFY(verified.value);
  }
};

QTEST_MAIN(NativeSmbSigningTest)

#include "test_native_smb_signing.moc"
