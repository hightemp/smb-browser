#include "smb/SmbclientDfsReferralResolver.h"

#include "core/LogSanitizer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDevice>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <algorithm>

namespace smb::infrastructure {

namespace {

constexpr int kPollIntervalMs = 100;

bool isCancelled(const smb::core::OperationContext &context) {
  return context.cancellationToken != nullptr &&
         context.cancellationToken->isCancellationRequested();
}

smb::core::AppError resolverError(smb::core::ErrorCode code,
                                  const QString &details, bool retryable) {
  return smb::core::AppError::fromCode(code, smb::core::ErrorCategory::Smb,
                                       details, retryable);
}

QString uncPath(const smb::core::Connection &connection) {
  return QStringLiteral("//%1/%2").arg(connection.server, connection.share);
}

QString normalizedUri(const QString &server, const QString &share) {
  return QStringLiteral("smb://%1/%2").arg(server, share);
}

bool isSameShare(const smb::core::Connection &connection,
                 const SmbclientDfsTarget &target) {
  return connection.server.compare(target.server, Qt::CaseInsensitive) == 0 &&
         connection.share.compare(target.share, Qt::CaseInsensitive) == 0;
}

QByteArray credentialFileContent(const smb::core::Connection &connection,
                                 const smb::core::CredentialSecret *secret) {
  QByteArray content;
  content += "username=";
  content += connection.username.toUtf8();
  content += '\n';
  content += "password=";
  if (secret != nullptr) {
    content += secret->bytes;
  }
  content += '\n';
  if (!connection.domain.isEmpty()) {
    content += "domain=";
    content += connection.domain.toUtf8();
    content += '\n';
  }
  return content;
}

QString sanitizedOutput(const QByteArray &output,
                        const smb::core::CredentialSecret *secret) {
  smb::core::LogSanitizer sanitizer;
  if (secret != nullptr && !secret->bytes.isEmpty()) {
    sanitizer.addSecretValue(QString::fromUtf8(secret->bytes));
  }
  return sanitizer.sanitize(QString::fromUtf8(output));
}

QString findSmbclientExecutable() {
#ifdef Q_OS_WIN
  const auto executableName = QStringLiteral("smbclient.exe");
#else
  const auto executableName = QStringLiteral("smbclient");
#endif

  const auto fromPath = QStandardPaths::findExecutable(executableName);
  if (!fromPath.isEmpty()) {
    return fromPath;
  }

  const QDir appDir(QCoreApplication::applicationDirPath());
  const QStringList candidates{
      appDir.filePath(executableName),
      appDir.filePath(QStringLiteral("bin/%1").arg(executableName)),
      appDir.filePath(QStringLiteral("../bin/%1").arg(executableName)),
      appDir.filePath(
          QStringLiteral("../Resources/bin/%1").arg(executableName)),
  };

  for (const auto &candidate : candidates) {
    const QFileInfo info(candidate);
    if (info.exists() && info.isFile() && info.isExecutable()) {
      return info.absoluteFilePath();
    }
  }

  return {};
}

} // namespace

std::optional<SmbclientDfsTarget>
parseSmbclientShowconnectTarget(const QString &output) {
  static const QRegularExpression targetPattern(
      QStringLiteral(R"(^\s*//([^/\s]+)/(.+?)\s*$)"));

  const auto lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (auto line : lines) {
    line = line.trimmed();
    const auto match = targetPattern.match(line);
    if (!match.hasMatch()) {
      continue;
    }

    SmbclientDfsTarget target;
    target.server = match.captured(1).trimmed();
    target.share = match.captured(2).trimmed();
    if (!target.server.isEmpty() && !target.share.isEmpty()) {
      return target;
    }
  }

  return std::nullopt;
}

SmbclientDfsReferralResolver::SmbclientDfsReferralResolver(int timeoutSeconds)
    : m_timeoutMs(std::max(1, timeoutSeconds) * 1000) {}

smb::core::Result<std::optional<smb::core::Connection>>
SmbclientDfsReferralResolver::resolve(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context) {
  if (isCancelled(context)) {
    return smb::core::Result<std::optional<smb::core::Connection>>::failure(
        resolverError(smb::core::ErrorCode::OperationCancelled,
                      QStringLiteral("DFS referral resolution was cancelled."),
                      false));
  }

  const auto smbclient = findSmbclientExecutable();
  if (smbclient.isEmpty()) {
    return smb::core::Result<std::optional<smb::core::Connection>>::success(
        std::nullopt);
  }

  QTemporaryFile credentialsFile;
  QStringList args;
  args << uncPath(connection) << QStringLiteral("-m") << QStringLiteral("SMB3")
       << QStringLiteral("-c") << QStringLiteral("showconnect");

  QByteArray credentials;
  if (connection.authType == smb::core::AuthType::Password ||
      connection.authType == smb::core::AuthType::Guest) {
    credentials = credentialFileContent(connection, secret);
    if (!credentialsFile.open()) {
      return smb::core::Result<std::optional<smb::core::Connection>>::failure(
          resolverError(
              smb::core::ErrorCode::LocalIoError,
              QStringLiteral("Unable to create temporary credentials file for "
                             "DFS referral resolution."),
              false));
    }
    credentialsFile.setPermissions(QFileDevice::ReadOwner |
                                   QFileDevice::WriteOwner);
    if (credentialsFile.write(credentials) != credentials.size() ||
        !credentialsFile.flush()) {
      credentials.fill('\0');
      return smb::core::Result<std::optional<smb::core::Connection>>::failure(
          resolverError(
              smb::core::ErrorCode::LocalIoError,
              QStringLiteral("Unable to write temporary credentials file for "
                             "DFS referral resolution."),
              false));
    }
    credentials.fill('\0');
    credentialsFile.close();
    args << QStringLiteral("-A") << credentialsFile.fileName();
  } else if (connection.authType == smb::core::AuthType::Anonymous) {
    args << QStringLiteral("-N");
  } else {
    return smb::core::Result<std::optional<smb::core::Connection>>::success(
        std::nullopt);
  }

  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(smbclient, args);
  if (!process.waitForStarted(3000)) {
    return smb::core::Result<std::optional<smb::core::Connection>>::failure(
        resolverError(smb::core::ErrorCode::ServerUnavailable,
                      QStringLiteral("Unable to start smbclient for DFS "
                                     "referral resolution."),
                      true));
  }

  QElapsedTimer timer;
  timer.start();
  while (process.state() != QProcess::NotRunning) {
    if (isCancelled(context)) {
      process.kill();
      process.waitForFinished(kPollIntervalMs);
      return smb::core::Result<std::optional<smb::core::Connection>>::failure(
          resolverError(
              smb::core::ErrorCode::OperationCancelled,
              QStringLiteral("DFS referral resolution was cancelled."), false));
    }
    if (timer.elapsed() > m_timeoutMs) {
      process.kill();
      process.waitForFinished(kPollIntervalMs);
      return smb::core::Result<std::optional<smb::core::Connection>>::failure(
          resolverError(smb::core::ErrorCode::Timeout,
                        QStringLiteral("DFS referral resolution timed out."),
                        true));
    }
    process.waitForFinished(kPollIntervalMs);
  }

  const auto output = process.readAll();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return smb::core::Result<std::optional<smb::core::Connection>>::failure(
        resolverError(
            smb::core::ErrorCode::ShareUnavailable,
            QStringLiteral("smbclient DFS referral resolution failed: %1")
                .arg(sanitizedOutput(output, secret)),
            false));
  }

  const auto target =
      parseSmbclientShowconnectTarget(QString::fromUtf8(output));
  if (!target.has_value() || isSameShare(connection, target.value())) {
    return smb::core::Result<std::optional<smb::core::Connection>>::success(
        std::nullopt);
  }

  auto resolved = connection;
  resolved.server = target->server;
  resolved.share = target->share;
  resolved.normalizedUri = normalizedUri(target->server, target->share);

  return smb::core::Result<std::optional<smb::core::Connection>>::success(
      std::move(resolved));
}

} // namespace smb::infrastructure
