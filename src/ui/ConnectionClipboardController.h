#pragma once

#include "ui/ConnectionsPanel.h"

#include <QObject>

class QClipboard;
class QStatusBar;

namespace smb::ui {

class ConnectionClipboardController final : public QObject {
  Q_OBJECT

public:
  ConnectionClipboardController(ConnectionsPanel &panel, QClipboard &clipboard,
                                QStatusBar *statusBar = nullptr,
                                QObject *parent = nullptr);

public slots:
  void copyPath(const QString &normalizedUri);

signals:
  void pathCopied(const QString &normalizedUri);

private:
  QClipboard &m_clipboard;
  QStatusBar *m_statusBar = nullptr;
};

} // namespace smb::ui
