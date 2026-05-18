#include "core/LogSanitizer.h"

#include <QtTest/QtTest>

class LogSanitizerTest final : public QObject {
  Q_OBJECT

private slots:
  void masksExplicitSecretValues() {
    smb::core::LogSanitizer sanitizer({QStringLiteral("real-test-secret")});

    const auto sanitized = sanitizer.sanitize(
        QStringLiteral("Connection failed for password real-test-secret"));

    QVERIFY(!sanitized.contains(QStringLiteral("real-test-secret")));
    QVERIFY(sanitized.contains(QStringLiteral("***")));
  }

  void masksCredentialKeyValues() {
    const smb::core::LogSanitizer sanitizer;

    const auto sanitized = sanitizer.sanitize(QStringLiteral(
        R"(password="alpha beta" token=abc123 master_password=local-vault)"));

    QVERIFY(!sanitized.contains(QStringLiteral("alpha beta")));
    QVERIFY(!sanitized.contains(QStringLiteral("abc123")));
    QVERIFY(!sanitized.contains(QStringLiteral("local-vault")));
    QVERIFY(sanitized.contains(QStringLiteral(R"(password=***)")));
    QVERIFY(sanitized.contains(QStringLiteral("token=***")));
    QVERIFY(sanitized.contains(QStringLiteral("master_password=***")));
  }

  void masksCredentialUrisAndAuthorizationHeaders() {
    const smb::core::LogSanitizer sanitizer;

    const auto sanitized = sanitizer.sanitize(
        QStringLiteral("GET smb://DOMAIN;user:secret@server/share "
                       "Authorization: Bearer jwt-token"));

    QVERIFY(!sanitized.contains(QStringLiteral("DOMAIN;user:secret")));
    QVERIFY(!sanitized.contains(QStringLiteral("jwt-token")));
    QVERIFY(sanitized.contains(QStringLiteral("smb://***@server/share")));
    QVERIFY(sanitized.contains(QStringLiteral("Authorization: Bearer ***")));
  }

  void leavesSafeSmbPathsReadable() {
    const smb::core::LogSanitizer sanitizer;

    const auto sanitized =
        sanitizer.sanitize(QStringLiteral("Listed smb://server/share/folder"));

    QCOMPARE(sanitized, QStringLiteral("Listed smb://server/share/folder"));
  }
};

QTEST_MAIN(LogSanitizerTest)

#include "test_log_sanitizer.moc"
