#pragma once

#include <QMainWindow>
#include <memory>

class QPushButton;

namespace smb::application {
class ImportExportUseCase;
}

namespace smb::ui {
class ImportExportActionPrompter;
class ImportExportController;
}

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  void attachImportExport(smb::application::ImportExportUseCase &useCase,
                          smb::ui::ImportExportActionPrompter &prompter);

private:
  QWidget *createTopBar();
  QWidget *createConnectionsPanel();
  QWidget *createBrowserArea();
  QWidget *createStatusPanel();

  QPushButton *m_importButton = nullptr;
  QPushButton *m_exportButton = nullptr;
  std::unique_ptr<smb::ui::ImportExportController> m_importExportController;
};
