#include "core/PathNormalizer.h"

#include <QtTest/QtTest>

class PathNormalizerTest final : public QObject {
  Q_OBJECT

private slots:
  void parsesWindowsUncPath() {
    const auto result = smb::core::PathNormalizer::normalizeSmbPath(
        QStringLiteral("\\\\server\\share"));

    QVERIFY(result.ok());
    QCOMPARE(result.value().normalizedUri,
             QStringLiteral("smb://server/share"));
    QCOMPARE(result.value().server, QStringLiteral("server"));
    QCOMPARE(result.value().share, QStringLiteral("share"));
    QVERIFY(result.value().initialRemotePath.isEmpty());
  }

  void parsesSmbUri() {
    const auto result = smb::core::PathNormalizer::normalizeSmbPath(
        QStringLiteral("smb://server/share/folder/file.txt"));

    QVERIFY(result.ok());
    QCOMPARE(result.value().normalizedUri,
             QStringLiteral("smb://server/share"));
    QCOMPARE(result.value().server, QStringLiteral("server"));
    QCOMPARE(result.value().share, QStringLiteral("share"));
    QCOMPARE(result.value().initialRemotePath,
             QStringLiteral("folder/file.txt"));
  }

  void parsesServerSharePath() {
    const auto result = smb::core::PathNormalizer::normalizeSmbPath(
        QStringLiteral("server/share"));

    QVERIFY(result.ok());
    QCOMPARE(result.value().normalizedUri,
             QStringLiteral("smb://server/share"));
  }

  void rejectsMissingShare() {
    const auto result =
        smb::core::PathNormalizer::normalizeSmbPath(QStringLiteral("server"));

    QVERIFY(!result.ok());
    QVERIFY(result.error().code == smb::core::ErrorCode::InvalidPath);
  }

  void rejectsCredentialsInsideUri() {
    const auto result = smb::core::PathNormalizer::normalizeSmbPath(
        QStringLiteral("smb://user:secret@server/share"));

    QVERIFY(!result.ok());
    QVERIFY(result.error().sanitizedTechnicalDetails.contains(
        QStringLiteral("must not contain credentials")));
    QVERIFY(!result.error().sanitizedTechnicalDetails.contains(
        QStringLiteral("secret")));
  }

  void parsesDomainBackslashUser() {
    const auto result = smb::core::PathNormalizer::normalizeIdentity(
        smb::core::AuthType::Password, QStringLiteral("DOMAIN\\user"));

    QVERIFY(result.ok());
    QCOMPARE(result.value().domain, QStringLiteral("DOMAIN"));
    QCOMPARE(result.value().username, QStringLiteral("user"));
  }

  void parsesUserAtDomain() {
    const auto result = smb::core::PathNormalizer::normalizeIdentity(
        smb::core::AuthType::Password, QStringLiteral("user@example.local"));

    QVERIFY(result.ok());
    QCOMPARE(result.value().domain, QStringLiteral("example.local"));
    QCOMPARE(result.value().username, QStringLiteral("user"));
  }

  void appliesExplicitDomain() {
    const auto result = smb::core::PathNormalizer::normalizeIdentity(
        smb::core::AuthType::Password, QStringLiteral("user"),
        QStringLiteral("WORKGROUP"));

    QVERIFY(result.ok());
    QCOMPARE(result.value().domain, QStringLiteral("WORKGROUP"));
    QCOMPARE(result.value().username, QStringLiteral("user"));
  }

  void rejectsConflictingDomain() {
    const auto result = smb::core::PathNormalizer::normalizeIdentity(
        smb::core::AuthType::Password, QStringLiteral("DOMAIN\\user"),
        QStringLiteral("OTHER"));

    QVERIFY(!result.ok());
    QVERIFY(result.error().code == smb::core::ErrorCode::InvalidPath);
  }

  void supportsGuestAnonymousAndCurrentUser() {
    const auto guest = smb::core::PathNormalizer::normalizeIdentity(
        smb::core::AuthType::Guest, {}, {});
    const auto anonymous = smb::core::PathNormalizer::normalizeIdentity(
        smb::core::AuthType::Anonymous, {}, {});
    const auto currentUser = smb::core::PathNormalizer::normalizeIdentity(
        smb::core::AuthType::CurrentUser, {}, {});

    QVERIFY(guest.ok());
    QVERIFY(anonymous.ok());
    QVERIFY(currentUser.ok());
    QVERIFY(guest.value().authType == smb::core::AuthType::Guest);
    QVERIFY(anonymous.value().authType == smb::core::AuthType::Anonymous);
    QVERIFY(currentUser.value().authType == smb::core::AuthType::CurrentUser);
  }
};

QTEST_MAIN(PathNormalizerTest)

#include "test_path_normalizer.moc"
