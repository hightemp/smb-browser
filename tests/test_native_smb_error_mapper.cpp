#include "smb/NativeSmbErrorMapper.h"

#include <QtTest/QtTest>

#include <vector>

class NativeSmbErrorMapperTest final : public QObject {
  Q_OBJECT

private slots:
  void mapsNativeErrorCodes() {
    const std::vector<std::pair<smb::native_smb::ErrorCode,
                                smb::core::ErrorCode>>
        cases = {
            {smb::native_smb::ErrorCode::None, smb::core::ErrorCode::None},
            {smb::native_smb::ErrorCode::Cancelled,
             smb::core::ErrorCode::OperationCancelled},
            {smb::native_smb::ErrorCode::Timeout,
             smb::core::ErrorCode::Timeout},
            {smb::native_smb::ErrorCode::DnsError,
             smb::core::ErrorCode::DnsError},
            {smb::native_smb::ErrorCode::ServerUnavailable,
             smb::core::ErrorCode::ServerUnavailable},
            {smb::native_smb::ErrorCode::ShareUnavailable,
             smb::core::ErrorCode::ShareUnavailable},
            {smb::native_smb::ErrorCode::AuthenticationFailed,
             smb::core::ErrorCode::AuthenticationFailed},
            {smb::native_smb::ErrorCode::PermissionDenied,
             smb::core::ErrorCode::PermissionDenied},
            {smb::native_smb::ErrorCode::ProtocolUnsupported,
             smb::core::ErrorCode::ProtocolUnsupported},
            {smb::native_smb::ErrorCode::FileNotFound,
             smb::core::ErrorCode::FileNotFound},
            {smb::native_smb::ErrorCode::AlreadyExists,
             smb::core::ErrorCode::AlreadyExists},
            {smb::native_smb::ErrorCode::DirectoryNotEmpty,
             smb::core::ErrorCode::DirectoryNotEmpty},
            {smb::native_smb::ErrorCode::InvalidPath,
             smb::core::ErrorCode::InvalidPath},
            {smb::native_smb::ErrorCode::NetworkError,
             smb::core::ErrorCode::NetworkError},
            {smb::native_smb::ErrorCode::IoError,
             smb::core::ErrorCode::LocalIoError},
            {smb::native_smb::ErrorCode::UnsupportedCapability,
             smb::core::ErrorCode::ProtocolUnsupported},
            {smb::native_smb::ErrorCode::InternalError,
             smb::core::ErrorCode::Unknown},
        };

    for (const auto &testCase : cases) {
      QCOMPARE(static_cast<int>(
                   smb::infrastructure::nativeToCoreError(testCase.first)),
               static_cast<int>(testCase.second));
    }
  }

  void marksRetryableNetworkClassErrors() {
    QVERIFY(smb::infrastructure::isNativeErrorRetryable(
        smb::native_smb::ErrorCode::Timeout));
    QVERIFY(smb::infrastructure::isNativeErrorRetryable(
        smb::native_smb::ErrorCode::NetworkError));
    QVERIFY(smb::infrastructure::isNativeErrorRetryable(
        smb::native_smb::ErrorCode::ServerUnavailable));
    QVERIFY(!smb::infrastructure::isNativeErrorRetryable(
        smb::native_smb::ErrorCode::AuthenticationFailed));
    QVERIFY(!smb::infrastructure::isNativeErrorRetryable(
        smb::native_smb::ErrorCode::PermissionDenied));
  }

  void sanitizesTechnicalDetails() {
    smb::core::LogSanitizer sanitizer;
    sanitizer.addSecretValue(QStringLiteral("plain-secret"));
    const smb::native_smb::ProtocolError nativeError{
        smb::native_smb::ErrorCode::AuthenticationFailed,
        "password=plain-secret token=abc123 smb://DOMAIN;user:plain-secret@server/share"};

    const auto error =
        smb::infrastructure::makeNativeSmbError(nativeError, sanitizer);

    QCOMPARE(static_cast<int>(error.code),
             static_cast<int>(smb::core::ErrorCode::AuthenticationFailed));
    QCOMPARE(static_cast<int>(error.category),
             static_cast<int>(smb::core::ErrorCategory::Smb));
    QVERIFY(!error.sanitizedTechnicalDetails.contains(
        QStringLiteral("plain-secret")));
    QVERIFY(!error.sanitizedTechnicalDetails.contains(
        QStringLiteral("abc123")));
    QVERIFY(error.sanitizedTechnicalDetails.contains(QStringLiteral("***")));
  }
};

QTEST_MAIN(NativeSmbErrorMapperTest)

#include "test_native_smb_error_mapper.moc"
