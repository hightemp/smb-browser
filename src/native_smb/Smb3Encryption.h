#pragma once

#include "Transport.h"

#include <array>
#include <memory>

namespace smb::native_smb {

using Nonce16 = std::array<std::uint8_t, 16>;

enum class Smb3KeyDirection {
  ClientToServer,
  ServerToClient,
};

struct AesCcmEncryptedMessage {
  ByteVector ciphertext;
  ByteVector tag;
};

DecodeResult<AesCcmEncryptedMessage>
aes128CcmEncrypt(const ByteVector &key, const ByteVector &nonce,
                 const ByteVector &plaintext, const ByteVector &aad,
                 std::size_t tagLength);

DecodeResult<ByteVector> aes128CcmDecrypt(const ByteVector &key,
                                          const ByteVector &nonce,
                                          const ByteVector &ciphertext,
                                          const ByteVector &aad,
                                          const ByteVector &tag);

DecodeResult<ByteVector> deriveSmb3EncryptionKey(
    const ByteVector &sessionKey, Dialect dialect,
    Smb3KeyDirection direction);

DecodeResult<ByteVector> encryptSmb3DirectTcpFrame(
    const ByteVector &requestFrame, const ByteVector &encryptionKey,
    std::uint64_t sessionId, const Nonce16 &nonce, Dialect dialect);

DecodeResult<ByteVector>
decryptSmb3DirectTcpFrame(const ByteVector &responseFrame,
                          const ByteVector &decryptionKey,
                          std::uint64_t expectedSessionId, Dialect dialect);

class Smb3EncryptionTransport final : public Transport {
public:
  Smb3EncryptionTransport(std::unique_ptr<Transport> inner,
                          ByteVector encryptionKey, ByteVector decryptionKey,
                          std::uint64_t sessionId, Dialect dialect);

  DecodeResult<ByteVector>
  exchange(const ByteVector &requestFrame,
           const OperationContext &context) override;

private:
  Nonce16 nextNonce();

  std::unique_ptr<Transport> m_inner;
  ByteVector m_encryptionKey;
  ByteVector m_decryptionKey;
  std::uint64_t m_sessionId = 0;
  Dialect m_dialect = Dialect::Smb300;
  std::uint64_t m_nonceCounter = 1;
};

} // namespace smb::native_smb
