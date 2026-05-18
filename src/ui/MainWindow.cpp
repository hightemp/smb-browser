#include "ui/MainWindow.h"

#include "core/AppInfo.h"
#include "logging/FileLogger.h"
#include "ui/ConnectionsPanel.h"
#include "ui/ImportExportController.h"
#include "ui/LogViewer.h"
#include "ui/SettingsDialog.h"
#include "ui/StatusPanel.h"

#include <QAbstractItemView>
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
  search->setPlaceholderText(tr("Search connections or files"));
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

  addButton(QStringLiteral("addConnectionButton"), tr("Add"),
            QStyle::SP_FileDialogNewFolder);
  addButton(QStringLiteral("editConnectionButton"), tr("Edit"),
            QStyle::SP_FileIcon);
  addButton(QStringLiteral("deleteConnectionButton"), tr("Delete"),
            QStyle::SP_TrashIcon);
  addButton(QStringLiteral("checkConnectionButton"), tr("Check"),
            QStyle::SP_BrowserReload);
  addButton(QStringLiteral("connectButton"), tr("Connect"),
            QStyle::SP_DialogOpenButton);
  m_importButton =
      addButton(QStringLiteral("importButton"), tr("Import"), QStyle::SP_ArrowDown);
  m_exportButton =
      addButton(QStringLiteral("exportButton"), tr("Export"), QStyle::SP_ArrowUp);
  auto *logsButton = addButton(QStringLiteral("logsButton"), tr("Logs"),
                               QStyle::SP_FileDialogDetailedView);
  connect(logsButton, &QPushButton::clicked, this, [this]() {
    auto *viewer = new smb::ui::LogViewer(
        smb::infrastructure::FileLogger::defaultLogFilePath(), {}, this);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->show();
  });
  auto *settingsButton = addButton(QStringLiteral("settingsButton"),
                                   tr("Settings"),
                                   QStyle::SP_FileDialogContentsView);
  connect(settingsButton, &QPushButton::clicked, this, [this]() {
    auto *dialog = new smb::ui::SettingsDialog(nullptr, nullptr, nullptr, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->loadSettings();
    dialog->show();
  });

  return bar;
}

QWidget *MainWindow::createConnectionsPanel() {
  return new smb::ui::ConnectionsPanel(this);
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

QWidget *MainWindow::createBrowserArea() {
  auto *area = new QFrame(this);
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

  addNavButton(QStringLiteral("backButton"), tr("Back"), QStyle::SP_ArrowBack);
  addNavButton(QStringLiteral("forwardButton"), tr("Forward"),
               QStyle::SP_ArrowForward);
  addNavButton(QStringLiteral("upButton"), tr("Up"), QStyle::SP_ArrowUp);
  addNavButton(QStringLiteral("refreshButton"), tr("Refresh"),
               QStyle::SP_BrowserReload);

  auto *fileSearch = new QLineEdit(toolbar);
  fileSearch->setObjectName(QStringLiteral("fileSearchEdit"));
  fileSearch->setPlaceholderText(tr("Search current folder"));
  toolbarLayout->addWidget(fileSearch, 1);
  layout->addWidget(toolbar);

  auto *table = new QTableView(area);
  table->setObjectName(QStringLiteral("remoteFilesView"));
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table->setAlternatingRowColors(true);
  layout->addWidget(table, 1);

  auto *placeholder =
      new QLabel(tr("Select a connection to browse remote files."), area);
  placeholder->setObjectName(QStringLiteral("browserPlaceholder"));
  placeholder->setAlignment(Qt::AlignCenter);
  layout->addWidget(placeholder);

  return area;
}

QWidget *MainWindow::createStatusPanel() {
  return new smb::ui::StatusPanel(this);
}
