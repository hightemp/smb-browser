#include "smb/Libsmb2SmbClient.h"

#include <QtTest/QtTest>

namespace {

smb::core::Connection passwordConnection() {
  auto connection = smb::core::Connection::createEmpty();
  connection.name = QStringLiteral("Production Backend Test");
  connection.normalizedUri = QStringLiteral("smb://server/share");
  connection.server = QStringLiteral("server");
  connection.share = QStringLiteral("share");
  connection.username = QStringLiteral("user");
  connection.authType = smb::core::AuthType::Password;
  return connection;
}

} // namespace

class Libsmb2SmbClientTest final : public QObject {
  Q_OBJECT

private slots:
  void passwordOperationsRequireSecretBeforeNetworkCall() {
    smb::infrastructure::Libsmb2SmbClient client;

    const auto result =
        client.checkConnection(passwordConnection(), nullptr, {});

    QVERIFY(!result.ok());
    QVERIFY(result.error().code == smb::core::ErrorCode::CredentialNotFound);
    QVERIFY(!result.error().sanitizedTechnicalDetails.contains(
        QStringLiteral("test-secret")));
  }

  void cancelledOperationStopsBeforeNetworkCall() {
    smb::infrastructure::Libsmb2SmbClient client;
    smb::core::CancellationToken token;
    token.cancel();

    smb::core::OperationContext context;
    context.cancellationToken = &token;

    const smb::core::CredentialSecret secret{QByteArrayLiteral("test-secret")};
    const auto result =
        client.checkConnection(passwordConnection(), &secret, context);

    QVERIFY(!result.ok());
    QVERIFY(result.error().code == smb::core::ErrorCode::OperationCancelled);
  }

  void writeOperationsValidateSecretBeforeNetworkCall() {
    smb::infrastructure::Libsmb2SmbClient client;
    const auto connection = passwordConnection();

    const auto result = client.createDirectory(
        connection, nullptr, QStringLiteral("/new-folder"), {});

    QVERIFY(!result.ok());
    QVERIFY(result.error().code == smb::core::ErrorCode::CredentialNotFound);
  }

  void crossShareCopyIsExplicitlyDeferred() {
    smb::infrastructure::Libsmb2SmbClient client;
    const auto connection = passwordConnection();
    const smb::core::CredentialSecret secret{QByteArrayLiteral("test-secret")};

    const auto result =
        client.copy(connection, &secret, QStringLiteral("/source.txt"),
                    connection, &secret, QStringLiteral("/target.txt"), {});

    QVERIFY(!result.ok());
    QVERIFY(result.error().code == smb::core::ErrorCode::Unknown);
    QVERIFY(result.error().sanitizedTechnicalDetails.contains(
        QStringLiteral("not implemented")));
  }
};

QTEST_MAIN(Libsmb2SmbClientTest)

#include "test_libsmb2_smb_client.moc"
