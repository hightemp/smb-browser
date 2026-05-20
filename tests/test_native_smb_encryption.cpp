#include "Smb3Encryption.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

namespace {

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

smb::native_smb::ByteVector smb2Frame(smb::native_smb::Command command,
                                      std::uint64_t messageId,
                                      std::uint64_t sessionId) {
  smb::native_smb::Smb2SyncHeader header;
  header.command = command;
  header.messageId = messageId;
  header.sessionId = sessionId;
  return smb::native_smb::encodeDirectTcpFrame(
      smb::native_smb::encodeSmb2SyncHeader(header));
}

std::uint32_t readU32Le(const smb::native_smb::ByteVector &bytes,
                        std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

class ScriptedEncryptedServerTransport final
    : public smb::native_smb::Transport {
public:
  ScriptedEncryptedServerTransport(smb::native_smb::ByteVector requestKey,
                                   smb::native_smb::ByteVector responseKey,
                                   std::uint64_t sessionId,
                                   smb::native_smb::ByteVector response)
      : m_requestKey(std::move(requestKey)),
        m_responseKey(std::move(responseKey)), m_sessionId(sessionId),
        m_response(std::move(response)) {}

  smb::native_smb::DecodeResult<smb::native_smb::ByteVector>
  exchange(const smb::native_smb::ByteVector &requestFrame,
           const smb::native_smb::OperationContext &) override {
    auto decrypted = smb::native_smb::decryptSmb3DirectTcpFrame(
        requestFrame, m_requestKey, m_sessionId,
        smb::native_smb::Dialect::Smb302);
    if (!decrypted.ok) {
      return decrypted;
    }
    lastPlainRequest = decrypted.value;

    smb::native_smb::Nonce16 nonce{};
    nonce[0] = 0x55;
    return smb::native_smb::encryptSmb3DirectTcpFrame(
        m_response, m_responseKey, m_sessionId, nonce,
        smb::native_smb::Dialect::Smb302);
  }

  smb::native_smb::ByteVector lastPlainRequest;

private:
  smb::native_smb::ByteVector m_requestKey;
  smb::native_smb::ByteVector m_responseKey;
  std::uint64_t m_sessionId = 0;
  smb::native_smb::ByteVector m_response;
};

} // namespace

class NativeSmbEncryptionTest final : public QObject {
  Q_OBJECT

private slots:
  void computesRfc3610AesCcmVector() {
    const auto key = hexBytes("c0c1c2c3c4c5c6c7c8c9cacbcccdcecf");
    const auto nonce = hexBytes("00000003020100a0a1a2a3a4a5");
    const auto aad = hexBytes("0001020304050607");
    const auto plaintext = hexBytes(
        "08090a0b0c0d0e0f101112131415161718191a1b1c1d1e");

    const auto encrypted =
        smb::native_smb::aes128CcmEncrypt(key, nonce, plaintext, aad, 8);

    QVERIFY(encrypted.ok);
    QCOMPARE(encrypted.value.ciphertext,
             hexBytes("588c979a61c663d2f066d0c2c0f989806d5f6b61dac384"));
    QCOMPARE(encrypted.value.tag, hexBytes("17e8d12cfdf926e0"));

    const auto decrypted = smb::native_smb::aes128CcmDecrypt(
        key, nonce, encrypted.value.ciphertext, aad, encrypted.value.tag);
    QVERIFY(decrypted.ok);
    QCOMPARE(decrypted.value, plaintext);
  }

  void encryptsAndDecryptsSmb3TransformFrame() {
    const smb::native_smb::ByteVector key(16, 0x42);
    smb::native_smb::Nonce16 nonce{};
    std::copy_n(hexBytes("0102030405060708090a0b").begin(), 11,
                nonce.begin());
    const auto frame = smb2Frame(smb::native_smb::Command::Read, 7,
                                 0x0102030405060708ULL);

    const auto encrypted = smb::native_smb::encryptSmb3DirectTcpFrame(
        frame, key, 0x0102030405060708ULL, nonce,
        smb::native_smb::Dialect::Smb302);

    QVERIFY(encrypted.ok);
    const auto transformPayload =
        smb::native_smb::decodeDirectTcpPayload(encrypted.value);
    QVERIFY(transformPayload.ok);
    QCOMPARE(readU32Le(transformPayload.value, 0), 0x424D53FDu);

    const auto decrypted = smb::native_smb::decryptSmb3DirectTcpFrame(
        encrypted.value, key, 0x0102030405060708ULL,
        smb::native_smb::Dialect::Smb302);
    QVERIFY(decrypted.ok);
    QCOMPARE(decrypted.value, frame);
  }

  void encryptionTransportEncryptsRequestAndDecryptsResponse() {
    const auto sessionKey = hexBytes("000102030405060708090a0b0c0d0e0f");
    const auto clientToServer = smb::native_smb::deriveSmb3EncryptionKey(
        sessionKey, smb::native_smb::Dialect::Smb302,
        smb::native_smb::Smb3KeyDirection::ClientToServer);
    const auto serverToClient = smb::native_smb::deriveSmb3EncryptionKey(
        sessionKey, smb::native_smb::Dialect::Smb302,
        smb::native_smb::Smb3KeyDirection::ServerToClient);
    QVERIFY(clientToServer.ok);
    QVERIFY(serverToClient.ok);

    const auto sessionId = 0x0102030405060708ULL;
    const auto request =
        smb2Frame(smb::native_smb::Command::QueryInfo, 11, sessionId);
    const auto response =
        smb2Frame(smb::native_smb::Command::QueryInfo, 11, sessionId);
    auto inner = std::make_unique<ScriptedEncryptedServerTransport>(
        clientToServer.value, serverToClient.value, sessionId, response);
    auto *innerPtr = inner.get();
    smb::native_smb::Smb3EncryptionTransport transport(
        std::move(inner), clientToServer.value, serverToClient.value, sessionId,
        smb::native_smb::Dialect::Smb302);

    const auto result = transport.exchange(request, {});

    QVERIFY(result.ok);
    QCOMPARE(result.value, response);
    QCOMPARE(innerPtr->lastPlainRequest, request);
  }
};

QTEST_MAIN(NativeSmbEncryptionTest)

#include "test_native_smb_encryption.moc"
