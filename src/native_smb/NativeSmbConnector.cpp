#include "NativeSmbConnector.h"

#include "Smb2Signing.h"
#include "Smb3Encryption.h"

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

bool supportsAesCcmEncryption(Dialect dialect) {
  return dialect == Dialect::Smb300 || dialect == Dialect::Smb302;
}

NegotiateRequestOptions negotiateOptionsFrom(
    const NativeSmbConnectorOptions &options) {
  auto negotiate = options.negotiateOptions;
  negotiate.signing = options.config.signing;
  negotiate.capabilities |=
      capabilityMask({GlobalCapability::Dfs, GlobalCapability::LargeMtu});
  if (options.config.encryption != SecurityPolicy::Disabled) {
    negotiate.capabilities |= capabilityMask({GlobalCapability::Encryption});
  }
  return negotiate;
}

SessionSetupRequestOptions sessionSetupOptionsFrom(
    const NativeSmbConnectorOptions &options, const ByteVector &token) {
  SessionSetupRequestOptions setup;
  setup.signing = options.config.signing;
  setup.capabilities =
      capabilityMask({GlobalCapability::Dfs, GlobalCapability::LargeMtu});
  if (options.config.encryption != SecurityPolicy::Disabled) {
    setup.capabilities |= capabilityMask({GlobalCapability::Encryption});
  }
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
    if (!negotiated.value.encryptionSupported) {
      return unsupportedEncryption(
          "SMB encryption is required by policy, but the server did not "
          "advertise SMB3 encryption support.");
    }
    if (!supportsAesCcmEncryption(negotiated.value.dialect)) {
      return unsupportedEncryption(
          "SMB encryption is required by policy, but the negotiated dialect is "
          "not supported by the clean-room AES-CCM transform layer.");
    }
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
  const bool sessionCanEncrypt =
      negotiated.value.encryptionSupported &&
      supportsAesCcmEncryption(negotiated.value.dialect) &&
      !session.guestSession && !session.nullSession;
  if ((session.encryptData ||
       options.config.encryption == SecurityPolicy::Required) &&
      !sessionCanEncrypt) {
    return unsupportedEncryption(
        "SMB encryption is required, but the negotiated "
        "connection does not support the clean-room AES-CCM transform layer.");
  }

  ByteVector sessionKey;
  const bool needsSessionKey =
      negotiated.value.signingRequired || session.encryptData ||
      options.config.encryption == SecurityPolicy::Required;
  if (needsSessionKey) {
    const auto derivedSessionKey = tokenProvider.sessionBaseKey();
    if (!derivedSessionKey.ok) {
      return failureFrom(derivedSessionKey.error);
    }
    sessionKey = derivedSessionKey.value;
  }
  if (negotiated.value.signingRequired) {
    if (!supportsSigning(negotiated.value.dialect)) {
      return DecodeResult<NativeSmbConnectedState>::failure(
          ErrorCode::UnsupportedCapability,
          "Clean-room SMB signing is not implemented for the negotiated dialect.");
    }

    auto signingKey = sessionKey;
    if (supportsAesCmacSigning(negotiated.value.dialect)) {
      const auto derivedKey =
          deriveSmb3SigningKey(sessionKey, negotiated.value.dialect);
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

  bool connectionEncrypted = false;
  auto enableEncryption = [&]() -> DecodeResult<bool> {
    if (connectionEncrypted) {
      return DecodeResult<bool>::success(true);
    }
    if (!sessionCanEncrypt) {
      return DecodeResult<bool>::failure(
          ErrorCode::UnsupportedCapability,
          "SMB encryption is required, but the negotiated connection does not "
          "support the clean-room AES-CCM transform layer.");
    }
    if (sessionKey.empty()) {
      const auto derivedSessionKey = tokenProvider.sessionBaseKey();
      if (!derivedSessionKey.ok) {
        return DecodeResult<bool>::failure(derivedSessionKey.error.code,
                                           derivedSessionKey.error.message);
      }
      sessionKey = derivedSessionKey.value;
    }

    const auto encryptionKey = deriveSmb3EncryptionKey(
        sessionKey, negotiated.value.dialect,
        Smb3KeyDirection::ClientToServer);
    if (!encryptionKey.ok) {
      return DecodeResult<bool>::failure(encryptionKey.error.code,
                                         encryptionKey.error.message);
    }
    const auto decryptionKey = deriveSmb3EncryptionKey(
        sessionKey, negotiated.value.dialect,
        Smb3KeyDirection::ServerToClient);
    if (!decryptionKey.ok) {
      return DecodeResult<bool>::failure(decryptionKey.error.code,
                                         decryptionKey.error.message);
    }

    transport = std::make_unique<Smb3EncryptionTransport>(
        std::move(transport), encryptionKey.value, decryptionKey.value,
        sessionId, negotiated.value.dialect);
    connectionEncrypted = true;
    return DecodeResult<bool>::success(true);
  };

  if (session.encryptData ||
      options.config.encryption == SecurityPolicy::Required) {
    const auto encrypted = enableEncryption();
    if (!encrypted.ok) {
      return DecodeResult<NativeSmbConnectedState>::failure(
          encrypted.error.code, encrypted.error.message);
    }
  }

  TreeConnectRequestOptions treeOptions;
  treeOptions.server = options.config.server;
  treeOptions.share = options.config.share;
  const TreeConnector treeConnector;
  auto tree =
      treeConnector.connect(*transport, treeOptions, messageId++, sessionId,
                            context);
  if (!tree.ok &&
      tree.error.code == ErrorCode::PermissionDenied &&
      options.config.encryption == SecurityPolicy::Preferred &&
      sessionCanEncrypt && !connectionEncrypted) {
    const auto encrypted = enableEncryption();
    if (!encrypted.ok) {
      return DecodeResult<NativeSmbConnectedState>::failure(
          encrypted.error.code, encrypted.error.message);
    }
    tree = treeConnector.connect(*transport, treeOptions, messageId++,
                                 sessionId, context);
  }
  if (!tree.ok) {
    return failureFrom(tree.error);
  }
  const bool shouldEncrypt =
      session.encryptData || tree.value.requiresEncryption ||
      options.config.encryption == SecurityPolicy::Required;
  if (shouldEncrypt && !connectionEncrypted) {
    const auto encrypted = enableEncryption();
    if (!encrypted.ok) {
      return DecodeResult<NativeSmbConnectedState>::failure(
          encrypted.error.code, encrypted.error.message);
    }
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
