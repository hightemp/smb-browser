#pragma once

#include <QObject>
#include <QVector>

class QAction;
class QEvent;
class QMainWindow;
class QMenu;
class QSystemTrayIcon;

namespace smb::ui {

struct TrayFavoriteConnection {
  QString id;
  QString name;
};

class TrayController final : public QObject {
  Q_OBJECT

public:
  explicit TrayController(QObject *parent = nullptr);

  void setMainWindow(QMainWindow *window);
  void setCloseToTrayEnabled(bool enabled);
  void setNotificationsEnabled(bool enabled);
  void setQuitOnExitAction(bool enabled);
  void setFavorites(QVector<TrayFavoriteConnection> favorites);

  bool closeToTrayEnabled() const;
  bool notificationsEnabled() const;
  QMenu *menu() const;
  QSystemTrayIcon *trayIcon() const;

  void showTrayIcon();
  void hideTrayIcon();
  void showConnectionError(const QString &title, const QString &message);

signals:
  void mainWindowRequested();
  void favoriteConnectionRequested(const QString &connectionId);
  void exitRequested();

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void rebuildMenu();
  void showMainWindow();
  void exitApplication();

  QMainWindow *m_mainWindow = nullptr;
  QSystemTrayIcon *m_trayIcon = nullptr;
  QMenu *m_menu = nullptr;
  QAction *m_showAction = nullptr;
  QAction *m_exitAction = nullptr;
  QVector<TrayFavoriteConnection> m_favorites;
  bool m_closeToTrayEnabled = true;
  bool m_notificationsEnabled = true;
  bool m_quitOnExitAction = true;
  bool m_exiting = false;
};

} // namespace smb::ui
