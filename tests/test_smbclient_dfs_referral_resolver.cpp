#include "smb/SmbclientDfsReferralResolver.h"

#include <QtTest/QtTest>

class SmbclientDfsReferralResolverTest final : public QObject {
  Q_OBJECT

private slots:
  void parsesShowconnectTarget() {
    const auto target = smb::infrastructure::parseSmbclientShowconnectTarget(
        QStringLiteral("Domain=[V-TELL]\n"
                       "//RU-PM-FS-P01.v-tell.com/RU\n"));

    QVERIFY(target.has_value());
    QCOMPARE(target->server, QStringLiteral("RU-PM-FS-P01.v-tell.com"));
    QCOMPARE(target->share, QStringLiteral("RU"));
  }

  void ignoresOutputWithoutTarget() {
    const auto target = smb::infrastructure::parseSmbclientShowconnectTarget(
        QStringLiteral("session setup failed: NT_STATUS_LOGON_FAILURE\n"));

    QVERIFY(!target.has_value());
  }
};

QTEST_MAIN(SmbclientDfsReferralResolverTest)

#include "test_smbclient_dfs_referral_resolver.moc"
