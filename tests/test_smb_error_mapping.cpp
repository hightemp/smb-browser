#include "core/SmbError.h"

#include <QtTest/QtTest>

class SmbErrorMappingTest final : public QObject {
  Q_OBJECT

private slots:
  void mapsCommonSmbErrorsToConnectionStatuses() {
    QVERIFY(
        smb::core::connectionStatusForSmbError(smb::core::SmbErrorCode::None) ==
        smb::core::ConnectionStatus::Available);
    QVERIFY(smb::core::connectionStatusForSmbError(
                smb::core::SmbErrorCode::DnsError) ==
            smb::core::ConnectionStatus::DnsError);
    QVERIFY(smb::core::connectionStatusForSmbError(
                smb::core::SmbErrorCode::AuthenticationFailed) ==
            smb::core::ConnectionStatus::AuthenticationFailed);
    QVERIFY(smb::core::connectionStatusForSmbError(
                smb::core::SmbErrorCode::PermissionDenied) ==
            smb::core::ConnectionStatus::PermissionDenied);
    QVERIFY(smb::core::connectionStatusForSmbError(
                smb::core::SmbErrorCode::Timeout) ==
            smb::core::ConnectionStatus::Timeout);
    QVERIFY(smb::core::connectionStatusForSmbError(
                smb::core::SmbErrorCode::ProtocolUnsupported) ==
            smb::core::ConnectionStatus::ProtocolUnsupported);
  }

  void statusNamesAreStable() {
    QCOMPARE(smb::core::toString(smb::core::ConnectionStatus::NotChecked),
             QStringLiteral("not_checked"));
    QCOMPARE(
        smb::core::toString(smb::core::ConnectionStatus::ServerUnavailable),
        QStringLiteral("server_unavailable"));
  }
};

QTEST_MAIN(SmbErrorMappingTest)

#include "test_smb_error_mapping.moc"
