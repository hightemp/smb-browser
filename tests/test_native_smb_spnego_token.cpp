#include "SpnegoToken.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

smb::native_smb::ByteVector ntlmToken(std::uint32_t messageType) {
  smb::native_smb::ByteVector bytes{'N', 'T', 'L', 'M', 'S', 'S', 'P', 0};
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>((messageType >> shift) & 0xFF));
  }
  bytes.push_back(0xA5);
  return bytes;
}

bool contains(const smb::native_smb::ByteVector &haystack,
              const smb::native_smb::ByteVector &needle) {
  return std::search(haystack.begin(), haystack.end(), needle.begin(),
                     needle.end()) != haystack.end();
}

} // namespace

class NativeSmbSpnegoTokenTest final : public QObject {
  Q_OBJECT

private slots:
  void wrapsNegTokenInit() {
    const auto raw = ntlmToken(1);
    const auto wrapped = smb::native_smb::buildSpnegoNegTokenInit(raw);

    QCOMPARE(wrapped[0], std::uint8_t{0x60});
    QVERIFY(contains(wrapped, raw));
  }

  void wrapsAndUnwrapsNegTokenResp() {
    const auto raw = ntlmToken(2);
    const auto wrapped = smb::native_smb::buildSpnegoNegTokenResp(raw);
    const auto unwrapped = smb::native_smb::unwrapSpnegoNtlmToken(wrapped);

    QVERIFY(unwrapped.ok);
    QCOMPARE(unwrapped.value, raw);
  }

  void passesRawNtlmThrough() {
    const auto raw = ntlmToken(3);
    const auto unwrapped = smb::native_smb::unwrapSpnegoNtlmToken(raw);

    QVERIFY(unwrapped.ok);
    QCOMPARE(unwrapped.value, raw);
  }

  void rejectsMalformedDer() {
    const smb::native_smb::ByteVector invalid{0xA1, 0x81};
    const auto unwrapped = smb::native_smb::unwrapSpnegoNtlmToken(invalid);

    QVERIFY(!unwrapped.ok);
    QCOMPARE(static_cast<int>(unwrapped.error.code),
             static_cast<int>(
                 smb::native_smb::ErrorCode::ProtocolUnsupported));
  }
};

QTEST_MAIN(NativeSmbSpnegoTokenTest)

#include "test_native_smb_spnego_token.moc"
