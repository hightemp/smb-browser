#include "smb/NativeDfsReferralResolver.h"

#include <QtTest/QtTest>

class NativeDfsReferralResolverTest final : public QObject {
  Q_OBJECT

private slots:
  void parsesUncTargetShare() {
    const auto target = smb::infrastructure::parseNativeDfsReferralTarget(
        QStringLiteral("\\\\fs01.example.com\\Finance"));

    QVERIFY(target.has_value());
    QCOMPARE(target->server, QStringLiteral("fs01.example.com"));
    QCOMPARE(target->share, QStringLiteral("Finance"));
    QCOMPARE(target->targetPathPrefix, QStringLiteral("/"));
  }

  void parsesUncTargetWithPathPrefix() {
    const auto target = smb::infrastructure::parseNativeDfsReferralTarget(
        QStringLiteral("\\\\fs01.example.com\\Finance\\Dept\\Reports"));

    QVERIFY(target.has_value());
    QCOMPARE(target->server, QStringLiteral("fs01.example.com"));
    QCOMPARE(target->share, QStringLiteral("Finance"));
    QCOMPARE(target->targetPathPrefix, QStringLiteral("/Dept/Reports"));
  }

  void rejectsInvalidTargets() {
    QVERIFY(!smb::infrastructure::parseNativeDfsReferralTarget(
                 QStringLiteral("not-a-unc-path"))
                 .has_value());
    QVERIFY(!smb::infrastructure::parseNativeDfsReferralTarget(
                 QStringLiteral("\\\\server"))
                 .has_value());
  }
};

QTEST_MAIN(NativeDfsReferralResolverTest)

#include "test_native_dfs_referral_resolver.moc"
