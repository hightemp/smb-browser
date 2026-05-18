#include "ui/MainWindow.h"

#include "core/AppInfo.h"

#include <QLabel>
#include <QStatusBar>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(smb::core::applicationName());
  resize(1100, 720);

  auto *placeholder = new QLabel(tr("Select a connection to begin."), this);
  placeholder->setAlignment(Qt::AlignCenter);
  setCentralWidget(placeholder);

  statusBar()->showMessage(tr("Ready"));
}
