#include "ui/ConnectionClipboardController.h"

#include <QClipboard>
#include <QStatusBar>

namespace smb::ui {

ConnectionClipboardController::ConnectionClipboardController(
    ConnectionsPanel &panel, QClipboard &clipboard, QStatusBar *statusBar,
    QObject *parent)
    : QObject(parent), m_clipboard(clipboard), m_statusBar(statusBar) {
  connect(&panel, &ConnectionsPanel::copyPathRequested, this,
          &ConnectionClipboardController::copyPath);
}

void ConnectionClipboardController::copyPath(const QString &normalizedUri) {
  if (normalizedUri.isEmpty()) {
    return;
  }

  m_clipboard.setText(normalizedUri);
  if (m_statusBar != nullptr) {
    m_statusBar->showMessage(tr("Copied path: %1").arg(normalizedUri), 3000);
  }
  emit pathCopied(normalizedUri);
}

} // namespace smb::ui
