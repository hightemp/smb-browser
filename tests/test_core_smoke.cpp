#include "core/AppInfo.h"

#include <QtTest/QtTest>

class CoreSmokeTest final : public QObject {
  Q_OBJECT

private slots:
  void applicationMetadataIsAvailable() {
    QCOMPARE(smb::core::applicationName(), QStringLiteral("SMB Browser"));
    QVERIFY(!smb::core::applicationVersion().isEmpty());
  }
};

QTEST_MAIN(CoreSmokeTest)

#include "test_core_smoke.moc"
