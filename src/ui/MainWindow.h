#pragma once

#include <QMainWindow>

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);

private:
  QWidget *createTopBar();
  QWidget *createConnectionsPanel();
  QWidget *createBrowserArea();
  QWidget *createStatusPanel();
};
