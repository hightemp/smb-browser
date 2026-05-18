#include "ui/MainWindow.h"
#include "ui/LocalizationManager.h"
#include "ui/ThemeManager.h"
#include "ui/TrayController.h"

#include <QApplication>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("SMB Browser"));
  QApplication::setOrganizationName(QStringLiteral("SMB Browser"));

  smb::ui::LocalizationManager localizationManager;
  localizationManager.apply(app);

  smb::ui::ThemeManager themeManager;
  themeManager.apply(app);

  MainWindow window;
  smb::ui::TrayController trayController;
  trayController.setMainWindow(&window);
  trayController.showTrayIcon();
  window.show();

  return app.exec();
}
