#include "ui/LogViewer.h"

#include <QComboBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>
#include <QVBoxLayout>
#include <utility>

namespace smb::ui {

LogViewer::LogViewer(QString logFilePath, smb::core::LogSanitizer sanitizer,
                     QWidget *parent)
    : QDialog(parent), m_logFilePath(std::move(logFilePath)),
      m_sanitizer(std::move(sanitizer)) {
  setObjectName(QStringLiteral("logViewer"));
  setWindowTitle(tr("Application Log"));
  resize(820, 520);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);

  auto *toolbar = new QWidget(this);
  toolbar->setObjectName(QStringLiteral("logViewerToolbar"));
  auto *toolbarLayout = new QHBoxLayout(toolbar);
  toolbarLayout->setContentsMargins(0, 0, 0, 0);
  toolbarLayout->setSpacing(6);

  toolbarLayout->addWidget(new QLabel(tr("Level"), toolbar));
  m_levelFilter = new QComboBox(toolbar);
  m_levelFilter->setObjectName(QStringLiteral("logLevelFilter"));
  m_levelFilter->addItem(tr("All"), QString());
  m_levelFilter->addItem(tr("Debug"), QStringLiteral("[debug]"));
  m_levelFilter->addItem(tr("Info"), QStringLiteral("[info]"));
  m_levelFilter->addItem(tr("Warning"), QStringLiteral("[warning]"));
  m_levelFilter->addItem(tr("Error"), QStringLiteral("[error]"));
  toolbarLayout->addWidget(m_levelFilter);

  m_searchEdit = new QLineEdit(toolbar);
  m_searchEdit->setObjectName(QStringLiteral("logSearchEdit"));
  m_searchEdit->setPlaceholderText(tr("Search log"));
  toolbarLayout->addWidget(m_searchEdit, 1);

  auto *reloadButton = new QPushButton(tr("Reload"), toolbar);
  reloadButton->setObjectName(QStringLiteral("reloadLogButton"));
  toolbarLayout->addWidget(reloadButton);
  layout->addWidget(toolbar);

  m_textEdit = new QPlainTextEdit(this);
  m_textEdit->setObjectName(QStringLiteral("logText"));
  m_textEdit->setReadOnly(true);
  m_textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
  layout->addWidget(m_textEdit, 1);

  connect(m_levelFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this]() { applyFilter(); });
  connect(m_searchEdit, &QLineEdit::textChanged, this,
          [this]() { applyFilter(); });
  connect(reloadButton, &QPushButton::clicked, this, [this]() { reload(); });

  reload();
}

bool LogViewer::reload() {
  if (m_logFilePath.isEmpty()) {
    setLogLines({});
    return false;
  }

  QFile file(m_logFilePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    setLogLines({});
    return false;
  }

  QStringList lines;
  QTextStream stream(&file);
  while (!stream.atEnd()) {
    lines.push_back(stream.readLine());
  }
  setLogLines(std::move(lines));
  return true;
}

void LogViewer::setLogLines(QStringList lines) {
  m_lines.clear();
  m_lines.reserve(lines.size());
  for (const auto &line : std::as_const(lines)) {
    m_lines.push_back(m_sanitizer.sanitize(line));
  }
  applyFilter();
}

QString LogViewer::displayText() const { return m_textEdit->toPlainText(); }

QString LogViewer::logFilePath() const { return m_logFilePath; }

void LogViewer::applyFilter() {
  const auto query = m_searchEdit->text().trimmed();
  QStringList visibleLines;
  for (const auto &line : std::as_const(m_lines)) {
    if (!lineMatchesLevel(line)) {
      continue;
    }
    if (!query.isEmpty() && !line.contains(query, Qt::CaseInsensitive)) {
      continue;
    }
    visibleLines.push_back(line);
  }
  m_textEdit->setPlainText(visibleLines.join(QLatin1Char('\n')));
}

bool LogViewer::lineMatchesLevel(const QString &line) const {
  const auto marker = m_levelFilter->currentData().toString();
  return marker.isEmpty() || line.contains(marker, Qt::CaseInsensitive);
}

} // namespace smb::ui
