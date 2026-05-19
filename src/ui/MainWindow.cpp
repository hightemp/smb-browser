#include "ui/MainWindow.h"

#include "core/AppInfo.h"
#include "logging/FileLogger.h"
#include "ui/ConnectionsPanel.h"
#include "ui/ImportExportController.h"
#include "ui/LogViewer.h"
#include "ui/SettingsDialog.h"
#include "ui/StatusPanel.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setObjectName(QStringLiteral("mainWindow"));
  setWindowTitle(smb::core::applicationName());
  resize(1200, 760);

  auto *root = new QWidget(this);
  root->setObjectName(QStringLiteral("mainRoot"));

  auto *rootLayout = new QVBoxLayout(root);
  rootLayout->setContentsMargins(8, 8, 8, 8);
  rootLayout->setSpacing(8);
  rootLayout->addWidget(createTopBar());

  auto *splitter = new QSplitter(Qt::Horizontal, root);
  splitter->setObjectName(QStringLiteral("mainSplitter"));
  splitter->addWidget(createConnectionsPanel());
  splitter->addWidget(createBrowserArea());
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({320, 880});
  rootLayout->addWidget(splitter, 1);
  rootLayout->addWidget(createStatusPanel());

  setCentralWidget(root);
  wireConnectionActions();
  retranslateUi();

  statusBar()->showMessage(tr("Ready"));
}

MainWindow::~MainWindow() = default;

QWidget *MainWindow::createTopBar() {
  auto *bar = new QFrame(this);
  bar->setObjectName(QStringLiteral("mainToolbar"));
  bar->setFrameShape(QFrame::StyledPanel);

  auto *layout = new QHBoxLayout(bar);
  layout->setContentsMargins(8, 6, 8, 6);
  layout->setSpacing(6);

  auto *search = new QLineEdit(bar);
  search->setObjectName(QStringLiteral("globalSearchEdit"));
  layout->addWidget(search, 1);

  const auto addButton = [this, bar, layout](const QString &objectName,
                                             const QString &text,
                                             QStyle::StandardPixmap icon) {
    auto *button = new QPushButton(style()->standardIcon(icon), text, bar);
    button->setObjectName(objectName);
    button->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(button);
    return button;
  };

  m_addConnectionButton =
      addButton(QStringLiteral("addConnectionButton"), QString(),
                QStyle::SP_FileDialogNewFolder);
  m_editConnectionButton =
      addButton(QStringLiteral("editConnectionButton"), QString(),
                QStyle::SP_FileIcon);
  m_deleteConnectionButton =
      addButton(QStringLiteral("deleteConnectionButton"), QString(),
                QStyle::SP_TrashIcon);
  m_checkConnectionButton =
      addButton(QStringLiteral("checkConnectionButton"), QString(),
                QStyle::SP_BrowserReload);
  m_connectButton =
      addButton(QStringLiteral("connectButton"), QString(),
                QStyle::SP_DialogOpenButton);
  m_importButton =
      addButton(QStringLiteral("importButton"), QString(), QStyle::SP_ArrowDown);
  m_exportButton =
      addButton(QStringLiteral("exportButton"), QString(), QStyle::SP_ArrowUp);
  auto *logsButton = addButton(QStringLiteral("logsButton"), QString(),
                               QStyle::SP_FileDialogDetailedView);
  connect(logsButton, &QPushButton::clicked, this, [this]() {
    auto *viewer = new smb::ui::LogViewer(
        smb::infrastructure::FileLogger::defaultLogFilePath(), {}, this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->show();
  });
  auto *settingsButton = addButton(QStringLiteral("settingsButton"),
                                   QString(),
                                   QStyle::SP_FileDialogContentsView);
  connect(settingsButton, &QPushButton::clicked, this, [this]() {
    auto *dialog = new smb::ui::SettingsDialog(
        m_settingsUseCase, m_themeManager, m_localizationManager, this,
        m_tempFileCache);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->loadSettings();
    dialog->show();
  });

  return bar;
}

void MainWindow::changeEvent(QEvent *event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }

  QMainWindow::changeEvent(event);
}

QWidget *MainWindow::createConnectionsPanel() {
  m_connectionsPanel = new smb::ui::ConnectionsPanel(this);
  return m_connectionsPanel;
}

void MainWindow::attachImportExport(
    smb::application::ImportExportUseCase &useCase,
    smb::ui::ImportExportActionPrompter &prompter) {
  if (m_importButton == nullptr || m_exportButton == nullptr) {
    return;
  }

  m_importExportController = std::make_unique<smb::ui::ImportExportController>(
      *m_importButton, *m_exportButton, useCase, prompter, this);
}

void MainWindow::attachSettings(
    smb::application::SettingsUseCase &settingsUseCase,
    smb::ui::ThemeManager &themeManager,
    smb::ui::LocalizationManager &localizationManager,
    smb::application::TempFileCache &tempFileCache) {
  m_settingsUseCase = &settingsUseCase;
  m_themeManager = &themeManager;
  m_localizationManager = &localizationManager;
  m_tempFileCache = &tempFileCache;
}

smb::ui::ConnectionsPanel *MainWindow::connectionsPanel() const {
  return m_connectionsPanel;
}

smb::ui::StatusPanel *MainWindow::statusPanel() const { return m_statusPanel; }

void MainWindow::attachRemoteBrowser(QWidget &browserWidget) {
  if (m_browserArea == nullptr || m_browserArea->layout() == nullptr) {
    return;
  }

  auto *layout = qobject_cast<QVBoxLayout *>(m_browserArea->layout());
  if (layout == nullptr) {
    return;
  }

  while (auto *item = layout->takeAt(0)) {
    if (auto *widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }

  browserWidget.setParent(m_browserArea);
  layout->addWidget(&browserWidget, 1);
}

void MainWindow::wireConnectionActions() {
  if (m_connectionsPanel == nullptr) {
    return;
  }

  if (m_addConnectionButton != nullptr) {
    connect(m_addConnectionButton, &QPushButton::clicked, m_connectionsPanel,
            &smb::ui::ConnectionsPanel::addRequested);
  }
  if (m_editConnectionButton != nullptr) {
    connect(m_editConnectionButton, &QPushButton::clicked, this, [this]() {
      const auto id = m_connectionsPanel->selectedConnectionId();
      if (!id.isEmpty()) {
        emit m_connectionsPanel->editRequested(id);
      }
    });
  }
  if (m_deleteConnectionButton != nullptr) {
    connect(m_deleteConnectionButton, &QPushButton::clicked, this, [this]() {
      const auto id = m_connectionsPanel->selectedConnectionId();
      if (!id.isEmpty()) {
        emit m_connectionsPanel->deleteRequested(id);
      }
    });
  }
  if (m_checkConnectionButton != nullptr) {
    connect(m_checkConnectionButton, &QPushButton::clicked, this, [this]() {
      const auto id = m_connectionsPanel->selectedConnectionId();
      if (!id.isEmpty()) {
        emit m_connectionsPanel->checkRequested(id);
      }
    });
  }
  if (m_connectButton != nullptr) {
    connect(m_connectButton, &QPushButton::clicked, this, [this]() {
      const auto id = m_connectionsPanel->selectedConnectionId();
      if (!id.isEmpty()) {
        emit m_connectionsPanel->connectRequested(id);
      }
    });
  }

  connect(m_connectionsPanel,
          &smb::ui::ConnectionsPanel::selectionAvailabilityChanged, this,
          &MainWindow::setTopConnectionActionsEnabled);
  setTopConnectionActionsEnabled(false);
}

void MainWindow::retranslateUi() {
  setWindowTitle(smb::core::applicationName());

  auto setButtonText = [this](const QString &objectName, const QString &text) {
    auto *button = findChild<QPushButton *>(objectName);
    if (button != nullptr) {
      button->setText(text);
    }
  };

  if (auto *search =
          findChild<QLineEdit *>(QStringLiteral("globalSearchEdit"))) {
    search->setPlaceholderText(tr("Search connections or files"));
  }
  setButtonText(QStringLiteral("addConnectionButton"), tr("Add"));
  setButtonText(QStringLiteral("editConnectionButton"), tr("Edit"));
  setButtonText(QStringLiteral("deleteConnectionButton"), tr("Delete"));
  setButtonText(QStringLiteral("checkConnectionButton"), tr("Check"));
  setButtonText(QStringLiteral("connectButton"), tr("Connect"));
  setButtonText(QStringLiteral("importButton"), tr("Import"));
  setButtonText(QStringLiteral("exportButton"), tr("Export"));
  setButtonText(QStringLiteral("logsButton"), tr("Logs"));
  setButtonText(QStringLiteral("settingsButton"), tr("Settings"));

  setButtonText(QStringLiteral("backButton"), tr("Back"));
  setButtonText(QStringLiteral("forwardButton"), tr("Forward"));
  setButtonText(QStringLiteral("upButton"), tr("Up"));
  setButtonText(QStringLiteral("refreshButton"), tr("Refresh"));
  if (auto *fileSearch =
          findChild<QLineEdit *>(QStringLiteral("fileSearchEdit"))) {
    fileSearch->setPlaceholderText(tr("Search current folder"));
  }
  if (auto *placeholder =
          findChild<QLabel *>(QStringLiteral("browserPlaceholder"))) {
    placeholder->setText(tr("Select a connection to browse remote files."));
  }
  if (m_connectionsPanel != nullptr) {
    m_connectionsPanel->retranslateUi();
  }
  statusBar()->showMessage(tr("Ready"));
}

void MainWindow::setTopConnectionActionsEnabled(bool enabled) {
  if (m_editConnectionButton != nullptr) {
    m_editConnectionButton->setEnabled(enabled);
  }
  if (m_deleteConnectionButton != nullptr) {
    m_deleteConnectionButton->setEnabled(enabled);
  }
  if (m_checkConnectionButton != nullptr) {
    m_checkConnectionButton->setEnabled(enabled);
  }
  if (m_connectButton != nullptr) {
    m_connectButton->setEnabled(enabled);
  }
}

QWidget *MainWindow::createBrowserArea() {
  auto *area = new QFrame(this);
  m_browserArea = area;
  area->setObjectName(QStringLiteral("browserArea"));
  area->setFrameShape(QFrame::StyledPanel);

  auto *layout = new QVBoxLayout(area);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(6);

  auto *toolbar = new QWidget(area);
  toolbar->setObjectName(QStringLiteral("browserToolbar"));
  auto *toolbarLayout = new QHBoxLayout(toolbar);
  toolbarLayout->setContentsMargins(0, 0, 0, 0);
  toolbarLayout->setSpacing(6);

  const auto addNavButton = [this, toolbar, toolbarLayout](
                                const QString &objectName, const QString &text,
                                QStyle::StandardPixmap icon) {
    auto *button = new QPushButton(style()->standardIcon(icon), text, toolbar);
    button->setObjectName(objectName);
    toolbarLayout->addWidget(button);
    return button;
  };

  addNavButton(QStringLiteral("backButton"), QString(), QStyle::SP_ArrowBack);
  addNavButton(QStringLiteral("forwardButton"), QString(),
               QStyle::SP_ArrowForward);
  addNavButton(QStringLiteral("upButton"), QString(), QStyle::SP_ArrowUp);
  addNavButton(QStringLiteral("refreshButton"), QString(),
               QStyle::SP_BrowserReload);

  auto *fileSearch = new QLineEdit(toolbar);
  fileSearch->setObjectName(QStringLiteral("fileSearchEdit"));
  toolbarLayout->addWidget(fileSearch, 1);
  layout->addWidget(toolbar);

  auto *table = new QTableView(area);
  table->setObjectName(QStringLiteral("remoteFilesView"));
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table->setAlternatingRowColors(true);
  layout->addWidget(table, 1);

  auto *placeholder = new QLabel(area);
  placeholder->setObjectName(QStringLiteral("browserPlaceholder"));
  placeholder->setAlignment(Qt::AlignCenter);
  layout->addWidget(placeholder);

  return area;
}

QWidget *MainWindow::createStatusPanel() {
  m_statusPanel = new smb::ui::StatusPanel(this);
  return m_statusPanel;
}
