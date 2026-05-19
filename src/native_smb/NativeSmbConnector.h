#pragma once

#include "NativeSmbConnection.h"
#include "Negotiator.h"
#include "SessionSetupExchanger.h"
#include "TreeConnector.h"

#include <memory>

namespace smb::native_smb {

class SessionSetupTokenProvider {
public:
  virtual ~SessionSetupTokenProvider() = default;

  virtual DecodeResult<ByteVector>
  initialToken(const NegotiatedConnection &negotiated,
               const ConnectionConfig &config) = 0;

  virtual DecodeResult<ByteVector>
  nextToken(const SessionSetupResponse &challenge,
            const ConnectionConfig &config) = 0;

  virtual DecodeResult<ByteVector> sessionBaseKey() const {
    return DecodeResult<ByteVector>::failure(
        ErrorCode::UnsupportedCapability,
        "Session setup token provider does not expose a session key.");
  }
};

struct NativeSmbConnectorOptions {
  ConnectionConfig config;
  NegotiateRequestOptions negotiateOptions;
  std::uint64_t firstSessionMessageId = 1;
  std::uint16_t maxSessionSetupRounds = 8;
};

struct NativeSmbConnectedState {
  std::unique_ptr<NativeSmbConnection> connection;
  NegotiatedConnection negotiated;
  SessionSetupResponse session;
  TreeConnectResult tree;
};

class NativeSmbConnector {
public:
  DecodeResult<NativeSmbConnectedState>
  connect(std::unique_ptr<Transport> transport,
          SessionSetupTokenProvider &tokenProvider,
          const NativeSmbConnectorOptions &options,
          const OperationContext &context) const;
};

} // namespace smb::native_smb
