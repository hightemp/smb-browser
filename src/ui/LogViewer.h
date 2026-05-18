#pragma once

#include "core/LogSanitizer.h"

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;

namespace smb::ui {

class LogViewer final : public QDialog {
  Q_OBJECT

public:
  explicit LogViewer(QString logFilePath,
                     smb::core::LogSanitizer sanitizer = {},
                     QWidget *parent = nullptr);

  bool reload();
  void setLogLines(QStringList lines);
  QString displayText() const;
  QString logFilePath() const;

private:
  void applyFilter();
  bool lineMatchesLevel(const QString &line) const;

  QString m_logFilePath;
  smb::core::LogSanitizer m_sanitizer;
  QStringList m_lines;
  QComboBox *m_levelFilter = nullptr;
  QLineEdit *m_searchEdit = nullptr;
  QPlainTextEdit *m_textEdit = nullptr;
};

} // namespace smb::ui
