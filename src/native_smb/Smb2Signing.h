#pragma once

#include "Transport.h"

#include <array>
#include <memory>

namespace smb::native_smb {

using Digest32 = std::array<std::uint8_t, 32>;
using Block16 = std::array<std::uint8_t, 16>;

Digest32 sha256(const ByteVector &data);
Digest32 hmacSha256(const ByteVector &key, const ByteVector &data);
Block16 aes128EncryptBlock(const Block16 &key, const Block16 &plaintext);
DecodeResult<Block16> aes128Cmac(const ByteVector &key, const ByteVector &data);
DecodeResult<ByteVector> deriveSmb3SigningKey(const ByteVector &sessionKey,
                                              Dialect dialect);
ByteVector initialSmb311PreauthHash();
DecodeResult<ByteVector> updateSmb311PreauthHash(const ByteVector &currentHash,
                                                 const ByteVector &message);
DecodeResult<ByteVector>
deriveSmb311SigningKey(const ByteVector &sessionKey,
                       const ByteVector &preauthIntegrityHash);
bool supportsHmacSha256Signing(Dialect dialect);
bool supportsAesCmacSigning(Dialect dialect);
bool supportsSigning(Dialect dialect);
DecodeResult<ByteVector> signSmb2DirectTcpFrame(const ByteVector &requestFrame,
                                                const ByteVector &sessionKey,
                                                Dialect dialect);
DecodeResult<bool> verifySmb2DirectTcpFrameSignature(
    const ByteVector &responseFrame, const ByteVector &sessionKey,
    Dialect dialect);
std::string toHex(const Digest32 &digest);
std::string toHex16(const Block16 &digest);

class SigningTransport final : public Transport {
public:
  SigningTransport(std::unique_ptr<Transport> inner, ByteVector sessionKey,
                   Dialect dialect, bool verifyResponses = true);

  DecodeResult<ByteVector>
  exchange(const ByteVector &requestFrame,
           const OperationContext &context) override;

private:
  std::unique_ptr<Transport> m_inner;
  ByteVector m_sessionKey;
  Dialect m_dialect;
  bool m_verifyResponses = true;
};

} // namespace smb::native_smb
