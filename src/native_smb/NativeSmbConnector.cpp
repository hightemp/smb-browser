#include "NativeSmbConnector.h"

#include "Smb2Signing.h"

#include <utility>

namespace smb::native_smb {
namespace {

DecodeResult<NativeSmbConnectedState> failureFrom(const ProtocolError &error) {
  return DecodeResult<NativeSmbConnectedState>::failure(error.code,
                                                        error.message);
}

DecodeResult<NativeSmbConnectedState> internalFailure(std::string message) {
  return DecodeResult<NativeSmbConnectedState>::failure(
      ErrorCode::InternalError, std::move(message));
}

DecodeResult<NativeSmbConnectedState>
unsupportedEncryption(std::string message) {
  return DecodeResult<NativeSmbConnectedState>::failure(
      ErrorCode::UnsupportedCapability, std::move(message));
}

NegotiateRequestOptions negotiateOptionsFrom(
    const NativeSmbConnectorOptions &options) {
  auto negotiate = options.negotiateOptions;
  negotiate.signing = options.config.signing;
  negotiate.capabilities |=
      capabilityMask({GlobalCapability::Dfs, GlobalCapability::LargeMtu});
  return negotiate;
}

SessionSetupRequestOptions sessionSetupOptionsFrom(
    const NativeSmbConnectorOptions &options, const ByteVector &token) {
  SessionSetupRequestOptions setup;
  setup.signing = options.config.signing;
  setup.capabilities =
      capabilityMask({GlobalCapability::Dfs, GlobalCapability::LargeMtu});
  setup.securityBuffer = token;
  return setup;
}

} // namespace

DecodeResult<NativeSmbConnectedState> NativeSmbConnector::connect(
    std::unique_ptr<Transport> transport, SessionSetupTokenProvider &tokenProvider,
    const NativeSmbConnectorOptions &options,
    const OperationContext &context) const {
  if (!transport) {
    return internalFailure("Native SMB connector requires a transport.");
  }

  const Negotiator negotiator;
  const auto negotiated =
      negotiator.negotiate(*transport, negotiateOptionsFrom(options), context);
  if (!negotiated.ok) {
    return failureFrom(negotiated.error);
  }
  if (options.config.encryption == SecurityPolicy::Required) {
    return unsupportedEncryption(
        "SMB encryption is required by policy, but clean-room SMB encryption is "
        "not implemented yet.");
  }

  auto token = tokenProvider.initialToken(negotiated.value, options.config);
  if (!token.ok) {
    return failureFrom(token.error);
  }

  const SessionSetupExchanger sessionSetup;
  SessionSetupResponse session;
  std::uint64_t sessionId = 0;
  auto messageId =
      options.firstSessionMessageId == 0 ? 1 : options.firstSessionMessageId;
  const auto maxRounds =
      options.maxSessionSetupRounds == 0 ? 1 : options.maxSessionSetupRounds;

  for (std::uint16_t round = 0; round < maxRounds; ++round) {
    auto setupOptions = sessionSetupOptionsFrom(options, token.value);
    const auto setupResponse = sessionSetup.exchange(
        *transport, setupOptions, messageId++, sessionId, context);
    if (!setupResponse.ok) {
      return failureFrom(setupResponse.error);
    }

    session = setupResponse.value;
    sessionId = session.sessionId;
    if (!session.moreProcessingRequired) {
      break;
    }

    token = tokenProvider.nextToken(session, options.config);
    if (!token.ok) {
      return failureFrom(token.error);
    }

    if (round + 1 == maxRounds) {
      return internalFailure("SMB SESSION_SETUP exceeded round limit.");
    }
  }

  if (sessionId == 0) {
    return internalFailure("SMB SESSION_SETUP completed without a session id.");
  }
  if (session.encryptData) {
    return unsupportedEncryption(
        "SMB encryption is required by the server session, but clean-room SMB "
        "encryption is not implemented yet.");
  }

  if (negotiated.value.signingRequired) {
    const auto sessionKey = tokenProvider.sessionBaseKey();
    if (!sessionKey.ok) {
      return failureFrom(sessionKey.error);
    }
    if (!supportsSigning(negotiated.value.dialect)) {
      return DecodeResult<NativeSmbConnectedState>::failure(
          ErrorCode::UnsupportedCapability,
          "Clean-room SMB signing is not implemented for the negotiated dialect.");
    }

    auto signingKey = sessionKey.value;
    if (supportsAesCmacSigning(negotiated.value.dialect)) {
      const auto derivedKey =
          deriveSmb3SigningKey(sessionKey.value, negotiated.value.dialect);
      if (!derivedKey.ok) {
        return DecodeResult<NativeSmbConnectedState>::failure(
            derivedKey.error.code, derivedKey.error.message);
      }
      signingKey = derivedKey.value;
    }

    transport = std::make_unique<SigningTransport>(
        std::move(transport), std::move(signingKey),
        negotiated.value.dialect, true);
  }

  TreeConnectRequestOptions treeOptions;
  treeOptions.server = options.config.server;
  treeOptions.share = options.config.share;
  const TreeConnector treeConnector;
  const auto tree =
      treeConnector.connect(*transport, treeOptions, messageId++, sessionId,
                            context);
  if (!tree.ok) {
    return failureFrom(tree.error);
  }
  if (tree.value.requiresEncryption) {
    return unsupportedEncryption(
        "SMB encryption is required by the share, but clean-room SMB encryption "
        "is not implemented yet.");
  }

  NativeSmbSessionConfig sessionConfig;
  sessionConfig.treeId = tree.value.treeId;
  sessionConfig.sessionId = sessionId;
  sessionConfig.firstMessageId = messageId;

  NativeSmbConnectedState state;
  state.negotiated = negotiated.value;
  state.session = session;
  state.tree = tree.value;
  state.connection = std::make_unique<NativeSmbConnection>(
      std::move(transport), sessionConfig);
  return DecodeResult<NativeSmbConnectedState>::success(std::move(state));
}

} // namespace smb::native_smb
