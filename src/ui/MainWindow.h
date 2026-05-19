#pragma once

#include <QMainWindow>
#include <memory>

class QPushButton;
class QEvent;
class QFrame;

namespace smb::application {
class ImportExportUseCase;
class SettingsUseCase;
}

namespace smb::ui {
class ConnectionsPanel;
class ImportExportActionPrompter;
class ImportExportController;
class LocalizationManager;
class StatusPanel;
class ThemeManager;
}

namespace smb::application {
class TempFileCache;
}

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  void attachImportExport(smb::application::ImportExportUseCase &useCase,
                          smb::ui::ImportExportActionPrompter &prompter);
  void attachSettings(smb::application::SettingsUseCase &settingsUseCase,
                      smb::ui::ThemeManager &themeManager,
                      smb::ui::LocalizationManager &localizationManager,
                      smb::application::TempFileCache &tempFileCache);
  void attachRemoteBrowser(QWidget &browserWidget);
  smb::ui::ConnectionsPanel *connectionsPanel() const;
  smb::ui::StatusPanel *statusPanel() const;

signals:
  void connectionsImported();

private:
  void changeEvent(QEvent *event) override;
  QWidget *createTopBar();
  QWidget *createConnectionsPanel();
  QWidget *createBrowserArea();
  QWidget *createStatusPanel();
  void retranslateUi();

  smb::ui::ConnectionsPanel *m_connectionsPanel = nullptr;
  QFrame *m_browserArea = nullptr;
  smb::ui::StatusPanel *m_statusPanel = nullptr;
  QPushButton *m_importButton = nullptr;
  QPushButton *m_exportButton = nullptr;
  smb::application::SettingsUseCase *m_settingsUseCase = nullptr;
  smb::ui::ThemeManager *m_themeManager = nullptr;
  smb::ui::LocalizationManager *m_localizationManager = nullptr;
  smb::application::TempFileCache *m_tempFileCache = nullptr;
  std::unique_ptr<smb::ui::ImportExportController> m_importExportController;
};
