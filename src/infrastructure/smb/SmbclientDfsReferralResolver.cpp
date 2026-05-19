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
#include <QStringList>
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

QString normalizeRemotePath(QString remotePath) {
  remotePath = remotePath.trimmed();
  remotePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
  while (remotePath.contains(QStringLiteral("//"))) {
    remotePath.replace(QStringLiteral("//"), QStringLiteral("/"));
  }
  if (remotePath.isEmpty()) {
    remotePath = QStringLiteral("/");
  }
  if (!remotePath.startsWith(QLatin1Char('/'))) {
    remotePath.prepend(QLatin1Char('/'));
  }
  while (remotePath.size() > 1 && remotePath.endsWith(QLatin1Char('/'))) {
    remotePath.chop(1);
  }
  return remotePath;
}

QStringList remotePathSegments(const QString &remotePath) {
  const auto normalized = normalizeRemotePath(remotePath);
  if (normalized == QStringLiteral("/")) {
    return {};
  }
  return normalized.mid(1).split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

QString pathFromSegments(const QStringList &segments, int count) {
  if (count <= 0) {
    return QStringLiteral("/");
  }
  return QStringLiteral("/%1")
      .arg(segments.mid(0, count).join(QLatin1Char('/')));
}

QString pathFromSegments(const QStringList &segments, int first, int count) {
  if (count <= 0 || first >= segments.size()) {
    return QStringLiteral("/");
  }
  return QStringLiteral("/%1")
      .arg(segments.mid(first, count).join(QLatin1Char('/')));
}

QString smbclientQuotedPath(const QString &remotePath) {
  auto relative = normalizeRemotePath(remotePath);
  if (relative.startsWith(QLatin1Char('/'))) {
    relative.remove(0, 1);
  }
  relative.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
  relative.replace(QStringLiteral("\""), QStringLiteral("\\\""));
  return relative;
}

QString smbclientCommandForPath(const QString &remotePath) {
  const auto normalized = normalizeRemotePath(remotePath);
  if (normalized == QStringLiteral("/")) {
    return QStringLiteral("showconnect");
  }
  return QStringLiteral("cd \"%1\"; showconnect")
      .arg(smbclientQuotedPath(normalized));
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

smb::core::Result<std::optional<SmbclientDfsTarget>>
SmbclientDfsReferralResolver::resolveTarget(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context, const QString &remotePath,
    bool commandFailureIsError) {
  if (isCancelled(context)) {
    return smb::core::Result<std::optional<SmbclientDfsTarget>>::failure(
        resolverError(smb::core::ErrorCode::OperationCancelled,
                      QStringLiteral("DFS referral resolution was cancelled."),
                      false));
  }

  const auto smbclient = findSmbclientExecutable();
  if (smbclient.isEmpty()) {
    return smb::core::Result<std::optional<SmbclientDfsTarget>>::success(
        std::nullopt);
  }

  QTemporaryFile credentialsFile;
  QStringList args;
  args << uncPath(connection) << QStringLiteral("-m") << QStringLiteral("SMB3")
       << QStringLiteral("-c") << smbclientCommandForPath(remotePath);

  QByteArray credentials;
  if (connection.authType == smb::core::AuthType::Password ||
      connection.authType == smb::core::AuthType::Guest) {
    credentials = credentialFileContent(connection, secret);
    if (!credentialsFile.open()) {
      return smb::core::Result<std::optional<SmbclientDfsTarget>>::failure(
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
      return smb::core::Result<std::optional<SmbclientDfsTarget>>::failure(
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
    return smb::core::Result<std::optional<SmbclientDfsTarget>>::success(
        std::nullopt);
  }

  QProcess process;
  process.setProcessChannelMode(QProcess::MergedChannels);
  process.start(smbclient, args);
  if (!process.waitForStarted(3000)) {
    return smb::core::Result<std::optional<SmbclientDfsTarget>>::failure(
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
      return smb::core::Result<std::optional<SmbclientDfsTarget>>::failure(
          resolverError(
              smb::core::ErrorCode::OperationCancelled,
              QStringLiteral("DFS referral resolution was cancelled."), false));
    }
    if (timer.elapsed() > m_timeoutMs) {
      process.kill();
      process.waitForFinished(kPollIntervalMs);
      return smb::core::Result<std::optional<SmbclientDfsTarget>>::failure(
          resolverError(smb::core::ErrorCode::Timeout,
                        QStringLiteral("DFS referral resolution timed out."),
                        true));
    }
    process.waitForFinished(kPollIntervalMs);
  }

  const auto output = process.readAll();
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (!commandFailureIsError) {
      return smb::core::Result<std::optional<SmbclientDfsTarget>>::success(
          std::nullopt);
    }
    return smb::core::Result<std::optional<SmbclientDfsTarget>>::failure(
        resolverError(
            smb::core::ErrorCode::ShareUnavailable,
            QStringLiteral("smbclient DFS referral resolution failed: %1")
                .arg(sanitizedOutput(output, secret)),
            false));
  }

  const auto target =
      parseSmbclientShowconnectTarget(QString::fromUtf8(output));
  return smb::core::Result<std::optional<SmbclientDfsTarget>>::success(target);
}

smb::core::Result<std::optional<smb::core::Connection>>
SmbclientDfsReferralResolver::resolve(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret,
    const smb::core::OperationContext &context) {
  const auto targetResult = resolveTarget(
      connection, secret, context, QStringLiteral("/"), true);
  if (!targetResult.ok()) {
    return smb::core::Result<std::optional<smb::core::Connection>>::failure(
        targetResult.error());
  }

  const auto target = targetResult.value();
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

smb::core::Result<std::optional<smb::core::DfsResolvedPath>>
SmbclientDfsReferralResolver::resolvePath(
    const smb::core::Connection &connection,
    const smb::core::CredentialSecret *secret, const QString &remotePath,
    const smb::core::OperationContext &context) {
  const auto normalized = normalizeRemotePath(remotePath);
  if (normalized == QStringLiteral("/")) {
    const auto resolved = resolve(connection, secret, context);
    if (!resolved.ok()) {
      return smb::core::Result<
          std::optional<smb::core::DfsResolvedPath>>::failure(resolved.error());
    }
    if (!resolved.value().has_value()) {
      return smb::core::Result<
          std::optional<smb::core::DfsResolvedPath>>::success(std::nullopt);
    }

    smb::core::DfsResolvedPath path;
    path.connection = resolved.value().value();
    path.remotePath = QStringLiteral("/");
    path.originalPathPrefix = QStringLiteral("/");
    path.targetPathPrefix = QStringLiteral("/");
    return smb::core::Result<
        std::optional<smb::core::DfsResolvedPath>>::success(std::move(path));
  }

  const auto segments = remotePathSegments(normalized);
  for (int i = 0; i < segments.size(); ++i) {
    const auto prefix = pathFromSegments(segments, i + 1);
    const auto targetResult =
        resolveTarget(connection, secret, context, prefix, false);
    if (!targetResult.ok()) {
      return smb::core::Result<
          std::optional<smb::core::DfsResolvedPath>>::failure(
          targetResult.error());
    }
    const auto target = targetResult.value();
    if (!target.has_value() || isSameShare(connection, target.value())) {
      continue;
    }

    smb::core::DfsResolvedPath path;
    path.connection = connection;
    path.connection.server = target->server;
    path.connection.share = target->share;
    path.connection.normalizedUri = normalizedUri(target->server, target->share);
    path.remotePath = pathFromSegments(segments, i + 1,
                                       segments.size() - i - 1);
    path.originalPathPrefix = prefix;
    path.targetPathPrefix = QStringLiteral("/");
    return smb::core::Result<
        std::optional<smb::core::DfsResolvedPath>>::success(std::move(path));
  }

  return smb::core::Result<std::optional<smb::core::DfsResolvedPath>>::success(
      std::nullopt);
}

} // namespace smb::infrastructure
