#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("SMB Browser"));
  QApplication::setOrganizationName(QStringLiteral("SMB Browser"));

  MainWindow window;
  window.show();

  return app.exec();
}
