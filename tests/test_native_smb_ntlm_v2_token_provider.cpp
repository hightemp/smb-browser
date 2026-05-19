#include "NtlmV2TokenProvider.h"
#include "SpnegoToken.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace {

smb::native_smb::ByteVector bytes(const char *text) {
  const auto *begin = reinterpret_cast<const std::uint8_t *>(text);
  return smb::native_smb::ByteVector(begin, begin + std::strlen(text));
}

void appendU16Le(smb::native_smb::ByteVector &bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void appendU32Le(smb::native_smb::ByteVector &bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
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

void appendSecurityBuffer(smb::native_smb::ByteVector &bytes,
                          std::uint16_t length, std::uint32_t offset) {
  appendU16Le(bytes, length);
  appendU16Le(bytes, length);
  appendU32Le(bytes, offset);
}

void appendAvPair(smb::native_smb::ByteVector &bytes, std::uint16_t id,
                  const smb::native_smb::ByteVector &value) {
  appendU16Le(bytes, id);
  appendU16Le(bytes, static_cast<std::uint16_t>(value.size()));
  bytes.insert(bytes.end(), value.begin(), value.end());
}

smb::native_smb::ByteVector msNlmpExampleTargetInfo() {
  smb::native_smb::ByteVector targetInfo;
  appendAvPair(targetInfo, 2, smb::native_smb::encodeUtf16Le("Domain"));
  appendAvPair(targetInfo, 1, smb::native_smb::encodeUtf16Le("Server"));
  appendAvPair(targetInfo, 0, {});
  return targetInfo;
}

smb::native_smb::ByteVector rawChallengeMessage() {
  const auto targetName = smb::native_smb::encodeUtf16Le("Server");
  const auto targetInfo = msNlmpExampleTargetInfo();

  smb::native_smb::ByteVector bytes;
  bytes.push_back('N');
  bytes.push_back('T');
  bytes.push_back('L');
  bytes.push_back('M');
  bytes.push_back('S');
  bytes.push_back('S');
  bytes.push_back('P');
  bytes.push_back(0);
  appendU32Le(bytes, 2);
  appendSecurityBuffer(bytes, static_cast<std::uint16_t>(targetName.size()),
                       56);
  appendU32Le(bytes, smb::native_smb::kNtlmNegotiateUnicode |
                         smb::native_smb::kNtlmNegotiateSign |
                         smb::native_smb::kNtlmNegotiateSeal |
                         smb::native_smb::kNtlmNegotiateNtlm |
                         smb::native_smb::kNtlmNegotiateAlwaysSign |
                         smb::native_smb::
                             kNtlmNegotiateExtendedSessionSecurity |
                         smb::native_smb::kNtlmNegotiateTargetInfo |
                         smb::native_smb::kNtlmNegotiateVersion |
                         smb::native_smb::kNtlmNegotiate128 |
                         smb::native_smb::kNtlmNegotiate56);
  constexpr std::array<std::uint8_t, 8> serverChallenge{
      0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
  bytes.insert(bytes.end(), serverChallenge.begin(), serverChallenge.end());
  appendU32Le(bytes, 0);
  appendU32Le(bytes, 0);
  appendSecurityBuffer(bytes, static_cast<std::uint16_t>(targetInfo.size()),
                       static_cast<std::uint32_t>(56 + targetName.size()));
  for (int i = 0; i < 8; ++i) {
    bytes.push_back(0);
  }
  bytes.insert(bytes.end(), targetName.begin(), targetName.end());
  bytes.insert(bytes.end(), targetInfo.begin(), targetInfo.end());
  return bytes;
}

smb::native_smb::SecretBuffer secret(std::string_view text) {
  const auto *begin = reinterpret_cast<const std::uint8_t *>(text.data());
  return smb::native_smb::SecretBuffer(
      smb::native_smb::ByteVector(begin, begin + text.size()));
}

bool contains(const smb::native_smb::ByteVector &haystack,
              const smb::native_smb::ByteVector &needle) {
  return std::search(haystack.begin(), haystack.end(), needle.begin(),
                     needle.end()) != haystack.end();
}

} // namespace

class NativeSmbNtlmV2TokenProviderTest final : public QObject {
  Q_OBJECT

private slots:
  void buildsNegotiateAndAuthenticateTokens() {
    smb::native_smb::NtlmV2TokenProviderOptions providerOptions;
    providerOptions.workstation = "COMPUTER";
    providerOptions.fixedTimestamp = 0;
    providerOptions.fixedClientChallenge =
        std::array<std::uint8_t, 8>{0xAA, 0xAA, 0xAA, 0xAA,
                                    0xAA, 0xAA, 0xAA, 0xAA};
    smb::native_smb::NtlmV2TokenProvider provider(secret("Password"),
                                                  providerOptions);

    smb::native_smb::ConnectionConfig config;
    config.domain = "Domain";
    config.username = "User";

    const auto negotiate = provider.initialToken({}, config);

    QVERIFY(negotiate.ok);
    QCOMPARE(negotiate.value[0], std::uint8_t{0x60});
    QCOMPARE(provider.lastNegotiateMessageForTests()[0], std::uint8_t{'N'});
    QCOMPARE(readU32Le(provider.lastNegotiateMessageForTests(), 8),
             std::uint32_t{1});

    smb::native_smb::SessionSetupResponse challenge;
    challenge.securityBuffer =
        smb::native_smb::buildSpnegoNegTokenResp(rawChallengeMessage());
    const auto authenticate = provider.nextToken(challenge, config);

    QVERIFY(authenticate.ok);
    const auto rawAuthenticate =
        smb::native_smb::unwrapSpnegoNtlmToken(authenticate.value);
    QVERIFY(rawAuthenticate.ok);
    QCOMPARE(readU32Le(rawAuthenticate.value, 8), std::uint32_t{3});
    QCOMPARE(readU16Le(rawAuthenticate.value, 20), std::uint16_t{84});
    QCOMPARE(readU32Le(rawAuthenticate.value, 24), std::uint32_t{132});
    QCOMPARE(QString::fromStdString(
                 smb::native_smb::toHex(provider.sessionBaseKeyForTests())),
             QStringLiteral("8de40ccadbc14a82f15cb0ad0de95ca3"));
    QVERIFY(!contains(rawAuthenticate.value, bytes("Password")));
    QVERIFY(!contains(rawAuthenticate.value,
                      smb::native_smb::encodeUtf16Le("Password")));
  }

  void supportsRawNtlmModeForUnitHarnesses() {
    smb::native_smb::NtlmV2TokenProviderOptions providerOptions;
    providerOptions.useSpnego = false;
    providerOptions.workstation = "COMPUTER";
    providerOptions.fixedTimestamp = 0;
    providerOptions.fixedClientChallenge =
        std::array<std::uint8_t, 8>{0xAA, 0xAA, 0xAA, 0xAA,
                                    0xAA, 0xAA, 0xAA, 0xAA};
    smb::native_smb::NtlmV2TokenProvider provider(secret("Password"),
                                                  providerOptions);
    smb::native_smb::ConnectionConfig config;
    config.domain = "Domain";
    config.username = "User";

    smb::native_smb::SessionSetupResponse challenge;
    challenge.securityBuffer = rawChallengeMessage();
    const auto authenticate = provider.nextToken(challenge, config);

    QVERIFY(authenticate.ok);
    QCOMPARE(readU32Le(authenticate.value, 8), std::uint32_t{3});
  }

  void anonymousAuthUsesEmptyIdentityAndNoNtResponse() {
    smb::native_smb::NtlmV2TokenProviderOptions providerOptions;
    providerOptions.useSpnego = false;
    smb::native_smb::NtlmV2TokenProvider provider(secret(""),
                                                  providerOptions);
    smb::native_smb::ConnectionConfig config;
    config.authMode = smb::native_smb::AuthMode::Anonymous;
    config.domain = "IgnoredDomain";
    config.username = "IgnoredUser";

    smb::native_smb::SessionSetupResponse challenge;
    challenge.securityBuffer = rawChallengeMessage();
    const auto authenticate = provider.nextToken(challenge, config);

    QVERIFY(authenticate.ok);
    QCOMPARE(readU32Le(authenticate.value, 8), std::uint32_t{3});
    QCOMPARE(readU16Le(authenticate.value, 12), std::uint16_t{1});
    QCOMPARE(readU16Le(authenticate.value, 20), std::uint16_t{0});
    QCOMPARE(readU16Le(authenticate.value, 28), std::uint16_t{0});
    QCOMPARE(readU16Le(authenticate.value, 36), std::uint16_t{0});
    QVERIFY(!contains(authenticate.value,
                      smb::native_smb::encodeUtf16Le("IgnoredDomain")));
    QVERIFY(!contains(authenticate.value,
                      smb::native_smb::encodeUtf16Le("IgnoredUser")));

    const auto sessionKey = provider.sessionBaseKey();
    QVERIFY(!sessionKey.ok);
    QCOMPARE(static_cast<int>(sessionKey.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::AuthenticationFailed));
  }

  void guestAuthDefaultsToGuestIdentityWithEmptyPassword() {
    smb::native_smb::NtlmV2TokenProviderOptions providerOptions;
    providerOptions.useSpnego = false;
    providerOptions.workstation = "COMPUTER";
    providerOptions.fixedTimestamp = 0;
    providerOptions.fixedClientChallenge =
        std::array<std::uint8_t, 8>{0xBB, 0xBB, 0xBB, 0xBB,
                                    0xBB, 0xBB, 0xBB, 0xBB};
    smb::native_smb::NtlmV2TokenProvider provider(secret(""),
                                                  providerOptions);
    smb::native_smb::ConnectionConfig config;
    config.authMode = smb::native_smb::AuthMode::Guest;
    config.domain = "WORKGROUP";

    smb::native_smb::SessionSetupResponse challenge;
    challenge.securityBuffer = rawChallengeMessage();
    const auto authenticate = provider.nextToken(challenge, config);

    QVERIFY(authenticate.ok);
    QCOMPARE(readU32Le(authenticate.value, 8), std::uint32_t{3});
    QCOMPARE(readU16Le(authenticate.value, 28), std::uint16_t{18});
    QCOMPARE(readU16Le(authenticate.value, 36), std::uint16_t{10});
    QVERIFY(readU16Le(authenticate.value, 20) > 0);
    QVERIFY(contains(authenticate.value,
                     smb::native_smb::encodeUtf16Le("WORKGROUP")));
    QVERIFY(contains(authenticate.value,
                     smb::native_smb::encodeUtf16Le("Guest")));

    const auto sessionKey = provider.sessionBaseKey();
    QVERIFY(sessionKey.ok);
    QCOMPARE(sessionKey.value.size(), std::size_t{16});
  }

  void currentUserAuthReportsUnsupported() {
    smb::native_smb::NtlmV2TokenProvider provider(secret(""));
    smb::native_smb::ConnectionConfig config;
    config.authMode = smb::native_smb::AuthMode::CurrentUser;

    const auto negotiate = provider.initialToken({}, config);

    QVERIFY(!negotiate.ok);
    QCOMPARE(static_cast<int>(negotiate.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::UnsupportedCapability));
  }

  void rejectsInvalidSpnegoToken() {
    smb::native_smb::NtlmV2TokenProvider provider(secret("Password"));
    smb::native_smb::ConnectionConfig config;
    config.domain = "Domain";
    config.username = "User";

    smb::native_smb::SessionSetupResponse challenge;
    challenge.securityBuffer = {'S', 'P', 'N', 'E', 'G', 'O'};
    const auto authenticate = provider.nextToken(challenge, config);

    QVERIFY(!authenticate.ok);
    QCOMPARE(static_cast<int>(authenticate.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }
};

QTEST_MAIN(NativeSmbNtlmV2TokenProviderTest)

#include "test_native_smb_ntlm_v2_token_provider.moc"
