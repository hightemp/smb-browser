#include "smb/DfsResolvingSmbClient.h"

#include <QtTest/QtTest>

#include <utility>

namespace {

smb::core::Connection connection(QString server, QString share) {
  smb::core::Connection result;
  result.server = std::move(server);
  result.share = std::move(share);
  result.normalizedUri =
      QStringLiteral("smb://%1/%2").arg(result.server, result.share);
  result.authType = smb::core::AuthType::Password;
  result.username = QStringLiteral("user");
  result.domain = QStringLiteral("DOMAIN");
  return result;
}

smb::core::AppError smbFailure(smb::core::ErrorCode code,
                               const QString &details) {
  return smb::core::AppError::fromCode(code, smb::core::ErrorCategory::Smb,
                                       details, false);
}

class RecordingSmbClient final : public smb::core::SmbClient {
public:
  smb::core::ErrorCode originalError = smb::core::ErrorCode::ShareUnavailable;
  QString originalDetails =
      QStringLiteral("Tree Connect failed with STATUS_BAD_NETWORK_NAME");
  QString targetServer = QStringLiteral("target.example.com");
  QVector<QString> checkedServers;

  smb::core::Result<bool>
  checkConnection(const smb::core::Connection &candidate,
                  const smb::core::CredentialSecret *,
                  const smb::core::OperationContext &) override {
    checkedServers.push_back(candidate.server);
    if (candidate.server == targetServer) {
      return smb::core::Result<bool>::success(true);
    }
    return smb::core::Result<bool>::failure(
        smbFailure(originalError, originalDetails));
  }

  smb::core::Result<QVector<smb::core::RemoteFileEntry>>
  listDirectory(const smb::core::Connection &,
                const smb::core::CredentialSecret *, const QString &,
                const smb::core::OperationContext &) override {
    return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::success({});
  }

  smb::core::Result<bool>
  createDirectory(const smb::core::Connection &,
                  const smb::core::CredentialSecret *, const QString &,
                  const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> remove(const smb::core::Connection &,
                                 const smb::core::CredentialSecret *,
                                 const QString &,
                                 const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> rename(const smb::core::Connection &,
                                 const smb::core::CredentialSecret *,
                                 const QString &, const QString &,
                                 const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool>
  downloadFile(const smb::core::Connection &,
               const smb::core::CredentialSecret *, const QString &,
               const QString &, const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool>
  uploadFile(const smb::core::Connection &, const smb::core::CredentialSecret *,
             const QString &, const QString &,
             const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> copy(const smb::core::Connection &,
                               const smb::core::CredentialSecret *,
                               const QString &, const smb::core::Connection &,
                               const smb::core::CredentialSecret *,
                               const QString &,
                               const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> move(const smb::core::Connection &,
                               const smb::core::CredentialSecret *,
                               const QString &, const smb::core::Connection &,
                               const smb::core::CredentialSecret *,
                               const QString &,
                               const smb::core::OperationContext &) override {
    return smb::core::Result<bool>::success(true);
  }
};

class StaticDfsResolver final : public smb::core::DfsReferralResolver {
public:
  smb::core::Connection resolved =
      connection(QStringLiteral("target.example.com"), QStringLiteral("RU"));
  int calls = 0;

  smb::core::Result<std::optional<smb::core::Connection>>
  resolve(const smb::core::Connection &, const smb::core::CredentialSecret *,
          const smb::core::OperationContext &) override {
    ++calls;
    return smb::core::Result<std::optional<smb::core::Connection>>::success(
        resolved);
  }
};

} // namespace

class DfsResolvingSmbClientTest final : public QObject {
  Q_OBJECT

private slots:
  void retriesDfsReferralOnResolvedTarget() {
    RecordingSmbClient delegate;
    StaticDfsResolver resolver;
    smb::infrastructure::DfsResolvingSmbClient client(delegate, resolver);

    const auto result = client.checkConnection(
        connection(QStringLiteral("dfs.example.com"), QStringLiteral("ru")),
        nullptr, {});

    QVERIFY(result.ok());
    QCOMPARE(resolver.calls, 1);
    QCOMPARE(delegate.checkedServers.size(), 2);
    QCOMPARE(delegate.checkedServers.at(0), QStringLiteral("dfs.example.com"));
    QCOMPARE(delegate.checkedServers.at(1),
             QStringLiteral("target.example.com"));
  }

  void usesCachedTargetForNextOperation() {
    RecordingSmbClient delegate;
    StaticDfsResolver resolver;
    smb::infrastructure::DfsResolvingSmbClient client(delegate, resolver);
    const auto original =
        connection(QStringLiteral("dfs.example.com"), QStringLiteral("ru"));

    QVERIFY(client.checkConnection(original, nullptr, {}).ok());
    QVERIFY(client.checkConnection(original, nullptr, {}).ok());

    QCOMPARE(resolver.calls, 1);
    QCOMPARE(delegate.checkedServers.size(), 3);
    QCOMPARE(delegate.checkedServers.at(2),
             QStringLiteral("target.example.com"));
  }

  void doesNotResolvePlainDnsErrors() {
    RecordingSmbClient delegate;
    delegate.originalError = smb::core::ErrorCode::DnsError;
    delegate.originalDetails = QStringLiteral("Could not resolve name");
    StaticDfsResolver resolver;
    smb::infrastructure::DfsResolvingSmbClient client(delegate, resolver);

    const auto result = client.checkConnection(
        connection(QStringLiteral("missing.example.com"), QStringLiteral("ru")),
        nullptr, {});

    QVERIFY(!result.ok());
    QVERIFY(result.error().code == smb::core::ErrorCode::DnsError);
    QCOMPARE(resolver.calls, 0);
    QCOMPARE(delegate.checkedServers.size(), 1);
  }
};

QTEST_MAIN(DfsResolvingSmbClientTest)

#include "test_dfs_resolving_smb_client.moc"
