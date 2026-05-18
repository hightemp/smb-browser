#include "core/PathNormalizer.h"

#include <QStringList>
#include <QUrl>
#include <utility>

namespace smb::core {

namespace {

QString cleanedPathInput(QString input) {
  return input.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
}

Result<NormalizedSmbPath> invalidPath(QString details) {
  return Result<NormalizedSmbPath>::failure(
      AppError::fromCode(ErrorCode::InvalidPath, ErrorCategory::Validation,
                         std::move(details), false));
}

Result<ParsedIdentity> invalidIdentity(QString details) {
  return Result<ParsedIdentity>::failure(
      AppError::fromCode(ErrorCode::InvalidPath, ErrorCategory::Validation,
                         std::move(details), false));
}

QString joinInitialPath(const QStringList &parts, int firstIndex) {
  if (parts.size() <= firstIndex) {
    return {};
  }

  return parts.mid(firstIndex).join(QLatin1Char('/'));
}

} // namespace

Result<NormalizedSmbPath>
PathNormalizer::normalizeSmbPath(const QString &inputPath) {
  const auto input = inputPath.trimmed();
  if (input.isEmpty()) {
    return invalidPath(QStringLiteral("SMB path is empty."));
  }

  QString server;
  QString share;
  QString initialRemotePath;

  if (input.startsWith(QStringLiteral("smb://"), Qt::CaseInsensitive)) {
    const QUrl url(input);
    if (!url.isValid() ||
        url.scheme().compare(QStringLiteral("smb"), Qt::CaseInsensitive) != 0) {
      return invalidPath(QStringLiteral("SMB URI is invalid."));
    }
    if (!url.userName().isEmpty() || !url.password().isEmpty()) {
      return invalidPath(
          QStringLiteral("SMB URI must not contain credentials."));
    }

    server = url.host().trimmed();
    const auto pathParts =
        url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!pathParts.isEmpty()) {
      share = pathParts.at(0).trimmed();
      initialRemotePath = joinInitialPath(pathParts, 1);
    }
  } else {
    auto path = cleanedPathInput(input);
    while (path.startsWith(QLatin1Char('/'))) {
      path.remove(0, 1);
    }

    const auto pathParts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (pathParts.size() >= 2) {
      server = pathParts.at(0).trimmed();
      share = pathParts.at(1).trimmed();
      initialRemotePath = joinInitialPath(pathParts, 2);
    }
  }

  if (server.isEmpty()) {
    return invalidPath(QStringLiteral("SMB server is missing."));
  }
  if (share.isEmpty()) {
    return invalidPath(QStringLiteral("SMB share is missing."));
  }

  NormalizedSmbPath result;
  result.inputPath = inputPath;
  result.normalizedUri = QStringLiteral("smb://%1/%2").arg(server, share);
  result.server = server;
  result.share = share;
  result.initialRemotePath = initialRemotePath;

  return Result<NormalizedSmbPath>::success(std::move(result));
}

Result<ParsedIdentity>
PathNormalizer::normalizeIdentity(AuthType authType,
                                  const QString &usernameInput,
                                  const QString &domainInput) {
  ParsedIdentity identity;
  identity.authType = authType;

  if (authType == AuthType::Guest || authType == AuthType::Anonymous ||
      authType == AuthType::CurrentUser) {
    identity.domain = domainInput.trimmed();
    identity.username = usernameInput.trimmed();
    return Result<ParsedIdentity>::success(std::move(identity));
  }

  auto username = usernameInput.trimmed();
  auto domain = domainInput.trimmed();

  if (username.isEmpty()) {
    return invalidIdentity(
        QStringLiteral("Username is required for password authentication."));
  }

  const auto slashIndex = username.indexOf(QLatin1Char('\\'));
  const auto atIndex = username.indexOf(QLatin1Char('@'));

  if (slashIndex > 0) {
    const auto parsedDomain = username.left(slashIndex).trimmed();
    const auto parsedUsername = username.mid(slashIndex + 1).trimmed();
    if (parsedUsername.isEmpty()) {
      return invalidIdentity(QStringLiteral("Username part is empty."));
    }
    if (!domain.isEmpty() &&
        domain.compare(parsedDomain, Qt::CaseInsensitive) != 0) {
      return invalidIdentity(
          QStringLiteral("Explicit domain conflicts with username domain."));
    }
    domain = parsedDomain;
    username = parsedUsername;
  } else if (atIndex > 0) {
    const auto parsedUsername = username.left(atIndex).trimmed();
    const auto parsedDomain = username.mid(atIndex + 1).trimmed();
    if (parsedUsername.isEmpty() || parsedDomain.isEmpty()) {
      return invalidIdentity(
          QStringLiteral("User principal name is incomplete."));
    }
    if (!domain.isEmpty() &&
        domain.compare(parsedDomain, Qt::CaseInsensitive) != 0) {
      return invalidIdentity(
          QStringLiteral("Explicit domain conflicts with username domain."));
    }
    username = parsedUsername;
    domain = parsedDomain;
  }

  identity.domain = domain;
  identity.username = username;

  return Result<ParsedIdentity>::success(std::move(identity));
}

} // namespace smb::core
