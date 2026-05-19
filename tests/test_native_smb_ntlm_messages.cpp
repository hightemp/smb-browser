#include "NtlmMessages.h"

#include <QtTest/QtTest>

#include <cstdint>

namespace {

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

smb::native_smb::ByteVector challengeMessage() {
  const auto targetName = smb::native_smb::encodeUtf16Le("DOMAIN");
  const smb::native_smb::ByteVector targetInfo{0x02, 0x00, 0x0C, 0x00,
                                               'D',  0x00, 'O',  0x00,
                                               'M',  0x00, 'A',  0x00,
                                               'I',  0x00, 'N',  0x00,
                                               0x00, 0x00, 0x00, 0x00};

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
                         smb::native_smb::kNtlmNegotiateTargetInfo);
  for (int i = 0; i < 8; ++i) {
    bytes.push_back(static_cast<std::uint8_t>(0xA0 + i));
  }
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

} // namespace

class NativeSmbNtlmMessagesTest final : public QObject {
  Q_OBJECT

private slots:
  void buildsNegotiateMessage() {
    smb::native_smb::NtlmNegotiateOptions options;
    options.domain = "domain";
    options.workstation = "workstation";

    const auto message = smb::native_smb::buildNtlmNegotiateMessage(options);

    QCOMPARE(message.size(), std::size_t{74});
    QCOMPARE(message[0], std::uint8_t{'N'});
    QCOMPARE(message[7], std::uint8_t{0});
    QCOMPARE(readU32Le(message, 8), std::uint32_t{1});
    QVERIFY((readU32Le(message, 12) &
             smb::native_smb::kNtlmNegotiateExtendedSessionSecurity) != 0);
    QCOMPARE(readU16Le(message, 16), std::uint16_t{12});
    QCOMPARE(readU32Le(message, 20), std::uint32_t{40});
    QCOMPARE(readU16Le(message, 24), std::uint16_t{22});
    QCOMPARE(readU32Le(message, 28), std::uint32_t{52});
    QCOMPARE(readU16Le(message, 40), std::uint16_t{'D'});
    QCOMPARE(readU16Le(message, 50), std::uint16_t{'N'});
    QCOMPARE(readU16Le(message, 52), std::uint16_t{'W'});
  }

  void decodesChallengeMessage() {
    const auto decoded =
        smb::native_smb::decodeNtlmChallengeMessage(challengeMessage());

    QVERIFY(decoded.ok);
    QCOMPARE(QString::fromStdString(decoded.value.targetName),
             QStringLiteral("DOMAIN"));
    QCOMPARE(decoded.value.flags, smb::native_smb::kNtlmNegotiateUnicode |
                                    smb::native_smb::kNtlmNegotiateTargetInfo);
    QCOMPARE(decoded.value.serverChallenge[0], std::uint8_t{0xA0});
    QCOMPARE(decoded.value.serverChallenge[7], std::uint8_t{0xA7});
    QVERIFY(decoded.value.hasTargetInfo);
    QCOMPARE(decoded.value.targetInfo.size(), std::size_t{20});
  }

  void rejectsInvalidChallengeMessage() {
    auto invalid = challengeMessage();
    invalid[0] = 'X';

    const auto decoded =
        smb::native_smb::decodeNtlmChallengeMessage(invalid);

    QVERIFY(!decoded.ok);
    QCOMPARE(static_cast<int>(decoded.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }

  void buildsAuthenticateMessage() {
    smb::native_smb::NtlmAuthenticateOptions options;
    options.domain = "Domain";
    options.username = "User";
    options.workstation = "COMPUTER";
    options.flags = smb::native_smb::kNtlmNegotiateUnicode |
                    smb::native_smb::kNtlmNegotiateVersion |
                    smb::native_smb::kNtlmNegotiateExtendedSessionSecurity;
    options.lmChallengeResponse = smb::native_smb::ByteVector(24, 0xAA);
    options.ntChallengeResponse = smb::native_smb::ByteVector(84, 0xBB);

    const auto message =
        smb::native_smb::buildNtlmAuthenticateMessage(options);

    QCOMPARE(message[0], std::uint8_t{'N'});
    QCOMPARE(readU32Le(message, 8), std::uint32_t{3});
    QCOMPARE(readU16Le(message, 12), std::uint16_t{24});
    QCOMPARE(readU32Le(message, 16), std::uint32_t{108});
    QCOMPARE(readU16Le(message, 20), std::uint16_t{84});
    QCOMPARE(readU32Le(message, 24), std::uint32_t{132});
    QCOMPARE(readU16Le(message, 28), std::uint16_t{12});
    QCOMPARE(readU32Le(message, 32), std::uint32_t{72});
    QCOMPARE(readU16Le(message, 36), std::uint16_t{8});
    QCOMPARE(readU32Le(message, 40), std::uint32_t{84});
    QCOMPARE(readU16Le(message, 44), std::uint16_t{16});
    QCOMPARE(readU32Le(message, 48), std::uint32_t{92});
    QCOMPARE(readU32Le(message, 60), options.flags);
    QCOMPARE(readU16Le(message, 72), std::uint16_t{'D'});
    QCOMPARE(readU16Le(message, 84), std::uint16_t{'U'});
    QCOMPARE(readU16Le(message, 92), std::uint16_t{'C'});
    QCOMPARE(message[108], std::uint8_t{0xAA});
    QCOMPARE(message[132], std::uint8_t{0xBB});
  }
};

QTEST_MAIN(NativeSmbNtlmMessagesTest)

#include "test_native_smb_ntlm_messages.moc"
