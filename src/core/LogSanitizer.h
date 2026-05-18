#pragma once

#include <QString>
#include <QStringList>

namespace smb::core {

class LogSanitizer {
public:
  static constexpr auto Mask = "***";

  LogSanitizer() = default;
  explicit LogSanitizer(QStringList secretValues);

  void addSecretValue(const QString &secretValue);
  QString sanitize(const QString &message) const;

private:
  QStringList m_secretValues;
};

} // namespace smb::core
