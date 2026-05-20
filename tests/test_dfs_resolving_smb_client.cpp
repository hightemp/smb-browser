#include "smb/DfsResolvingSmbClient.h"

#include <QHash>
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

smb::core::RemoteFileEntry remoteEntry(QString name, QString path) {
  smb::core::RemoteFileEntry entry;
  entry.name = std::move(name);
  entry.remotePath = std::move(path);
  entry.type = smb::core::RemoteFileType::Directory;
  return entry;
}

class RecordingSmbClient final : public smb::core::SmbClient {
public:
  smb::core::ErrorCode originalError = smb::core::ErrorCode::ShareUnavailable;
  QString originalDetails =
      QStringLiteral("Tree Connect failed with STATUS_BAD_NETWORK_NAME");
  QString targetServer = QStringLiteral("target.example.com");
  QVector<QString> checkedServers;
  QVector<QPair<QString, QString>> listedPaths;
  bool failOriginalListWithPathReferral = false;
  QHash<QString, QVector<smb::core::RemoteFileEntry>> targetDirectoryEntries;

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
  listDirectory(const smb::core::Connection &candidate,
                const smb::core::CredentialSecret *, const QString &remotePath,
                const smb::core::OperationContext &) override {
    listedPaths.push_back({candidate.server, remotePath});
    if (candidate.server == targetServer) {
      return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::success(
          targetDirectoryEntries.value(remotePath));
    }
    if (failOriginalListWithPathReferral) {
      return smb::core::Result<QVector<smb::core::RemoteFileEntry>>::failure(
          smbFailure(smb::core::ErrorCode::ShareUnavailable,
                     QStringLiteral("SMB2_STATUS_PATH_NOT_COVERED "
                                    "ntstatus=0xc0000257")));
    }
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
  smb::core::DfsResolvedPath resolvedPath{
      resolved, QStringLiteral("/"), QStringLiteral("/Finance"),
      QStringLiteral("/")};
  int calls = 0;
  int pathCalls = 0;

  smb::core::Result<std::optional<smb::core::Connection>>
  resolve(const smb::core::Connection &, const smb::core::CredentialSecret *,
          const smb::core::OperationContext &) override {
    ++calls;
    return smb::core::Result<std::optional<smb::core::Connection>>::success(
        resolved);
  }

  smb::core::Result<std::optional<smb::core::DfsResolvedPath>>
  resolvePath(const smb::core::Connection &,
              const smb::core::CredentialSecret *, const QString &,
              const smb::core::OperationContext &) override {
    ++pathCalls;
    return smb::core::Result<
        std::optional<smb::core::DfsResolvedPath>>::success(resolvedPath);
  }
};

class MultiTargetDfsResolver final : public smb::core::DfsReferralResolver {
public:
  QVector<smb::core::DfsResolvedConnection> targets;
  QVector<smb::core::DfsResolvedPath> pathTargets;
  int targetCalls = 0;
  int pathTargetCalls = 0;

  smb::core::Result<std::optional<smb::core::Connection>>
  resolve(const smb::core::Connection &, const smb::core::CredentialSecret *,
          const smb::core::OperationContext &) override {
    if (targets.isEmpty()) {
      return smb::core::Result<std::optional<smb::core::Connection>>::success(
          std::nullopt);
    }
    return smb::core::Result<std::optional<smb::core::Connection>>::success(
        targets.first().connection);
  }

  smb::core::Result<QVector<smb::core::DfsResolvedConnection>>
  resolveTargets(const smb::core::Connection &,
                 const smb::core::CredentialSecret *,
                 const smb::core::OperationContext &) override {
    ++targetCalls;
    return smb::core::Result<QVector<smb::core::DfsResolvedConnection>>::
        success(targets);
  }

  smb::core::Result<std::optional<smb::core::DfsResolvedPath>>
  resolvePath(const smb::core::Connection &,
              const smb::core::CredentialSecret *, const QString &,
              const smb::core::OperationContext &) override {
    if (pathTargets.isEmpty()) {
      return smb::core::Result<
          std::optional<smb::core::DfsResolvedPath>>::success(std::nullopt);
    }
    return smb::core::Result<std::optional<smb::core::DfsResolvedPath>>::
        success(pathTargets.first());
  }

  smb::core::Result<QVector<smb::core::DfsResolvedPath>>
  resolvePathTargets(const smb::core::Connection &,
                     const smb::core::CredentialSecret *, const QString &,
                     const smb::core::OperationContext &) override {
    ++pathTargetCalls;
    return smb::core::Result<QVector<smb::core::DfsResolvedPath>>::success(
        pathTargets);
  }
};

smb::core::DfsResolvedConnection resolvedConnection(QString server,
                                                    int ttlSeconds = 300) {
  smb::core::DfsResolvedConnection result;
  result.connection = connection(std::move(server), QStringLiteral("RU"));
  result.ttlSeconds = ttlSeconds;
  return result;
}

smb::core::DfsResolvedPath resolvedPath(QString server,
                                        QString originalPrefix,
                                        QString targetPrefix,
                                        int ttlSeconds = 300) {
  smb::core::DfsResolvedPath result;
  result.connection = connection(std::move(server), QStringLiteral("RU"));
  result.originalPathPrefix = std::move(originalPrefix);
  result.targetPathPrefix = std::move(targetPrefix);
  result.remotePath = QStringLiteral("/");
  result.ttlSeconds = ttlSeconds;
  return result;
}

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

  void resolvesPathLevelDfsAndRebasesListedEntries() {
    RecordingSmbClient delegate;
    delegate.failOriginalListWithPathReferral = true;
    delegate.targetDirectoryEntries.insert(
        QStringLiteral("/"),
        {remoteEntry(QStringLiteral("Budget"), QStringLiteral("/Budget"))});
    StaticDfsResolver resolver;
    smb::infrastructure::DfsResolvingSmbClient client(delegate, resolver);

    const auto result = client.listDirectory(
        connection(QStringLiteral("dfs.example.com"), QStringLiteral("ru")),
        nullptr, QStringLiteral("/Finance"), {});

    QVERIFY(result.ok());
    QCOMPARE(resolver.calls, 0);
    QCOMPARE(resolver.pathCalls, 1);
    QCOMPARE(delegate.listedPaths.size(), 2);
    QCOMPARE(delegate.listedPaths.at(0).first,
             QStringLiteral("dfs.example.com"));
    QCOMPARE(delegate.listedPaths.at(0).second, QStringLiteral("/Finance"));
    QCOMPARE(delegate.listedPaths.at(1).first,
             QStringLiteral("target.example.com"));
    QCOMPARE(delegate.listedPaths.at(1).second, QStringLiteral("/"));
    QCOMPARE(result.value().size(), 1);
    QCOMPARE(result.value().at(0).remotePath,
             QStringLiteral("/Finance/Budget"));
  }

  void usesCachedPathTargetForNestedDfsNavigation() {
    RecordingSmbClient delegate;
    delegate.failOriginalListWithPathReferral = true;
    delegate.targetDirectoryEntries.insert(
        QStringLiteral("/"),
        {remoteEntry(QStringLiteral("Budget"), QStringLiteral("/Budget"))});
    delegate.targetDirectoryEntries.insert(
        QStringLiteral("/Budget"),
        {remoteEntry(QStringLiteral("2026"), QStringLiteral("/Budget/2026"))});
    StaticDfsResolver resolver;
    smb::infrastructure::DfsResolvingSmbClient client(delegate, resolver);
    const auto original =
        connection(QStringLiteral("dfs.example.com"), QStringLiteral("ru"));

    QVERIFY(client.listDirectory(original, nullptr, QStringLiteral("/Finance"),
                                 {})
                .ok());
    const auto nested = client.listDirectory(
        original, nullptr, QStringLiteral("/Finance/Budget"), {});

    QVERIFY(nested.ok());
    QCOMPARE(resolver.pathCalls, 1);
    QCOMPARE(delegate.listedPaths.size(), 3);
    QCOMPARE(delegate.listedPaths.at(2).first,
             QStringLiteral("target.example.com"));
    QCOMPARE(delegate.listedPaths.at(2).second, QStringLiteral("/Budget"));
    QCOMPARE(nested.value().size(), 1);
    QCOMPARE(nested.value().at(0).remotePath,
             QStringLiteral("/Finance/Budget/2026"));
  }

  void triesNextResolvedShareTargetWhenFirstTargetFails() {
    RecordingSmbClient delegate;
    delegate.targetServer = QStringLiteral("target2.example.com");
    MultiTargetDfsResolver resolver;
    resolver.targets = {resolvedConnection(QStringLiteral("target1.example.com")),
                        resolvedConnection(QStringLiteral("target2.example.com"))};
    smb::infrastructure::DfsResolvingSmbClient client(delegate, resolver);

    const auto result = client.checkConnection(
        connection(QStringLiteral("dfs.example.com"), QStringLiteral("ru")),
        nullptr, {});

    QVERIFY(result.ok());
    QCOMPARE(resolver.targetCalls, 1);
    QCOMPARE(delegate.checkedServers.size(), 3);
    QCOMPARE(delegate.checkedServers.at(0), QStringLiteral("dfs.example.com"));
    QCOMPARE(delegate.checkedServers.at(1), QStringLiteral("target1.example.com"));
    QCOMPARE(delegate.checkedServers.at(2), QStringLiteral("target2.example.com"));
  }

  void expiresShareTargetCacheByReferralTtl() {
    RecordingSmbClient delegate;
    MultiTargetDfsResolver resolver;
    resolver.targets = {resolvedConnection(QStringLiteral("target.example.com"), 1)};
    smb::infrastructure::DfsResolvingSmbClient client(delegate, resolver);
    const auto original =
        connection(QStringLiteral("dfs.example.com"), QStringLiteral("ru"));

    QVERIFY(client.checkConnection(original, nullptr, {}).ok());
    QTest::qWait(1100);
    QVERIFY(client.checkConnection(original, nullptr, {}).ok());

    QCOMPARE(resolver.targetCalls, 2);
    QCOMPARE(delegate.checkedServers.size(), 4);
    QCOMPARE(delegate.checkedServers.at(0), QStringLiteral("dfs.example.com"));
    QCOMPARE(delegate.checkedServers.at(1), QStringLiteral("target.example.com"));
    QCOMPARE(delegate.checkedServers.at(2), QStringLiteral("dfs.example.com"));
    QCOMPARE(delegate.checkedServers.at(3), QStringLiteral("target.example.com"));
  }

  void triesNextResolvedPathTargetWhenFirstTargetFails() {
    RecordingSmbClient delegate;
    delegate.failOriginalListWithPathReferral = true;
    delegate.targetServer = QStringLiteral("target2.example.com");
    delegate.targetDirectoryEntries.insert(
        QStringLiteral("/"),
        {remoteEntry(QStringLiteral("Budget"), QStringLiteral("/Budget"))});
    MultiTargetDfsResolver resolver;
    resolver.pathTargets = {
        resolvedPath(QStringLiteral("target1.example.com"),
                     QStringLiteral("/Finance"), QStringLiteral("/")),
        resolvedPath(QStringLiteral("target2.example.com"),
                     QStringLiteral("/Finance"), QStringLiteral("/"))};
    smb::infrastructure::DfsResolvingSmbClient client(delegate, resolver);

    const auto result = client.listDirectory(
        connection(QStringLiteral("dfs.example.com"), QStringLiteral("ru")),
        nullptr, QStringLiteral("/Finance"), {});

    QVERIFY(result.ok());
    QCOMPARE(resolver.pathTargetCalls, 1);
    QCOMPARE(delegate.listedPaths.size(), 3);
    QCOMPARE(delegate.listedPaths.at(0).first,
             QStringLiteral("dfs.example.com"));
    QCOMPARE(delegate.listedPaths.at(1).first,
             QStringLiteral("target1.example.com"));
    QCOMPARE(delegate.listedPaths.at(2).first,
             QStringLiteral("target2.example.com"));
    QCOMPARE(result.value().at(0).remotePath,
             QStringLiteral("/Finance/Budget"));
  }
};

QTEST_MAIN(DfsResolvingSmbClientTest)

#include "test_dfs_resolving_smb_client.moc"
