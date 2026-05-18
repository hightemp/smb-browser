#include "ui/TrayController.h"

#include "core/AppInfo.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QMainWindow>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

namespace smb::ui {

TrayController::TrayController(QObject *parent) : QObject(parent) {
  m_menu = new QMenu();
  m_menu->setObjectName(QStringLiteral("trayMenu"));

  m_trayIcon = new QSystemTrayIcon(this);
  m_trayIcon->setObjectName(QStringLiteral("trayIcon"));
  m_trayIcon->setToolTip(smb::core::applicationName());
  m_trayIcon->setContextMenu(m_menu);
  if (qApp != nullptr) {
    m_trayIcon->setIcon(qApp->style()->standardIcon(QStyle::SP_DriveNetIcon));
  }

  connect(m_trayIcon, &QSystemTrayIcon::activated, this,
          [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger ||
                reason == QSystemTrayIcon::DoubleClick) {
              showMainWindow();
            }
          });

  rebuildMenu();
}

void TrayController::setMainWindow(QMainWindow *window) {
  if (m_mainWindow == window) {
    return;
  }
  if (m_mainWindow != nullptr) {
    m_mainWindow->removeEventFilter(this);
  }
  m_mainWindow = window;
  if (m_mainWindow != nullptr) {
    m_mainWindow->installEventFilter(this);
  }
}

void TrayController::setCloseToTrayEnabled(bool enabled) {
  m_closeToTrayEnabled = enabled;
}

void TrayController::setNotificationsEnabled(bool enabled) {
  m_notificationsEnabled = enabled;
}

void TrayController::setQuitOnExitAction(bool enabled) {
  m_quitOnExitAction = enabled;
}

void TrayController::setFavorites(QVector<TrayFavoriteConnection> favorites) {
  m_favorites = std::move(favorites);
  rebuildMenu();
}

bool TrayController::closeToTrayEnabled() const { return m_closeToTrayEnabled; }

bool TrayController::notificationsEnabled() const {
  return m_notificationsEnabled;
}

QMenu *TrayController::menu() const { return m_menu; }

QSystemTrayIcon *TrayController::trayIcon() const { return m_trayIcon; }

void TrayController::showTrayIcon() {
  if (QSystemTrayIcon::isSystemTrayAvailable()) {
    m_trayIcon->show();
  }
}

void TrayController::hideTrayIcon() { m_trayIcon->hide(); }

void TrayController::showConnectionError(const QString &title,
                                         const QString &message) {
  if (!m_notificationsEnabled || !m_trayIcon->supportsMessages()) {
    return;
  }
  m_trayIcon->showMessage(title, message, QSystemTrayIcon::Warning, 8000);
}

bool TrayController::eventFilter(QObject *watched, QEvent *event) {
  if (watched == m_mainWindow && event->type() == QEvent::Close &&
      m_closeToTrayEnabled && !m_exiting) {
    auto *closeEvent = static_cast<QCloseEvent *>(event);
    closeEvent->ignore();
    m_mainWindow->hide();
    return true;
  }

  return QObject::eventFilter(watched, event);
}

void TrayController::rebuildMenu() {
  m_menu->clear();

  m_showAction = m_menu->addAction(tr("Show"));
  m_showAction->setObjectName(QStringLiteral("trayShowAction"));
  connect(m_showAction, &QAction::triggered, this,
          &TrayController::showMainWindow);

  if (!m_favorites.isEmpty()) {
    m_menu->addSeparator();
    for (const auto &favorite : std::as_const(m_favorites)) {
      auto *action = m_menu->addAction(favorite.name);
      action->setObjectName(
          QStringLiteral("trayFavoriteAction_%1").arg(favorite.id));
      action->setData(favorite.id);
      connect(action, &QAction::triggered, this, [this, favorite]() {
        emit favoriteConnectionRequested(favorite.id);
      });
    }
  }

  m_menu->addSeparator();
  m_exitAction = m_menu->addAction(tr("Exit"));
  m_exitAction->setObjectName(QStringLiteral("trayExitAction"));
  connect(m_exitAction, &QAction::triggered, this,
          &TrayController::exitApplication);
}

void TrayController::showMainWindow() {
  if (m_mainWindow != nullptr) {
    m_mainWindow->show();
    m_mainWindow->raise();
    m_mainWindow->activateWindow();
  }
  emit mainWindowRequested();
}

void TrayController::exitApplication() {
  m_exiting = true;
  emit exitRequested();
  if (m_quitOnExitAction && qApp != nullptr) {
    qApp->quit();
  }
}

} // namespace smb::ui
