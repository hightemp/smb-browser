#include "ui/RemoteBrowserWidget.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSize>
#include <QSizePolicy>
#include <QStyle>
#include <QTableView>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <utility>

namespace smb::ui {

namespace {

bool isBrowsableRemoteEntry(const smb::core::RemoteFileEntry &entry) {
  return entry.type == smb::core::RemoteFileType::Directory ||
         entry.type == smb::core::RemoteFileType::Symlink;
}

QString locationRootText(const smb::core::Connection &connection) {
  if (!connection.normalizedUri.trimmed().isEmpty()) {
    return connection.normalizedUri.trimmed();
  }
  if (!connection.server.trimmed().isEmpty() &&
      !connection.share.trimmed().isEmpty()) {
    return QStringLiteral("smb://%1/%2")
        .arg(connection.server.trimmed(), connection.share.trimmed());
  }
  if (!connection.name.trimmed().isEmpty()) {
    return connection.name.trimmed();
  }
  return connection.id.trimmed();
}

QString displayLocation(QString rootText, const QString &remotePath) {
  rootText = rootText.trimmed();
  while (rootText.endsWith(QLatin1Char('/'))) {
    rootText.chop(1);
  }
  auto path = remotePath.trimmed();
  if (path.isEmpty()) {
    path = QStringLiteral("/");
  }
  if (!path.startsWith(QLatin1Char('/'))) {
    path.prepend(QLatin1Char('/'));
  }
  return rootText + path;
}

} // namespace

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
  setAcceptDrops(true);

  m_model = new RemoteFileModel(this);
  m_filterModel = new RemoteFileFilterProxyModel(this);
  m_filterModel->setSourceModel(m_model);

  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(6);

  auto *toolbar = new QWidget(this);
  toolbar->setObjectName(QStringLiteral("remoteBrowserToolbar"));
  auto *toolbarLayout = new QVBoxLayout(toolbar);
  toolbarLayout->setContentsMargins(0, 0, 0, 0);
  toolbarLayout->setSpacing(6);
  auto *buttonLayout = new QHBoxLayout();
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->setSpacing(6);

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
  m_copyButton = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon),
                                 tr("Copy"), toolbar);
  m_copyButton->setObjectName(QStringLiteral("remoteBrowserCopyButton"));
  m_moveButton = new QPushButton(style()->standardIcon(QStyle::SP_DirIcon),
                                 tr("Move"), toolbar);
  m_moveButton->setObjectName(QStringLiteral("remoteBrowserMoveButton"));
  m_deleteButton = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon),
                                   tr("Delete"), toolbar);
  m_deleteButton->setObjectName(QStringLiteral("remoteBrowserDeleteButton"));
  m_renameButton = new QPushButton(style()->standardIcon(QStyle::SP_FileIcon),
                                   tr("Rename"), toolbar);
  m_renameButton->setObjectName(QStringLiteral("remoteBrowserRenameButton"));

  buttonLayout->addWidget(m_backButton);
  buttonLayout->addWidget(m_forwardButton);
  buttonLayout->addWidget(m_upButton);
  buttonLayout->addWidget(m_refreshButton);
  buttonLayout->addWidget(m_createFolderButton);
  buttonLayout->addWidget(m_uploadButton);
  buttonLayout->addWidget(m_downloadButton);
  buttonLayout->addWidget(m_copyButton);
  buttonLayout->addWidget(m_moveButton);
  buttonLayout->addWidget(m_renameButton);
  buttonLayout->addWidget(m_deleteButton);
  buttonLayout->addStretch(1);
  toolbarLayout->addLayout(buttonLayout);

  m_locationScrollArea = new QScrollArea(toolbar);
  m_locationScrollArea->setObjectName(QStringLiteral("remotePathScrollArea"));
  m_locationScrollArea->setFrameShape(QFrame::NoFrame);
  m_locationScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  m_locationScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_locationScrollArea->setWidgetResizable(true);
  m_locationScrollArea->setSizePolicy(QSizePolicy::Expanding,
                                      QSizePolicy::Fixed);
  m_locationScrollArea->setMinimumHeight(30);

  m_locationBar = new QWidget(m_locationScrollArea);
  m_locationBar->setObjectName(QStringLiteral("remotePathBar"));
  m_locationLayout = new QHBoxLayout(m_locationBar);
  m_locationLayout->setContentsMargins(0, 0, 0, 0);
  m_locationLayout->setSpacing(4);
  m_locationScrollArea->setWidget(m_locationBar);
  toolbarLayout->addWidget(m_locationScrollArea);

  m_searchEdit = new QLineEdit(toolbar);
  m_searchEdit->setObjectName(QStringLiteral("remoteFileSearchEdit"));
  m_searchEdit->setPlaceholderText(tr("Search current folder"));
  m_searchEdit->setMinimumWidth(240);
  m_searchEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto *searchLayout = new QHBoxLayout();
  searchLayout->setContentsMargins(0, 0, 0, 0);
  searchLayout->setSpacing(0);
  searchLayout->addWidget(m_searchEdit);
  toolbarLayout->addLayout(searchLayout);
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
  m_tableView->setDragEnabled(true);
  m_tableView->setDragDropMode(QAbstractItemView::DragOnly);
  m_tableView->setAcceptDrops(false);
  m_tableView->viewport()->setAcceptDrops(false);
  m_tableView->viewport()->installEventFilter(this);
  m_tableView->setAlternatingRowColors(true);
  m_tableView->setIconSize(QSize(22, 22));
  m_tableView->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Interactive);
  m_tableView->horizontalHeader()->setStretchLastSection(false);
  m_tableView->verticalHeader()->setVisible(false);
  rootLayout->addWidget(m_tableView, 1);
  QTimer::singleShot(0, this, &RemoteBrowserWidget::applyColumnProportions);

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
  connect(m_copyButton, &QPushButton::clicked, this,
          &RemoteBrowserWidget::copySelected);
  connect(m_moveButton, &QPushButton::clicked, this,
          &RemoteBrowserWidget::moveSelected);
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
            if (isBrowsableRemoteEntry(entry)) {
              openDirectory(entry.remotePath);
              return;
            }
            emit fileActivated(entry);
          });
  connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, [this]() { updateActionState(); });

  updateLocationBar();
  updateActionState();
}

void RemoteBrowserWidget::setDirectory(
    smb::application::OpenConnectionResult result) {
  m_connectionId = result.connection.id;
  m_locationRootText = locationRootText(result.connection);
  m_currentRemotePath = normalizeRemotePath(result.currentRemotePath);
  m_backStack.clear();
  m_forwardStack.clear();
  m_searchEdit->clear();
  m_model->setEntries(std::move(result.entries), m_currentRemotePath);
  updateLocationBar();
  showDirectoryState();
  updateActionState();
}

void RemoteBrowserWidget::clear() {
  m_connectionId.clear();
  m_locationRootText.clear();
  m_currentRemotePath.clear();
  m_backStack.clear();
  m_forwardStack.clear();
  m_searchEdit->clear();
  m_model->clear();
  updateLocationBar();
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

void RemoteBrowserWidget::copySelected() {
  const auto entries = selectedEntries();
  if (m_connectionId.isEmpty() || entries.isEmpty()) {
    return;
  }

  const auto destination = m_prompter.promptCopyDestination(
      this, m_connectionId, m_currentRemotePath, entries);
  if (!destination.has_value()) {
    return;
  }

  runFileOperation(QStringLiteral("copy"),
                   [this, entries, destination = destination.value()](
                       const smb::core::OperationContext &context) {
                     return copyOrMoveEntries(false, entries, destination,
                                              context);
                   });
}

void RemoteBrowserWidget::moveSelected() {
  const auto entries = selectedEntries();
  if (m_connectionId.isEmpty() || entries.isEmpty()) {
    return;
  }

  const auto destination = m_prompter.promptMoveDestination(
      this, m_connectionId, m_currentRemotePath, entries);
  if (!destination.has_value()) {
    return;
  }

  runFileOperation(QStringLiteral("move"),
                   [this, entries, destination = destination.value()](
                       const smb::core::OperationContext &context) {
                     return copyOrMoveEntries(true, entries, destination,
                                              context);
                   });
}

void RemoteBrowserWidget::prepareExternalDragForSelected() {
  if (m_connectionId.isEmpty()) {
    return;
  }

  QVector<smb::core::RemoteFileEntry> fileEntries;
  for (const auto &entry : selectedEntries()) {
    if (entry.isFile()) {
      fileEntries.push_back(entry);
    }
  }

  if (fileEntries.isEmpty()) {
    const auto error = smb::core::AppError::fromCode(
        smb::core::ErrorCode::InvalidPath, smb::core::ErrorCategory::Validation,
        tr("Only files can be dragged to the local desktop."));
    m_prompter.showError(this, tr("Unable to Drag Remote Items"), error);
    emit remoteOperationFailed(QStringLiteral("external_drag"), error);
    return;
  }

  const auto connectionId = m_connectionId;
  runFileOperation(
      QStringLiteral("external_drag"),
      [this, connectionId,
       fileEntries](const smb::core::OperationContext &context) {
        qint64 totalBytes = 0;
        for (const auto &entry : fileEntries) {
          totalBytes += qMax<qint64>(entry.size, 0);
        }
        if (totalBytes == 0) {
          totalBytes = fileEntries.size();
        }

        QVector<QUrl> urls;
        urls.reserve(fileEntries.size());
        qint64 completedBytes = 0;
        for (const auto &entry : fileEntries) {
          if (context.cancellationToken != nullptr &&
              context.cancellationToken->isCancellationRequested()) {
            return smb::core::Result<bool>::failure(
                smb::core::AppError::fromCode(
                    smb::core::ErrorCode::OperationCancelled,
                    smb::core::ErrorCategory::General,
                    tr("Operation cancelled.")));
          }

          const auto localPath =
              m_tempFileCache.localPathFor(connectionId, entry.remotePath);
          if (!localPath.ok()) {
            return smb::core::Result<bool>::failure(localPath.error());
          }

          auto itemContext = context;
          if (context.progressCallback) {
            itemContext.progressCallback =
                [&context, completedBytes,
                 totalBytes](const smb::core::TransferProgress &progress) {
                  context.progressCallback(smb::core::TransferProgress{
                      qMin(totalBytes,
                           completedBytes + progress.bytesTransferred),
                      totalBytes});
                };
          }

          auto downloaded = m_fileTransferUseCase.downloadFile(
              connectionId, entry.remotePath, localPath.value(), itemContext);
          if (!downloaded.ok()) {
            return downloaded;
          }

          completedBytes += entry.size > 0 ? entry.size : 1;
          if (context.progressCallback) {
            context.progressCallback(smb::core::TransferProgress{
                qMin(completedBytes, totalBytes), totalBytes});
          }
          urls.push_back(QUrl::fromLocalFile(localPath.value()));
        }

        QPointer<RemoteBrowserWidget> self(this);
        QMetaObject::invokeMethod(
            this,
            [self, urls]() {
              if (!self) {
                return;
              }
              emit self->externalDragReady(urls);
              if (self->m_startDragWhenReady) {
                self->m_startDragWhenReady = false;
                self->startExternalDragWithUrls(urls);
              }
            },
            Qt::QueuedConnection);
        return smb::core::Result<bool>::success(true);
      },
      false);
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

void RemoteBrowserWidget::dragEnterEvent(QDragEnterEvent *event) {
  if (!m_connectionId.isEmpty() && event->mimeData() != nullptr &&
      event->mimeData()->hasUrls()) {
    for (const auto &url : event->mimeData()->urls()) {
      if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) {
        event->acceptProposedAction();
        return;
      }
    }
  }

  event->ignore();
}

void RemoteBrowserWidget::dragMoveEvent(QDragMoveEvent *event) {
  if (!m_connectionId.isEmpty() && event->mimeData() != nullptr &&
      event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
    return;
  }

  event->ignore();
}

void RemoteBrowserWidget::dropEvent(QDropEvent *event) {
  if (m_connectionId.isEmpty() || event->mimeData() == nullptr ||
      !event->mimeData()->hasUrls()) {
    event->ignore();
    return;
  }

  QVector<QString> localPaths;
  for (const auto &url : event->mimeData()->urls()) {
    if (!url.isLocalFile()) {
      continue;
    }

    const QFileInfo info(url.toLocalFile());
    if (info.isFile()) {
      localPaths.push_back(info.absoluteFilePath());
    }
  }

  if (localPaths.isEmpty()) {
    event->ignore();
    return;
  }

  uploadLocalFiles(std::move(localPaths));
  event->acceptProposedAction();
}

void RemoteBrowserWidget::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  applyColumnProportions();
}

bool RemoteBrowserWidget::eventFilter(QObject *watched, QEvent *event) {
  if (watched != m_tableView->viewport()) {
    return QWidget::eventFilter(watched, event);
  }

  if (event->type() == QEvent::MouseButtonPress) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() == Qt::LeftButton) {
      m_dragStartPosition = mouseEvent->pos();
    }
    return QWidget::eventFilter(watched, event);
  }

  if (event->type() == QEvent::MouseMove) {
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (!(mouseEvent->buttons() & Qt::LeftButton)) {
      return QWidget::eventFilter(watched, event);
    }
    if ((mouseEvent->pos() - m_dragStartPosition).manhattanLength() <
        QApplication::startDragDistance()) {
      return QWidget::eventFilter(watched, event);
    }

    startExternalDragFromMouse();
    return true;
  }

  return QWidget::eventFilter(watched, event);
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
  m_locationRootText = locationRootText(result.connection);
  m_currentRemotePath = targetPath;
  m_searchEdit->clear();
  m_model->setEntries(std::move(result.entries), m_currentRemotePath);
  updateLocationBar();
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
  m_copyButton->setEnabled(hasConnection && hasSelection);
  m_moveButton->setEnabled(hasConnection && hasSelection);
  m_deleteButton->setEnabled(hasConnection && hasSelection);
  m_renameButton->setEnabled(hasConnection && hasSelection);
}

void RemoteBrowserWidget::updateLocationBar() {
  if (m_locationLayout == nullptr || m_locationScrollArea == nullptr) {
    return;
  }

  while (auto *item = m_locationLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  const auto hasConnection =
      !m_connectionId.isEmpty() && !m_currentRemotePath.isEmpty();
  m_locationScrollArea->setVisible(hasConnection);
  if (!hasConnection) {
    return;
  }

  const auto fullPath = displayLocation(m_locationRootText, m_currentRemotePath);
  const auto rootText = m_locationRootText.isEmpty() ? QStringLiteral("/")
                                                     : m_locationRootText;

  auto *rootButton = new QPushButton(
      style()->standardIcon(QStyle::SP_DriveNetIcon), rootText, m_locationBar);
  rootButton->setObjectName(QStringLiteral("remotePathRootButton"));
  rootButton->setFlat(true);
  rootButton->setFocusPolicy(Qt::NoFocus);
  rootButton->setToolTip(fullPath);
  rootButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  connect(rootButton, &QPushButton::clicked, this, [this]() {
    if (m_currentRemotePath != QStringLiteral("/")) {
      openDirectory(QStringLiteral("/"));
    }
  });
  m_locationLayout->addWidget(rootButton);

  const auto normalizedPath = normalizeRemotePath(m_currentRemotePath);
  if (normalizedPath != QStringLiteral("/")) {
    const auto segments =
        normalizedPath.mid(1).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString prefix;
    for (const auto &segment : segments) {
      prefix += QStringLiteral("/") + segment;

      auto *separator = new QLabel(QStringLiteral(">"), m_locationBar);
      separator->setObjectName(QStringLiteral("remotePathSeparator"));
      separator->setAlignment(Qt::AlignCenter);
      m_locationLayout->addWidget(separator);

      auto *segmentButton = new QPushButton(
          style()->standardIcon(QStyle::SP_DirIcon), segment, m_locationBar);
      segmentButton->setObjectName(QStringLiteral("remotePathSegmentButton"));
      segmentButton->setFlat(true);
      segmentButton->setFocusPolicy(Qt::NoFocus);
      segmentButton->setToolTip(displayLocation(m_locationRootText, prefix));
      segmentButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
      const auto segmentPath = prefix;
      connect(segmentButton, &QPushButton::clicked, this,
              [this, segmentPath]() {
                if (segmentPath != m_currentRemotePath) {
                  openDirectory(segmentPath);
                }
              });
      m_locationLayout->addWidget(segmentButton);
    }
  }

  m_locationLayout->addStretch(1);
}

void RemoteBrowserWidget::applyColumnProportions() {
  if (m_tableView == nullptr) {
    return;
  }

  static constexpr int kWeights[RemoteFileModel::ColumnCount] = {493, 99,  100,
                                                                 167, 100, 124};
  static constexpr int kMinimums[RemoteFileModel::ColumnCount] = {
      260, 90, 80, 160, 100, 110};
  constexpr int kTotalWeight = 493 + 99 + 100 + 167 + 100 + 124;

  const auto availableWidth = m_tableView->viewport()->width();
  if (availableWidth <= 0) {
    return;
  }

  int usedWidth = 0;
  for (int column = 0; column < RemoteFileModel::ColumnCount; ++column) {
    int width = 0;
    if (column == RemoteFileModel::ColumnCount - 1) {
      width = qMax(kMinimums[column], availableWidth - usedWidth);
    } else {
      width = qMax(kMinimums[column],
                   (availableWidth * kWeights[column]) / kTotalWeight);
      usedWidth += width;
    }
    m_tableView->setColumnWidth(column, width);
  }
}

smb::core::RemoteFileEntry RemoteBrowserWidget::selectedEntry() const {
  const auto selected = m_tableView->selectionModel()->selectedRows();
  if (selected.isEmpty()) {
    return {};
  }

  const auto sourceIndex = m_filterModel->mapToSource(selected.first());
  return m_model->entryAt(sourceIndex.row());
}

QVector<smb::core::RemoteFileEntry>
RemoteBrowserWidget::selectedEntries() const {
  const auto selected = m_tableView->selectionModel()->selectedRows();
  QVector<smb::core::RemoteFileEntry> entries;
  entries.reserve(selected.size());

  for (const auto &index : selected) {
    const auto sourceIndex = m_filterModel->mapToSource(index);
    const auto entry = m_model->entryAt(sourceIndex.row());
    if (!entry.name.isEmpty()) {
      entries.push_back(entry);
    }
  }

  return entries;
}

smb::core::Result<bool> RemoteBrowserWidget::copyOrMoveEntries(
    bool move, const QVector<smb::core::RemoteFileEntry> &entries,
    const RemoteDestination &destination,
    const smb::core::OperationContext &context) {
  if (destination.connectionId.trimmed().isEmpty() ||
      destination.remoteDirectory.trimmed().isEmpty()) {
    return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
        smb::core::ErrorCode::InvalidPath, smb::core::ErrorCategory::Validation,
        tr("Destination connection and folder are required.")));
  }

  qint64 totalBytes = 0;
  for (const auto &entry : entries) {
    totalBytes += qMax<qint64>(entry.size, 0);
  }
  if (totalBytes == 0) {
    totalBytes = entries.size();
  }

  qint64 completedBytes = 0;
  for (const auto &entry : entries) {
    if (context.cancellationToken != nullptr &&
        context.cancellationToken->isCancellationRequested()) {
      return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
          smb::core::ErrorCode::OperationCancelled,
          smb::core::ErrorCategory::General, tr("Operation cancelled.")));
    }

    auto itemContext = context;
    if (context.progressCallback) {
      itemContext.progressCallback =
          [&context, completedBytes,
           totalBytes](const smb::core::TransferProgress &progress) {
            context.progressCallback(smb::core::TransferProgress{
                qMin(totalBytes, completedBytes + progress.bytesTransferred),
                totalBytes});
          };
    }

    const auto targetPath =
        joinRemotePath(destination.remoteDirectory, entry.name);
    auto result =
        move ? m_fileTransferUseCase.move(m_connectionId, entry.remotePath,
                                          destination.connectionId, targetPath,
                                          itemContext)
             : m_fileTransferUseCase.copy(m_connectionId, entry.remotePath,
                                          destination.connectionId, targetPath,
                                          itemContext);
    if (!result.ok()) {
      return result;
    }

    completedBytes += entry.size > 0 ? entry.size : 1;
    if (context.progressCallback) {
      context.progressCallback(smb::core::TransferProgress{
          qMin(completedBytes, totalBytes), totalBytes});
    }
  }

  return smb::core::Result<bool>::success(true);
}

void RemoteBrowserWidget::uploadLocalFiles(QVector<QString> localPaths) {
  const auto connectionId = m_connectionId;
  const auto currentRemotePath = m_currentRemotePath;
  runFileOperation(
      QStringLiteral("drop_upload"),
      [this, connectionId, currentRemotePath,
       localPaths =
           std::move(localPaths)](const smb::core::OperationContext &context) {
        qint64 totalBytes = 0;
        for (const auto &localPath : localPaths) {
          totalBytes += qMax<qint64>(QFileInfo(localPath).size(), 0);
        }
        if (totalBytes == 0) {
          totalBytes = localPaths.size();
        }

        qint64 completedBytes = 0;
        for (const auto &localPath : localPaths) {
          if (context.cancellationToken != nullptr &&
              context.cancellationToken->isCancellationRequested()) {
            return smb::core::Result<bool>::failure(
                smb::core::AppError::fromCode(
                    smb::core::ErrorCode::OperationCancelled,
                    smb::core::ErrorCategory::General,
                    tr("Operation cancelled.")));
          }

          const QFileInfo info(localPath);
          const auto targetPath =
              joinRemotePath(currentRemotePath, info.fileName());
          auto itemContext = context;
          if (context.progressCallback) {
            itemContext.progressCallback =
                [&context, completedBytes,
                 totalBytes](const smb::core::TransferProgress &progress) {
                  context.progressCallback(smb::core::TransferProgress{
                      qMin(totalBytes,
                           completedBytes + progress.bytesTransferred),
                      totalBytes});
                };
          }

          auto uploaded = m_fileTransferUseCase.uploadFile(
              connectionId, localPath, targetPath, itemContext);
          if (!uploaded.ok()) {
            return uploaded;
          }

          completedBytes += info.size() > 0 ? info.size() : 1;
          if (context.progressCallback) {
            context.progressCallback(smb::core::TransferProgress{
                qMin(completedBytes, totalBytes), totalBytes});
          }
        }

        return smb::core::Result<bool>::success(true);
      });
}

void RemoteBrowserWidget::startExternalDragFromMouse() {
  m_startDragWhenReady = true;
  prepareExternalDragForSelected();
}

void RemoteBrowserWidget::startExternalDragWithUrls(const QVector<QUrl> &urls) {
  if (urls.isEmpty()) {
    return;
  }

  auto *mimeData = new QMimeData();
  mimeData->setUrls(QList<QUrl>(urls.cbegin(), urls.cend()));

  QDrag drag(this);
  drag.setMimeData(mimeData);
  drag.exec(Qt::CopyAction);
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
