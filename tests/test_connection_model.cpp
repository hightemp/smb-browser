#include "core/Connection.h"

#include <QtTest/QtTest>

class ConnectionModelTest final : public QObject {
  Q_OBJECT

private slots:
  void defaultConnectionDoesNotContainPassword() {
    const auto connection = smb::core::Connection::createEmpty();

    QVERIFY(connection.authType == smb::core::AuthType::Password);
    QVERIFY(connection.credentialRef.isEmpty());
    QVERIFY(!connection.usesStoredCredential());
    QVERIFY(connection.lastErrorCode == smb::core::ErrorCode::None);
  }

  void passwordConnectionUsesCredentialReferenceOnly() {
    auto connection = smb::core::Connection::createEmpty();
    connection.credentialRef = QStringLiteral("credential-1");

    QVERIFY(connection.usesStoredCredential());
    QVERIFY(!connection.credentialRef.contains(QStringLiteral("password")));
  }

  void authTypesHaveStableNames() {
    QCOMPARE(smb::core::toString(smb::core::AuthType::Password),
             QStringLiteral("password"));
    QCOMPARE(smb::core::toString(smb::core::AuthType::Guest),
             QStringLiteral("guest"));
    QCOMPARE(smb::core::toString(smb::core::AuthType::Anonymous),
             QStringLiteral("anonymous"));
    QCOMPARE(smb::core::toString(smb::core::AuthType::CurrentUser),
             QStringLiteral("current_user"));
  }
};

QTEST_MAIN(ConnectionModelTest)

#include "test_connection_model.moc"
