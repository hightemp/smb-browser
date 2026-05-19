#include "NtlmCrypto.h"

#include <QtTest/QtTest>

#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
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

smb::native_smb::ByteVector utf16(std::string_view text) {
  return smb::native_smb::encodeUtf16Le(text);
}

void appendAvPair(smb::native_smb::ByteVector &bytes, std::uint16_t id,
                  const smb::native_smb::ByteVector &value) {
  appendU16Le(bytes, id);
  appendU16Le(bytes, static_cast<std::uint16_t>(value.size()));
  bytes.insert(bytes.end(), value.begin(), value.end());
}

std::string hex(const smb::native_smb::ByteVector &bytes) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const auto byte : bytes) {
    stream << std::setw(2) << static_cast<int>(byte);
  }
  return stream.str();
}

smb::native_smb::ByteVector msNlmpExampleTargetInfo() {
  smb::native_smb::ByteVector targetInfo;
  appendAvPair(targetInfo, 2, utf16("Domain"));
  appendAvPair(targetInfo, 1, utf16("Server"));
  appendAvPair(targetInfo, 0, {});
  return targetInfo;
}

} // namespace

class NativeSmbNtlmCryptoTest final : public QObject {
  Q_OBJECT

private slots:
  void computesMd4TestVectors() {
    QCOMPARE(QString::fromStdString(smb::native_smb::toHex(
                 smb::native_smb::md4({}))),
             QStringLiteral("31d6cfe0d16ae931b73c59d7e0c089c0"));
    QCOMPARE(QString::fromStdString(smb::native_smb::toHex(
                 smb::native_smb::md4(bytes("abc")))),
             QStringLiteral("a448017aaf21d8525fc10ae87aa6729d"));
  }

  void computesMd5TestVectors() {
    QCOMPARE(QString::fromStdString(smb::native_smb::toHex(
                 smb::native_smb::md5({}))),
             QStringLiteral("d41d8cd98f00b204e9800998ecf8427e"));
    QCOMPARE(QString::fromStdString(smb::native_smb::toHex(
                 smb::native_smb::md5(bytes("abc")))),
             QStringLiteral("900150983cd24fb0d6963f7d28e17f72"));
  }

  void computesHmacMd5TestVector() {
    smb::native_smb::ByteVector key(16, 0x0B);
    const auto digest = smb::native_smb::hmacMd5(key, bytes("Hi There"));

    QCOMPARE(QString::fromStdString(smb::native_smb::toHex(digest)),
             QStringLiteral("9294727a3638bb1c13f48ef8158bfc9d"));
  }

  void computesNtHashAndNtowfv2TestVectors() {
    QCOMPARE(QString::fromStdString(smb::native_smb::toHex(
                 smb::native_smb::ntHash("password"))),
             QStringLiteral("8846f7eaee8fb117ad06bdd830b7586c"));
    QCOMPARE(QString::fromStdString(smb::native_smb::toHex(
                 smb::native_smb::ntowfv2("Password", "User", "Domain"))),
             QStringLiteral("0c868a403bfd7a93a3001ef22ef02e3f"));
  }

  void computesNtlmV2ResponseTestVector() {
    constexpr std::array<std::uint8_t, 8> serverChallenge{
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    constexpr std::array<std::uint8_t, 8> clientChallenge{
        0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};

    const auto response = smb::native_smb::computeNtlmV2Response(
        "Password", "User", "Domain", serverChallenge, clientChallenge, 0,
        msNlmpExampleTargetInfo());

    QVERIFY(response.ok);
    QCOMPARE(QString::fromStdString(
                 smb::native_smb::toHex(response.value.ntProof)),
             QStringLiteral("68cd0ab851e51c96aabc927bebef6a1c"));
    QCOMPARE(QString::fromStdString(
                 smb::native_smb::toHex(response.value.sessionBaseKey)),
             QStringLiteral("8de40ccadbc14a82f15cb0ad0de95ca3"));
    QCOMPARE(QString::fromStdString(hex(response.value.lmChallengeResponse)),
             QStringLiteral("86c35097ac9cec102554764a57cccc19"
                            "aaaaaaaaaaaaaaaa"));
    QCOMPARE(response.value.ntChallengeResponse.size(),
             std::size_t{16 + 28 + msNlmpExampleTargetInfo().size() + 4});
  }
};

QTEST_MAIN(NativeSmbNtlmCryptoTest)

#include "test_native_smb_ntlm_crypto.moc"
