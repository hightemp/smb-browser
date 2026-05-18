#include "smb/Libsmb2ErrorMapper.h"

#include <QtTest/QtTest>
#include <cerrno>

class Libsmb2ErrorMapperTest final : public QObject {
  Q_OBJECT

private slots:
  void mapsConnectionErrors() {
    using smb::infrastructure::Libsmb2ErrorContext;
    using smb::infrastructure::mapLibsmb2Error;

    QVERIFY(
        mapLibsmb2Error(-EACCES, QString(), Libsmb2ErrorContext::Connection) ==
        smb::core::ErrorCode::AuthenticationFailed);
    QVERIFY(mapLibsmb2Error(-ETIMEDOUT, QString(),
                            Libsmb2ErrorContext::Connection) ==
            smb::core::ErrorCode::Timeout);
    QVERIFY(mapLibsmb2Error(-ECONNREFUSED, QString(),
                            Libsmb2ErrorContext::Connection) ==
            smb::core::ErrorCode::ServerUnavailable);
    QVERIFY(
        mapLibsmb2Error(-EPROTO, QString(), Libsmb2ErrorContext::Connection) ==
        smb::core::ErrorCode::ProtocolUnsupported);
    QVERIFY(mapLibsmb2Error(-EIO, QStringLiteral("Could not resolve name"),
                            Libsmb2ErrorContext::Connection) ==
            smb::core::ErrorCode::DnsError);
  }

  void mapsDirectoryErrors() {
    using smb::infrastructure::Libsmb2ErrorContext;
    using smb::infrastructure::mapLibsmb2Error;

    QVERIFY(
        mapLibsmb2Error(-EACCES, QString(), Libsmb2ErrorContext::Directory) ==
        smb::core::ErrorCode::PermissionDenied);
    QVERIFY(
        mapLibsmb2Error(-ENOENT, QString(), Libsmb2ErrorContext::Directory) ==
        smb::core::ErrorCode::FileNotFound);
  }

  void mapsFileOperationErrors() {
    using smb::infrastructure::Libsmb2ErrorContext;
    using smb::infrastructure::mapLibsmb2Error;

    QVERIFY(mapLibsmb2Error(-EACCES, QString(),
                            Libsmb2ErrorContext::FileOperation) ==
            smb::core::ErrorCode::PermissionDenied);
    QVERIFY(mapLibsmb2Error(-EEXIST, QString(),
                            Libsmb2ErrorContext::FileOperation) ==
            smb::core::ErrorCode::AlreadyExists);
    QVERIFY(mapLibsmb2Error(-ENOTEMPTY, QString(),
                            Libsmb2ErrorContext::FileOperation) ==
            smb::core::ErrorCode::DirectoryNotEmpty);
    QVERIFY(mapLibsmb2Error(-ENOTDIR, QString(),
                            Libsmb2ErrorContext::FileOperation) ==
            smb::core::ErrorCode::FileNotFound);
    QVERIFY(
        mapLibsmb2Error(-EIO, QString(), Libsmb2ErrorContext::FileOperation) ==
        smb::core::ErrorCode::NetworkError);
  }
};

QTEST_MAIN(Libsmb2ErrorMapperTest)

#include "test_libsmb2_error_mapper.moc"
