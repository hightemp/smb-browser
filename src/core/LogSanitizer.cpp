#include "core/LogSanitizer.h"

#include <QRegularExpression>
#include <utility>

namespace smb::core {

namespace {

QString replacePattern(QString input, const QRegularExpression &pattern,
                       const QString &replacement) {
  input.replace(pattern, replacement);
  return input;
}

} // namespace

LogSanitizer::LogSanitizer(QStringList secretValues)
    : m_secretValues(std::move(secretValues)) {}

void LogSanitizer::addSecretValue(const QString &secretValue) {
  if (!secretValue.isEmpty() && !m_secretValues.contains(secretValue)) {
    m_secretValues.push_back(secretValue);
  }
}

QString LogSanitizer::sanitize(const QString &message) const {
  auto sanitized = message;

  for (const auto &secret : m_secretValues) {
    if (!secret.isEmpty()) {
      sanitized.replace(secret, QString::fromLatin1(Mask), Qt::CaseSensitive);
    }
  }

  sanitized = replacePattern(
      sanitized,
      QRegularExpression(
          QStringLiteral(
              R"(((?:password|passwd|pwd|token|access_token|refresh_token|secret|master_password)\s*[:=]\s*)("[^"]*"|'[^']*'|[^\s,;&]+))"),
          QRegularExpression::CaseInsensitiveOption),
      QStringLiteral(R"(\1***)"));

  sanitized = replacePattern(
      sanitized,
      QRegularExpression(QStringLiteral(R"(\b(smb://)([^/\s@]+)@)"),
                         QRegularExpression::CaseInsensitiveOption),
      QStringLiteral(R"(\1***@)"));

  sanitized = replacePattern(
      sanitized,
      QRegularExpression(
          QStringLiteral(R"((Authorization\s*[:=]\s*Bearer\s+)[^\s,;&]+)"),
          QRegularExpression::CaseInsensitiveOption),
      QStringLiteral(R"(\1***)"));

  sanitized = replacePattern(
      sanitized,
      QRegularExpression(QStringLiteral(
          R"(\b([A-Za-z0-9_.-]+):([A-Za-z0-9_.@-]+):([^\s,;&]+))")),
      QStringLiteral(R"(\1:\2:***)"));

  return sanitized;
}

} // namespace smb::core
