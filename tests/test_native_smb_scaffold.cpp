#include "SmbNative.h"

#include <QtTest/QtTest>

#include <cstdint>
#include <type_traits>
#include <vector>

class NativeSmbScaffoldTest final : public QObject {
  Q_OBJECT

private slots:
  void buildPolicyIsCleanRoomOnly() {
    const auto &policy = smb::native_smb::buildPolicy();

    QVERIFY(policy.cleanRoomOnly);
    QVERIFY(!policy.usesSambaCode);
    QVERIFY(!policy.requiresExternalSmbRuntime);
    QVERIFY(!policy.supportsSmb1);
    QVERIFY(policy.dialectFamily ==
            smb::native_smb::DialectFamily::Smb2AndSmb3);
  }

  void publicTypesCaptureInitialApiBoundary() {
    smb::native_smb::ConnectionConfig config;
    config.server = "server";
    config.share = "share";
    config.normalizedUri = "smb://server/share";
    config.domain = "DOMAIN";
    config.username = "user";
    config.authMode = smb::native_smb::AuthMode::Password;
    config.signing = smb::native_smb::SecurityPolicy::Required;
    config.encryption = smb::native_smb::SecurityPolicy::Preferred;

    QCOMPARE(QString::fromStdString(config.server), QStringLiteral("server"));
    QCOMPARE(QString::fromStdString(config.share), QStringLiteral("share"));
    QVERIFY(config.dialectFamily ==
            smb::native_smb::DialectFamily::Smb2AndSmb3);
  }

  void secretBufferIsMoveOnly() {
    static_assert(!std::is_copy_constructible<smb::native_smb::SecretBuffer>::value,
                  "SecretBuffer must not be copy constructible");
    static_assert(!std::is_copy_assignable<smb::native_smb::SecretBuffer>::value,
                  "SecretBuffer must not be copy assignable");
    static_assert(std::is_move_constructible<smb::native_smb::SecretBuffer>::value,
                  "SecretBuffer must be move constructible");

    smb::native_smb::SecretBuffer secret(
        std::vector<std::uint8_t>{'s', 'e', 'c', 'r', 'e', 't'});
    QVERIFY(!secret.empty());

    auto moved = std::move(secret);
    QCOMPARE(moved.bytes().size(), std::size_t{6});
    moved.clear();
    QVERIFY(moved.empty());
  }

  void cancellationTokenReportsState() {
    smb::native_smb::CancellationToken token;
    QVERIFY(!token.isCancellationRequested());
    token.cancel();
    QVERIFY(token.isCancellationRequested());
  }

  void operationContextSupportsCancellationCallback() {
    bool cancelled = false;
    smb::native_smb::OperationContext context;
    context.cancellationCallback = [&cancelled]() { return cancelled; };

    QVERIFY(!smb::native_smb::isCancellationRequested(context));
    cancelled = true;
    QVERIFY(smb::native_smb::isCancellationRequested(context));
  }
};

QTEST_MAIN(NativeSmbScaffoldTest)

#include "test_native_smb_scaffold.moc"
