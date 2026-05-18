#include "core/Error.h"

#include <QtTest/QtTest>

class ErrorModelTest final : public QObject {
  Q_OBJECT

private slots:
  void noneHasNoError() {
    const auto error = smb::core::AppError::none();

    QVERIFY(error.code == smb::core::ErrorCode::None);
    QVERIFY(!error.hasError());
    QVERIFY(error.userMessage.isEmpty());
  }

  void authenticationErrorHasSafeUserMessage() {
    const auto error = smb::core::AppError::fromCode(
        smb::core::ErrorCode::AuthenticationFailed,
        smb::core::ErrorCategory::Smb,
        QStringLiteral("sanitized backend detail"), false);

    QVERIFY(error.hasError());
    QVERIFY(error.category == smb::core::ErrorCategory::Smb);
    QVERIFY(!error.userMessage.contains(QStringLiteral("secret-password")));
    QVERIFY(!error.userMessage.contains(QStringLiteral("DOMAIN\\user")));
    QCOMPARE(error.sanitizedTechnicalDetails,
             QStringLiteral("sanitized backend detail"));
  }

  void codesHaveStableNames() {
    QCOMPARE(smb::core::toString(smb::core::ErrorCode::PermissionDenied),
             QStringLiteral("permission_denied"));
    QCOMPARE(smb::core::toString(smb::core::ErrorCategory::Credentials),
             QStringLiteral("credentials"));
  }
};

QTEST_MAIN(ErrorModelTest)

#include "test_error_model.moc"
