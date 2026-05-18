#include "ui/RemoteBrowserWidget.h"

#include <QAbstractItemView>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QStyle>
#include <QTableView>
#include <QVBoxLayout>
#include <utility>

namespace smb::ui {

RemoteBrowserWidget::RemoteBrowserWidget(
    smb::application::RemoteDirectoryUseCase &directoryUseCase,
    smb::application::RemoteFileOperationUseCase &fileOperationUseCase,
    smb::application::RemoteFileTransferUseCase &fileTransferUseCase,
    smb::application::OperationQueue &operationQueue,
    RemoteFileActionPrompter &prompter, QWidget *parent)
    : QWidget(parent), m_directoryUseCase(directoryUseCase),
      m_fileOperationUseCase(fileOperationUseCase),
      m_fileTransferUseCase(fileTransferUseCase),
      m_operationQueue(operationQueue), m_prompter(prompter) {
  qRegisterMetaType<smb::core::AppError>("smb::core::AppError");
  qRegisterMetaType<smb::core::RemoteFileEntry>("smb::core::RemoteFileEntry");

  setObjectName(QStringLiteral("remoteBrowserWidget"));

  m_model = new RemoteFileModel(this);
  m_filterModel = new RemoteFileFilterProxyModel(this);
  m_filterModel->setSourceModel(m_model);

  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(6);

  auto *toolbar = new QWidget(this);
  toolbar->setObjectName(QStringLiteral("remoteBrowserToolbar"));
  auto *toolbarLayout = new QHBoxLayout(toolbar);
  toolbarLayout->setContentsMargins(0, 0, 0, 0);
  toolbarLayout->setSpacing(6);

  m_backButton = new QPushButton(style()->standardIcon(QStyle::SP_ArrowBack),
                                 tr("Back"), toolbar);
  m_backButton->setObjectName(QStringLiteral("remoteBrowserBackButton"));
  m_forwardButton = new QPushButton(
      style()->standardIcon(QStyle::SP_ArrowForward), tr("Forward"), toolbar);
  m_forwardButton->setObjectName(QStringLiteral("remoteBrowserForwardButton"));
  m_upButton = new QPushButton(style()->standardIcon(QStyle::SP_ArrowUp),
                               tr("Up"), toolbar);
  m_upButton->setObjectName(QStringLiteral("remoteBrowserUpButton"));
  m_refreshButton = new QPushButton(
      style()->standardIcon(QStyle::SP_BrowserReload), tr("Refresh"), toolbar);
  m_refreshButton->setObjectName(QStringLiteral("remoteBrowserRefreshButton"));
  m_createFolderButton =
      new QPushButton(style()->standardIcon(QStyle::SP_FileDialogNewFolder),
                      tr("New Folder"), toolbar);
  m_createFolderButton->setObjectName(
      QStringLiteral("remoteBrowserCreateFolderButton"));
  m_uploadButton = new QPushButton(style()->standardIcon(QStyle::SP_ArrowUp),
                                   tr("Upload"), toolbar);
  m_uploadButton->setObjectName(QStringLiteral("remoteBrowserUploadButton"));
  m_downloadButton = new QPushButton(
      style()->standardIcon(QStyle::SP_ArrowDown), tr("Download"), toolbar);
  m_downloadButton->setObjectName(
      QStringLiteral("remoteBrowserDownloadButton"));
  m_deleteButton = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon),
                                   tr("Delete"), toolbar);
  m_deleteButton->setObjectName(QStringLiteral("remoteBrowserDeleteButton"));
  m_renameButton = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon),
                                   tr("Rename"), toolbar);
  m_renameButton->setObjectName(QStringLiteral("remoteBrowserRenameButton"));

  toolbarLayout->addWidget(m_backButton);
  toolbarLayout->addWidget(m_forwardButton);
  toolbarLayout->addWidget(m_upButton);
  toolbarLayout->addWidget(m_refreshButton);
  toolbarLayout->addWidget(m_createFolderButton);
  toolbarLayout->addWidget(m_uploadButton);
  toolbarLayout->addWidget(m_downloadButton);
  toolbarLayout->addWidget(m_renameButton);
  toolbarLayout->addWidget(m_deleteButton);

  m_searchEdit = new QLineEdit(toolbar);
  m_searchEdit->setObjectName(QStringLiteral("remoteFileSearchEdit"));
  m_searchEdit->setPlaceholderText(tr("Search current folder"));
  toolbarLayout->addWidget(m_searchEdit, 1);
  rootLayout->addWidget(toolbar);

  m_stateLabel =
      new QLabel(tr("Select a connection to browse remote files."), this);
  m_stateLabel->setObjectName(QStringLiteral("remoteBrowserStateLabel"));
  m_stateLabel->setAlignment(Qt::AlignCenter);
  rootLayout->addWidget(m_stateLabel);

  m_tableView = new QTableView(this);
  m_tableView->setObjectName(QStringLiteral("remoteFilesView"));
  m_tableView->setModel(m_filterModel);
  m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_tableView->setAlternatingRowColors(true);
  m_tableView->horizontalHeader()->setStretchLastSection(true);
  m_tableView->verticalHeader()->setVisible(false);
  rootLayout->addWidget(m_tableView, 1);

  connect(m_backButton, &QPushButton::clicked, this,
          &RemoteBrowserWidget::goBack);
  connect(m_forwardButton, &QPushButton::clicked, this,
          &RemoteBrowserWidget::goForward);
  connect(m_upButton, &QPushButton::clicked, this, &RemoteBrowserWidget::goUp);
  connect(m_refreshButton, &QPushButton::clicked, this,
          &RemoteBrowserWidget::refresh);
  connect(m_createFolderButton, &QPushButton::clicked, this,
          &RemoteBrowserWidget::createFolder);
  connect(m_uploadButton, &QPushButton::clicked, this,
          &RemoteBrowserWidget::uploadFile);
  connect(m_downloadButton, &QPushButton::clicked, this,
          &RemoteBrowserWidget::downloadSelected);
  connect(m_deleteButton, &QPushButton::clicked, this,
          &RemoteBrowserWidget::deleteSelected);
  connect(m_renameButton, &QPushButton::clicked, this,
          &RemoteBrowserWidget::renameSelected);
  connect(m_searchEdit, &QLineEdit::textChanged, this,
          [this](const QString &text) {
            m_filterModel->setFilterText(text);
            showDirectoryState();
          });
  connect(m_tableView, &QTableView::doubleClicked, this,
          [this](const QModelIndex &index) {
            const auto sourceIndex = m_filterModel->mapToSource(index);
            const auto entry = m_model->entryAt(sourceIndex.row());
            if (entry.name.isEmpty()) {
              return;
            }
            if (entry.isDirectory()) {
              openDirectory(entry.remotePath);
              return;
            }
            emit fileActivated(entry);
          });
  connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, [this]() { updateActionState(); });

  updateActionState();
}

void RemoteBrowserWidget::setDirectory(
    smb::application::OpenConnectionResult result) {
  m_connectionId = result.connection.id;
  m_currentRemotePath = normalizeRemotePath(result.currentRemotePath);
  m_backStack.clear();
  m_forwardStack.clear();
  m_searchEdit->clear();
  m_model->setEntries(std::move(result.entries), m_currentRemotePath);
  showDirectoryState();
  updateActionState();
}

void RemoteBrowserWidget::clear() {
  m_connectionId.clear();
  m_currentRemotePath.clear();
  m_backStack.clear();
  m_forwardStack.clear();
  m_searchEdit->clear();
  m_model->clear();
  m_stateLabel->setText(tr("Select a connection to browse remote files."));
  m_stateLabel->setVisible(true);
  updateActionState();
}

QString RemoteBrowserWidget::currentConnectionId() const {
  return m_connectionId;
}

QString RemoteBrowserWidget::currentRemotePath() const {
  return m_currentRemotePath;
}

RemoteFileModel *RemoteBrowserWidget::model() const { return m_model; }

void RemoteBrowserWidget::openDirectory(const QString &remotePath) {
  requestDirectory(remotePath, HistoryMode::Push);
}

void RemoteBrowserWidget::goBack() {
  if (m_backStack.isEmpty()) {
    return;
  }
  requestDirectory(m_backStack.last(), HistoryMode::Back);
}

void RemoteBrowserWidget::goForward() {
  if (m_forwardStack.isEmpty()) {
    return;
  }
  requestDirectory(m_forwardStack.last(), HistoryMode::Forward);
}

void RemoteBrowserWidget::goUp() {
  const auto parent = parentRemotePath(m_currentRemotePath);
  if (m_connectionId.isEmpty() || parent == m_currentRemotePath) {
    return;
  }
  requestDirectory(parent, HistoryMode::Push);
}

void RemoteBrowserWidget::refresh() {
  if (m_connectionId.isEmpty() || m_currentRemotePath.isEmpty()) {
    return;
  }
  requestDirectory(m_currentRemotePath, HistoryMode::Replace);
}

void RemoteBrowserWidget::createFolder() {
  if (m_connectionId.isEmpty()) {
    return;
  }

  const auto name =
      m_prompter.promptCreateFolderName(this, m_currentRemotePath);
  if (!name.has_value()) {
    return;
  }
  const auto trimmedName = name.value().trimmed();
  if (!isValidEntryName(trimmedName)) {
    const auto error = invalidNameError();
    m_prompter.showError(this, tr("Unable to Create Folder"), error);
    emit remoteOperationFailed(QStringLiteral("create_directory"), error);
    return;
  }

  const auto connectionId = m_connectionId;
  const auto targetPath = joinRemotePath(m_currentRemotePath, trimmedName);
  runFileOperation(QStringLiteral("create_directory"),
                   [this, connectionId,
                    targetPath](const smb::core::OperationContext &context) {
                     return m_fileOperationUseCase.createDirectory(
                         connectionId, targetPath, context);
                   });
}

void RemoteBrowserWidget::deleteSelected() {
  const auto entry = selectedEntry();
  if (m_connectionId.isEmpty() || entry.name.isEmpty()) {
    return;
  }
  if (!m_prompter.confirmDelete(this, entry)) {
    return;
  }

  const auto connectionId = m_connectionId;
  runFileOperation(QStringLiteral("delete"),
                   [this, connectionId, remotePath = entry.remotePath](
                       const smb::core::OperationContext &context) {
                     return m_fileOperationUseCase.remove(connectionId,
                                                          remotePath, context);
                   });
}

void RemoteBrowserWidget::renameSelected() {
  const auto entry = selectedEntry();
  if (m_connectionId.isEmpty() || entry.name.isEmpty()) {
    return;
  }

  const auto name = m_prompter.promptRename(this, entry);
  if (!name.has_value()) {
    return;
  }
  const auto trimmedName = name.value().trimmed();
  if (!isValidEntryName(trimmedName)) {
    const auto error = invalidNameError();
    m_prompter.showError(this, tr("Unable to Rename Item"), error);
    emit remoteOperationFailed(QStringLiteral("rename"), error);
    return;
  }

  const auto connectionId = m_connectionId;
  const auto targetPath =
      joinRemotePath(parentRemotePath(entry.remotePath), trimmedName);
  runFileOperation(QStringLiteral("rename"),
                   [this, connectionId, sourcePath = entry.remotePath,
                    targetPath](const smb::core::OperationContext &context) {
                     return m_fileOperationUseCase.rename(
                         connectionId, sourcePath, targetPath, context);
                   });
}

void RemoteBrowserWidget::downloadSelected() {
  const auto entry = selectedEntry();
  if (m_connectionId.isEmpty() || !entry.isFile()) {
    return;
  }

  const auto localPath = m_prompter.promptDownloadPath(this, entry);
  if (!localPath.has_value()) {
    return;
  }

  const auto connectionId = m_connectionId;
  runFileOperation(
      QStringLiteral("download"),
      [this, connectionId, remotePath = entry.remotePath,
       localPath =
           localPath.value()](const smb::core::OperationContext &context) {
        return m_fileTransferUseCase.downloadFile(connectionId, remotePath,
                                                  localPath, context);
      },
      false);
}

void RemoteBrowserWidget::uploadFile() {
  if (m_connectionId.isEmpty()) {
    return;
  }

  const auto localPath = m_prompter.promptUploadPath(this, m_currentRemotePath);
  if (!localPath.has_value()) {
    return;
  }

  const auto fileName = QFileInfo(localPath.value()).fileName();
  if (!isValidEntryName(fileName)) {
    const auto error = invalidNameError();
    m_prompter.showError(this, tr("Unable to Upload File"), error);
    emit remoteOperationFailed(QStringLiteral("upload"), error);
    return;
  }

  const auto connectionId = m_connectionId;
  const auto targetPath = joinRemotePath(m_currentRemotePath, fileName);
  runFileOperation(QStringLiteral("upload"),
                   [this, connectionId, localPath = localPath.value(),
                    targetPath](const smb::core::OperationContext &context) {
                     return m_fileTransferUseCase.uploadFile(
                         connectionId, localPath, targetPath, context);
                   });
}

void RemoteBrowserWidget::requestDirectory(const QString &remotePath,
                                           HistoryMode historyMode) {
  if (m_connectionId.isEmpty()) {
    return;
  }

  const auto connectionId = m_connectionId;
  const auto previousPath = m_currentRemotePath;
  const auto targetPath = normalizeRemotePath(remotePath);
  showLoading(targetPath);

  QPointer<RemoteBrowserWidget> self(this);
  auto &directoryUseCase = m_directoryUseCase;
  const auto operationId = m_operationQueue.enqueue(
      tr("Open remote folder"),
      [self, &directoryUseCase, connectionId, targetPath, previousPath,
       historyMode](const smb::core::OperationContext &context) {
        auto listed =
            directoryUseCase.listDirectory(connectionId, targetPath, context);
        if (!listed.ok()) {
          if (self) {
            self->deliverFailure(connectionId, targetPath, listed.error());
          }
          return smb::core::Result<bool>::failure(listed.error());
        }

        if (self) {
          auto result = std::move(listed.value());
          QMetaObject::invokeMethod(
              self.data(),
              [self, result = std::move(result), historyMode,
               previousPath]() mutable {
                if (self) {
                  self->applyDirectory(std::move(result), historyMode,
                                       previousPath);
                }
              },
              Qt::QueuedConnection);
        }
        return smb::core::Result<bool>::success(true);
      });

  emit directoryLoadStarted(connectionId, targetPath, operationId);
  updateActionState();
}

void RemoteBrowserWidget::applyDirectory(
    smb::application::OpenConnectionResult result, HistoryMode historyMode,
    const QString &previousPath) {
  const auto targetPath = normalizeRemotePath(result.currentRemotePath);
  const auto previous =
      previousPath.isEmpty() ? QString() : normalizeRemotePath(previousPath);

  switch (historyMode) {
  case HistoryMode::Push:
    if (!previous.isEmpty() && previous != targetPath) {
      m_backStack.push_back(previous);
    }
    m_forwardStack.clear();
    break;
  case HistoryMode::Back:
    if (!m_backStack.isEmpty() && m_backStack.last() == targetPath) {
      m_backStack.removeLast();
    }
    if (!previous.isEmpty() && previous != targetPath) {
      m_forwardStack.push_back(previous);
    }
    break;
  case HistoryMode::Forward:
    if (!m_forwardStack.isEmpty() && m_forwardStack.last() == targetPath) {
      m_forwardStack.removeLast();
    }
    if (!previous.isEmpty() && previous != targetPath) {
      m_backStack.push_back(previous);
    }
    break;
  case HistoryMode::Replace:
    break;
  }

  m_connectionId = result.connection.id;
  m_currentRemotePath = targetPath;
  m_searchEdit->clear();
  m_model->setEntries(std::move(result.entries), m_currentRemotePath);
  showDirectoryState();
  updateActionState();
  emit directoryOpened(m_connectionId, m_currentRemotePath);
}

void RemoteBrowserWidget::deliverFailure(const QString &connectionId,
                                         const QString &remotePath,
                                         const smb::core::AppError &error) {
  QPointer<RemoteBrowserWidget> self(this);
  if (!self) {
    return;
  }

  QMetaObject::invokeMethod(
      this,
      [self, connectionId, remotePath, error]() {
        if (!self) {
          return;
        }
        self->showError(error);
        self->updateActionState();
        emit self->directoryOpenFailed(connectionId, remotePath, error);
      },
      Qt::QueuedConnection);
}

void RemoteBrowserWidget::runFileOperation(
    const QString &operationName,
    std::function<smb::core::Result<bool>(const smb::core::OperationContext &)>
        operation,
    bool refreshAfterSuccess) {
  QPointer<RemoteBrowserWidget> self(this);
  const auto operationId = m_operationQueue.enqueue(
      operationName, [self, operationName, operation = std::move(operation),
                      refreshAfterSuccess](
                         const smb::core::OperationContext &context) mutable {
        auto result = operation(context);
        if (!result.ok()) {
          if (self) {
            const auto error = result.error();
            QMetaObject::invokeMethod(
                self.data(),
                [self, operationName, error]() {
                  if (!self) {
                    return;
                  }
                  self->showError(error);
                  self->m_prompter.showError(
                      self.data(), self->tr("Remote Operation Failed"), error);
                  self->updateActionState();
                  emit self->remoteOperationFailed(operationName, error);
                },
                Qt::QueuedConnection);
          }
          return smb::core::Result<bool>::failure(result.error());
        }

        if (self) {
          QMetaObject::invokeMethod(
              self.data(),
              [self, operationName, refreshAfterSuccess]() {
                if (!self) {
                  return;
                }
                emit self->remoteOperationCompleted(operationName);
                if (refreshAfterSuccess) {
                  self->refresh();
                }
              },
              Qt::QueuedConnection);
        }
        return smb::core::Result<bool>::success(true);
      });

  emit remoteOperationStarted(operationName, operationId);
}

void RemoteBrowserWidget::showLoading(const QString &remotePath) {
  m_stateLabel->setText(tr("Loading %1").arg(remotePath));
  m_stateLabel->setVisible(true);
}

void RemoteBrowserWidget::showDirectoryState() {
  if (m_connectionId.isEmpty()) {
    m_stateLabel->setText(tr("Select a connection to browse remote files."));
    m_stateLabel->setVisible(true);
    return;
  }

  if (m_model->rowCount() == 0) {
    m_stateLabel->setText(tr("No files in this folder."));
    m_stateLabel->setVisible(true);
    return;
  }

  if (m_filterModel->rowCount() == 0) {
    m_stateLabel->setText(tr("No matching files."));
    m_stateLabel->setVisible(true);
    return;
  }

  m_stateLabel->clear();
  m_stateLabel->setVisible(false);
}

void RemoteBrowserWidget::showError(const smb::core::AppError &error) {
  m_stateLabel->setText(error.userMessage.isEmpty()
                            ? tr("Unable to open remote folder.")
                            : error.userMessage);
  m_stateLabel->setVisible(true);
}

void RemoteBrowserWidget::updateActionState() {
  const auto hasConnection = !m_connectionId.isEmpty();
  const auto hasSelection = !selectedEntry().name.isEmpty();
  m_backButton->setEnabled(hasConnection && !m_backStack.isEmpty());
  m_forwardButton->setEnabled(hasConnection && !m_forwardStack.isEmpty());
  m_upButton->setEnabled(hasConnection && !m_currentRemotePath.isEmpty() &&
                         m_currentRemotePath != QStringLiteral("/"));
  m_refreshButton->setEnabled(hasConnection && !m_currentRemotePath.isEmpty());
  m_createFolderButton->setEnabled(hasConnection &&
                                   !m_currentRemotePath.isEmpty());
  m_uploadButton->setEnabled(hasConnection && !m_currentRemotePath.isEmpty());
  m_downloadButton->setEnabled(hasConnection && selectedEntry().isFile());
  m_deleteButton->setEnabled(hasConnection && hasSelection);
  m_renameButton->setEnabled(hasConnection && hasSelection);
}

smb::core::RemoteFileEntry RemoteBrowserWidget::selectedEntry() const {
  const auto selected = m_tableView->selectionModel()->selectedRows();
  if (selected.isEmpty()) {
    return {};
  }

  const auto sourceIndex = m_filterModel->mapToSource(selected.first());
  return m_model->entryAt(sourceIndex.row());
}

QString RemoteBrowserWidget::normalizeRemotePath(QString remotePath) {
  remotePath =
      remotePath.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (remotePath.isEmpty()) {
    return QStringLiteral("/");
  }
  if (!remotePath.startsWith(QLatin1Char('/'))) {
    remotePath.prepend(QLatin1Char('/'));
  }
  while (remotePath.size() > 1 && remotePath.endsWith(QLatin1Char('/'))) {
    remotePath.chop(1);
  }
  return remotePath;
}

QString RemoteBrowserWidget::parentRemotePath(const QString &remotePath) {
  const auto path = normalizeRemotePath(remotePath);
  if (path == QStringLiteral("/")) {
    return path;
  }

  const auto index = path.lastIndexOf(QLatin1Char('/'));
  if (index <= 0) {
    return QStringLiteral("/");
  }
  return path.left(index);
}

QString RemoteBrowserWidget::joinRemotePath(const QString &parentPath,
                                            const QString &childName) {
  const auto parent = normalizeRemotePath(parentPath);
  const auto child = childName.trimmed();
  if (parent == QStringLiteral("/")) {
    return QStringLiteral("/") + child;
  }
  return parent + QStringLiteral("/") + child;
}

bool RemoteBrowserWidget::isValidEntryName(const QString &name) {
  return !name.trimmed().isEmpty() && !name.contains(QLatin1Char('/')) &&
         !name.contains(QLatin1Char('\\'));
}

smb::core::AppError RemoteBrowserWidget::invalidNameError() {
  return smb::core::AppError::fromCode(
      smb::core::ErrorCode::InvalidPath, smb::core::ErrorCategory::Validation,
      tr("Remote item name must not be empty or contain path separators."));
}

} // namespace smb::ui
