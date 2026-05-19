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

  void mapsProtocolAndShareDiagnosticsFromDetails() {
    using smb::infrastructure::Libsmb2ErrorContext;
    using smb::infrastructure::mapLibsmb2Error;

    QVERIFY(mapLibsmb2Error(-EIO,
                            QStringLiteral("SMB dialect negotiation failed"),
                            Libsmb2ErrorContext::Connection) ==
            smb::core::ErrorCode::ProtocolUnsupported);
    QVERIFY(mapLibsmb2Error(-EIO, QStringLiteral("NT_STATUS_BAD_NETWORK_NAME"),
                            Libsmb2ErrorContext::Directory) ==
            smb::core::ErrorCode::ShareUnavailable);
    QVERIFY(mapLibsmb2Error(-EIO,
                            QStringLiteral("SMB2_STATUS_PATH_NOT_COVERED "
                                           "ntstatus=0xc0000257"),
                            Libsmb2ErrorContext::Directory) ==
            smb::core::ErrorCode::ShareUnavailable);
    QVERIFY(mapLibsmb2Error(-EIO, QStringLiteral("STATUS_PATH_NOT_COVERED"),
                            Libsmb2ErrorContext::FileOperation) ==
            smb::core::ErrorCode::ShareUnavailable);
    QVERIFY(
        mapLibsmb2Error(
            -EIO,
            QStringLiteral("Tree Connect failed with STATUS_BAD_NETWORK_NAME"),
            Libsmb2ErrorContext::Connection) ==
        smb::core::ErrorCode::ShareUnavailable);
    QVERIFY(mapLibsmb2Error(-EIO, QStringLiteral("NT_STATUS_LOGON_FAILURE"),
                            Libsmb2ErrorContext::Connection) ==
            smb::core::ErrorCode::AuthenticationFailed);
  }

  void buildsSanitizedActionableAppError() {
    using smb::infrastructure::Libsmb2ErrorContext;
    using smb::infrastructure::makeLibsmb2Error;

    smb::core::LogSanitizer sanitizer;
    sanitizer.addSecretValue(QStringLiteral("plain-secret"));

    const auto error = makeLibsmb2Error(
        -EACCES, QStringLiteral("libsmb2 connect failed password=plain-secret"),
        Libsmb2ErrorContext::Connection, sanitizer);

    QVERIFY(error.code == smb::core::ErrorCode::AuthenticationFailed);
    QVERIFY(error.category == smb::core::ErrorCategory::Smb);
    QVERIFY(error.userMessage.contains(QStringLiteral("Check username")));
    QVERIFY(error.sanitizedTechnicalDetails.contains(QStringLiteral("hint=")));
    QVERIFY(!error.sanitizedTechnicalDetails.contains(
        QStringLiteral("plain-secret")));
  }

  void shareUnavailableHintMentionsDfsForConnectionErrors() {
    using smb::infrastructure::Libsmb2ErrorContext;
    using smb::infrastructure::makeLibsmb2Error;

    smb::core::LogSanitizer sanitizer;
    const auto error = makeLibsmb2Error(
        -EIO,
        QStringLiteral("Tree Connect failed with STATUS_BAD_NETWORK_NAME"),
        Libsmb2ErrorContext::Connection, sanitizer);

    QVERIFY(error.code == smb::core::ErrorCode::ShareUnavailable);
    QVERIFY(error.userMessage.contains(QStringLiteral("DFS namespace")));
    QVERIFY(error.sanitizedTechnicalDetails.contains(
        QStringLiteral("DFS resolver")));
  }

  void shareUnavailableHintMentionsDfsForDirectoryErrors() {
    using smb::infrastructure::Libsmb2ErrorContext;
    using smb::infrastructure::makeLibsmb2Error;

    smb::core::LogSanitizer sanitizer;
    const auto error = makeLibsmb2Error(
        -EIO, QStringLiteral("SMB2_STATUS_PATH_NOT_COVERED ntstatus=0xc0000257"),
        Libsmb2ErrorContext::Directory, sanitizer);

    QVERIFY(error.code == smb::core::ErrorCode::ShareUnavailable);
    QVERIFY(error.userMessage.contains(QStringLiteral("DFS namespace")));
    QVERIFY(error.sanitizedTechnicalDetails.contains(
        QStringLiteral("DFS resolver")));
  }
};

QTEST_MAIN(Libsmb2ErrorMapperTest)

#include "test_libsmb2_error_mapper.moc"
