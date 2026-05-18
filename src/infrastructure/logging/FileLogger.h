#pragma once

#include "core/LogSanitizer.h"

#include <QDateTime>
#include <QString>

namespace smb::infrastructure {

enum class LogLevel {
  Debug,
  Info,
  Warning,
  Error,
};

struct LogRecord {
  QDateTime timestampUtc;
  LogLevel level = LogLevel::Info;
  QString category;
  QString correlationId;
  QString message;
  QString technicalDetails;
};

class FileLogger {
public:
  FileLogger(QString logFilePath, smb::core::LogSanitizer sanitizer = {});

  static QString defaultLogFilePath();

  bool log(LogRecord record);
  QString logFilePath() const;

private:
  QString format(LogRecord record) const;

  QString m_logFilePath;
  smb::core::LogSanitizer m_sanitizer;
};

QString logLevelName(LogLevel level);

} // namespace smb::infrastructure
