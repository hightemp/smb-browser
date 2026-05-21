#include "smb/SmbclientDfsReferralResolver.h"

#include <QtTest/QtTest>

class SmbclientDfsReferralResolverTest final : public QObject {
  Q_OBJECT

private slots:
  void parsesShowconnectTarget() {
    const auto target = smb::infrastructure::parseSmbclientShowconnectTarget(
        QStringLiteral("Domain=[EXAMPLE]\n"
                       "//files01.example.test/share\n"));

    QVERIFY(target.has_value());
    QCOMPARE(target->server, QStringLiteral("files01.example.test"));
    QCOMPARE(target->share, QStringLiteral("share"));
  }

  void ignoresOutputWithoutTarget() {
    const auto target = smb::infrastructure::parseSmbclientShowconnectTarget(
        QStringLiteral("session setup failed: NT_STATUS_LOGON_FAILURE\n"));

    QVERIFY(!target.has_value());
  }
};

QTEST_MAIN(SmbclientDfsReferralResolverTest)

#include "test_smbclient_dfs_referral_resolver.moc"
