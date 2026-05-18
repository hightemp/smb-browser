#include "logging/FileLogger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <utility>

namespace smb::infrastructure {

QString logLevelName(LogLevel level) {
  switch (level) {
  case LogLevel::Debug:
    return QStringLiteral("debug");
  case LogLevel::Info:
    return QStringLiteral("info");
  case LogLevel::Warning:
    return QStringLiteral("warning");
  case LogLevel::Error:
    return QStringLiteral("error");
  }

  return QStringLiteral("info");
}

FileLogger::FileLogger(QString logFilePath, smb::core::LogSanitizer sanitizer)
    : m_logFilePath(std::move(logFilePath)), m_sanitizer(std::move(sanitizer)) {
}

QString FileLogger::defaultLogFilePath() {
  auto baseDir =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (baseDir.isEmpty()) {
    baseDir = QDir::homePath() + QStringLiteral("/.smb-browser");
  }

  return QDir(baseDir).filePath(QStringLiteral("logs/smb-browser.log"));
}

bool FileLogger::log(LogRecord record) {
  const QFileInfo fileInfo(m_logFilePath);
  if (!fileInfo.dir().exists() &&
      !QDir().mkpath(fileInfo.dir().absolutePath())) {
    return false;
  }

  QFile file(m_logFilePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    return false;
  }

  QTextStream stream(&file);
  stream << format(std::move(record)) << '\n';
  stream.flush();
  return file.error() == QFileDevice::NoError;
}

QString FileLogger::logFilePath() const { return m_logFilePath; }

QString FileLogger::format(LogRecord record) const {
  if (!record.timestampUtc.isValid()) {
    record.timestampUtc = QDateTime::currentDateTimeUtc();
  }

  const auto timestamp =
      record.timestampUtc.toUTC().toString(Qt::ISODateWithMs);
  const auto category =
      record.category.isEmpty() ? QStringLiteral("app") : record.category;
  const auto correlationId = record.correlationId.isEmpty()
                                 ? QStringLiteral("-")
                                 : record.correlationId;
  const auto message = m_sanitizer.sanitize(record.message);
  const auto details = m_sanitizer.sanitize(record.technicalDetails);

  if (details.isEmpty()) {
    return QStringLiteral("%1 [%2] [%3] [%4] %5")
        .arg(timestamp, logLevelName(record.level), category, correlationId,
             message);
  }

  return QStringLiteral("%1 [%2] [%3] [%4] %5 | %6")
      .arg(timestamp, logLevelName(record.level), category, correlationId,
           message, details);
}

} // namespace smb::infrastructure
