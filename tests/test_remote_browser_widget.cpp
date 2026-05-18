#include "ui/RemoteBrowserWidget.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QHash>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMutex>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>
#include <algorithm>

namespace {

QString normalizePath(QString path) {
  path = path.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (path.isEmpty()) {
    return QStringLiteral("/");
  }
  if (!path.startsWith(QLatin1Char('/'))) {
    path.prepend(QLatin1Char('/'));
  }
  while (path.size() > 1 && path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  return path;
}

QString parentPath(const QString &path) {
  const auto normalized = normalizePath(path);
  const auto index = normalized.lastIndexOf(QLatin1Char('/'));
  if (index <= 0) {
    return QStringLiteral("/");
  }
  return normalized.left(index);
}

QString fileName(const QString &path) {
  const auto normalized = normalizePath(path);
  const auto index = normalized.lastIndexOf(QLatin1Char('/'));
  if (index < 0) {
    return normalized;
  }
  return normalized.mid(index + 1);
}

smb::core::Connection connection(const QString &id = QStringLiteral("conn-1")) {
  auto value = smb::core::Connection::createEmpty();
  value.id = id;
  value.name = QStringLiteral("Engineering");
  value.normalizedUri = QStringLiteral("smb://server/share");
  value.server = QStringLiteral("server");
  value.share = QStringLiteral("share");
  return value;
}

smb::core::RemoteFileEntry directory(const QString &name, const QString &path) {
  smb::core::RemoteFileEntry entry;
  entry.name = name;
  entry.remotePath = path;
  entry.type = smb::core::RemoteFileType::Directory;
  return entry;
}

smb::core::RemoteFileEntry file(const QString &name, const QString &path) {
  smb::core::RemoteFileEntry entry;
  entry.name = name;
  entry.remotePath = path;
  entry.type = smb::core::RemoteFileType::File;
  entry.size = 42;
  return entry;
}

smb::application::OpenConnectionResult
directoryResult(const QString &path,
                QVector<smb::core::RemoteFileEntry> entries) {
  smb::application::OpenConnectionResult result;
  result.connection = connection();
  result.currentRemotePath = normalizePath(path);
  result.entries = std::move(entries);
  return result;
}

class FakeRemoteBrowserUseCase final
    : public smb::application::RemoteDirectoryUseCase,
      public smb::application::RemoteFileOperationUseCase,
      public smb::application::RemoteFileTransferUseCase {
public:
  smb::core::Result<smb::application::OpenConnectionResult>
  listDirectory(const QString &connectionId, const QString &remotePath,
                const smb::core::OperationContext &context) override {
    QMutexLocker locker(&mutex);
    const auto path = normalizePath(remotePath);
    requestedPaths.push_back(path);

    if (context.cancellationToken != nullptr &&
        context.cancellationToken->isCancellationRequested()) {
      return smb::core::Result<smb::application::OpenConnectionResult>::failure(
          smb::core::AppError::fromCode(
              smb::core::ErrorCode::OperationCancelled,
              smb::core::ErrorCategory::General,
              QStringLiteral("Operation cancelled.")));
    }

    if (failures.contains(path)) {
      return smb::core::Result<smb::application::OpenConnectionResult>::failure(
          failures.value(path));
    }

    smb::application::OpenConnectionResult result;
    result.connection = connection(connectionId);
    result.currentRemotePath = path;
    result.entries = directories.value(path);
    return smb::core::Result<smb::application::OpenConnectionResult>::success(
        result);
  }

  smb::core::Result<bool>
  createDirectory(const QString &, const QString &remotePath,
                  const smb::core::OperationContext &) override {
    QMutexLocker locker(&mutex);
    const auto path = normalizePath(remotePath);
    operationPaths.push_back(QStringLiteral("create:%1").arg(path));
    if (operationFailures.contains(path)) {
      return smb::core::Result<bool>::failure(operationFailures.value(path));
    }
    if (directories.contains(path)) {
      return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
          smb::core::ErrorCode::AlreadyExists, smb::core::ErrorCategory::Smb,
          QStringLiteral("Already exists.")));
    }

    directories.insert(path, {});
    auto parentEntries = directories.value(parentPath(path));
    parentEntries.push_back(directory(fileName(path), path));
    directories.insert(parentPath(path), parentEntries);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> remove(const QString &, const QString &remotePath,
                                 const smb::core::OperationContext &) override {
    QMutexLocker locker(&mutex);
    const auto path = normalizePath(remotePath);
    operationPaths.push_back(QStringLiteral("delete:%1").arg(path));
    if (operationFailures.contains(path)) {
      return smb::core::Result<bool>::failure(operationFailures.value(path));
    }

    auto parentEntries = directories.value(parentPath(path));
    parentEntries.erase(std::remove_if(parentEntries.begin(),
                                       parentEntries.end(),
                                       [path](const auto &entry) {
                                         return entry.remotePath == path;
                                       }),
                        parentEntries.end());
    directories.insert(parentPath(path), parentEntries);
    directories.remove(path);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> rename(const QString &,
                                 const QString &sourceRemotePath,
                                 const QString &targetRemotePath,
                                 const smb::core::OperationContext &) override {
    QMutexLocker locker(&mutex);
    const auto source = normalizePath(sourceRemotePath);
    const auto target = normalizePath(targetRemotePath);
    operationPaths.push_back(
        QStringLiteral("rename:%1:%2").arg(source, target));
    if (operationFailures.contains(source)) {
      return smb::core::Result<bool>::failure(operationFailures.value(source));
    }

    auto parentEntries = directories.value(parentPath(source));
    for (auto &entry : parentEntries) {
      if (entry.remotePath == source) {
        entry.name = fileName(target);
        entry.remotePath = target;
        break;
      }
    }
    directories.insert(parentPath(source), parentEntries);

    if (directories.contains(source)) {
      directories.insert(target, directories.take(source));
    }
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool>
  downloadFile(const QString &, const QString &remotePath,
               const QString &localPath,
               const smb::core::OperationContext &) override {
    QMutexLocker locker(&mutex);
    const auto path = normalizePath(remotePath);
    operationPaths.push_back(QStringLiteral("download:%1").arg(path));
    if (operationFailures.contains(path)) {
      return smb::core::Result<bool>::failure(operationFailures.value(path));
    }

    QFile file(localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
          smb::core::ErrorCode::LocalIoError,
          smb::core::ErrorCategory::Transfer,
          QStringLiteral("Unable to write local file.")));
    }
    file.write("downloaded");
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool>
  uploadFile(const QString &, const QString &localPath,
             const QString &remotePath,
             const smb::core::OperationContext &) override {
    QMutexLocker locker(&mutex);
    const auto path = normalizePath(remotePath);
    operationPaths.push_back(QStringLiteral("upload:%1").arg(path));
    if (operationFailures.contains(path)) {
      return smb::core::Result<bool>::failure(operationFailures.value(path));
    }

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
      return smb::core::Result<bool>::failure(smb::core::AppError::fromCode(
          smb::core::ErrorCode::LocalIoError,
          smb::core::ErrorCategory::Transfer,
          QStringLiteral("Unable to read local file.")));
    }

    auto parentEntries = directories.value(parentPath(path));
    parentEntries.push_back(::file(fileName(path), path));
    directories.insert(parentPath(path), parentEntries);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> copy(const QString &, const QString &sourceRemotePath,
                               const QString &, const QString &targetRemotePath,
                               const smb::core::OperationContext &) override {
    QMutexLocker locker(&mutex);
    const auto source = normalizePath(sourceRemotePath);
    const auto target = normalizePath(targetRemotePath);
    operationPaths.push_back(QStringLiteral("copy:%1:%2").arg(source, target));
    if (operationFailures.contains(source)) {
      return smb::core::Result<bool>::failure(operationFailures.value(source));
    }

    auto parentEntries = directories.value(parentPath(target));
    if (directories.contains(source)) {
      directories.insert(target, directories.value(source));
      parentEntries.push_back(directory(fileName(target), target));
    } else {
      parentEntries.push_back(::file(fileName(target), target));
    }
    directories.insert(parentPath(target), parentEntries);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool>
  move(const QString &sourceConnectionId, const QString &sourceRemotePath,
       const QString &targetConnectionId, const QString &targetRemotePath,
       const smb::core::OperationContext &context) override {
    Q_UNUSED(sourceConnectionId)
    Q_UNUSED(targetConnectionId)
    Q_UNUSED(context)

    QMutexLocker locker(&mutex);
    const auto source = normalizePath(sourceRemotePath);
    const auto target = normalizePath(targetRemotePath);
    operationPaths.push_back(QStringLiteral("move:%1:%2").arg(source, target));
    if (operationFailures.contains(source)) {
      return smb::core::Result<bool>::failure(operationFailures.value(source));
    }

    auto targetParentEntries = directories.value(parentPath(target));
    if (directories.contains(source)) {
      directories.insert(target, directories.value(source));
      targetParentEntries.push_back(directory(fileName(target), target));
    } else {
      targetParentEntries.push_back(::file(fileName(target), target));
    }
    directories.insert(parentPath(target), targetParentEntries);

    auto sourceParentEntries = directories.value(parentPath(source));
    sourceParentEntries.erase(
        std::remove_if(
            sourceParentEntries.begin(), sourceParentEntries.end(),
            [source](const auto &entry) { return entry.remotePath == source; }),
        sourceParentEntries.end());
    directories.insert(parentPath(source), sourceParentEntries);
    directories.remove(source);
    return smb::core::Result<bool>::success(true);
  }

  QMutex mutex;
  QHash<QString, QVector<smb::core::RemoteFileEntry>> directories;
  QHash<QString, smb::core::AppError> failures;
  QHash<QString, smb::core::AppError> operationFailures;
  QVector<QString> requestedPaths;
  QVector<QString> operationPaths;
};

class FakeRemoteFilePrompter final : public smb::ui::RemoteFileActionPrompter {
public:
  std::optional<QString> promptCreateFolderName(QWidget *,
                                                const QString &) override {
    return nextCreateFolderName;
  }

  bool confirmDelete(QWidget *,
                     const smb::core::RemoteFileEntry &entry) override {
    confirmedDeletes.push_back(entry.remotePath);
    return confirmDeleteResult;
  }

  std::optional<QString>
  promptRename(QWidget *, const smb::core::RemoteFileEntry &entry) override {
    renamePrompts.push_back(entry.remotePath);
    return nextRenameName;
  }

  std::optional<QString>
  promptDownloadPath(QWidget *,
                     const smb::core::RemoteFileEntry &entry) override {
    downloadPrompts.push_back(entry.remotePath);
    return nextDownloadPath;
  }

  std::optional<QString>
  promptUploadPath(QWidget *, const QString &currentRemotePath) override {
    uploadPrompts.push_back(currentRemotePath);
    return nextUploadPath;
  }

  std::optional<smb::ui::RemoteDestination> promptCopyDestination(
      QWidget *, const QString &, const QString &,
      const QVector<smb::core::RemoteFileEntry> &entries) override {
    copyPrompts.push_back(entries.size());
    return nextCopyDestination;
  }

  std::optional<smb::ui::RemoteDestination> promptMoveDestination(
      QWidget *, const QString &, const QString &,
      const QVector<smb::core::RemoteFileEntry> &entries) override {
    movePrompts.push_back(entries.size());
    return nextMoveDestination;
  }

  void showError(QWidget *, const QString &title,
                 const smb::core::AppError &error) override {
    errors.push_back(title + QStringLiteral(":") +
                     smb::core::toString(error.code));
  }

  std::optional<QString> nextCreateFolderName;
  std::optional<QString> nextRenameName;
  std::optional<QString> nextDownloadPath;
  std::optional<QString> nextUploadPath;
  std::optional<smb::ui::RemoteDestination> nextCopyDestination;
  std::optional<smb::ui::RemoteDestination> nextMoveDestination;
  bool confirmDeleteResult = true;
  QVector<QString> confirmedDeletes;
  QVector<QString> renamePrompts;
  QVector<QString> downloadPrompts;
  QVector<QString> uploadPrompts;
  QVector<int> copyPrompts;
  QVector<int> movePrompts;
  QVector<QString> errors;
};

} // namespace

class RemoteBrowserWidgetTest final : public QObject {
  Q_OBJECT

private slots:
  void opensFolderAndDisplaysEntries() {
    FakeRemoteBrowserUseCase useCase;
    useCase.directories.insert(QStringLiteral("/docs"),
                               {file(QStringLiteral("readme.txt"),
                                     QStringLiteral("/docs/readme.txt"))});

    smb::application::OperationQueue operationQueue(1);
    FakeRemoteFilePrompter prompter;
    smb::ui::RemoteBrowserWidget widget(useCase, useCase, useCase,
                                        operationQueue, prompter);
    widget.setDirectory(directoryResult(
        QStringLiteral("/"),
        {directory(QStringLiteral("docs"), QStringLiteral("/docs"))}));

    QCOMPARE(widget.currentRemotePath(), QStringLiteral("/"));
    QCOMPARE(widget.model()->rowCount(), 1);

    QVector<QString> openedPaths;
    connect(&widget, &smb::ui::RemoteBrowserWidget::directoryOpened, this,
            [&openedPaths](const QString &, const QString &path) {
              openedPaths.push_back(path);
            });

    widget.openDirectory(QStringLiteral("/docs"));

    QTRY_COMPARE(openedPaths.size(), 1);
    QCOMPARE(openedPaths.first(), QStringLiteral("/docs"));
    QCOMPARE(widget.currentRemotePath(), QStringLiteral("/docs"));
    QCOMPARE(widget.model()->rowCount(), 1);
    QCOMPARE(widget.model()->entryAt(0).name, QStringLiteral("readme.txt"));
  }

  void supportsBackForwardUpAndRefresh() {
    FakeRemoteBrowserUseCase useCase;
    useCase.directories.insert(
        QStringLiteral("/docs"),
        {directory(QStringLiteral("sub"), QStringLiteral("/docs/sub"))});
    useCase.directories.insert(QStringLiteral("/docs/sub"),
                               {file(QStringLiteral("deep.txt"),
                                     QStringLiteral("/docs/sub/deep.txt"))});

    smb::application::OperationQueue operationQueue(1);
    FakeRemoteFilePrompter prompter;
    smb::ui::RemoteBrowserWidget widget(useCase, useCase, useCase,
                                        operationQueue, prompter);
    widget.setDirectory(directoryResult(
        QStringLiteral("/"),
        {directory(QStringLiteral("docs"), QStringLiteral("/docs"))}));

    widget.openDirectory(QStringLiteral("/docs"));
    QTRY_COMPARE(widget.currentRemotePath(), QStringLiteral("/docs"));
    widget.openDirectory(QStringLiteral("/docs/sub"));
    QTRY_COMPARE(widget.currentRemotePath(), QStringLiteral("/docs/sub"));

    auto *backButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserBackButton"));
    auto *forwardButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserForwardButton"));
    auto *upButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserUpButton"));
    auto *refreshButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserRefreshButton"));
    QVERIFY(backButton != nullptr);
    QVERIFY(forwardButton != nullptr);
    QVERIFY(upButton != nullptr);
    QVERIFY(refreshButton != nullptr);

    QVERIFY(backButton->isEnabled());
    backButton->click();
    QTRY_COMPARE(widget.currentRemotePath(), QStringLiteral("/docs"));

    QVERIFY(forwardButton->isEnabled());
    forwardButton->click();
    QTRY_COMPARE(widget.currentRemotePath(), QStringLiteral("/docs/sub"));

    QVERIFY(upButton->isEnabled());
    upButton->click();
    QTRY_COMPARE(widget.currentRemotePath(), QStringLiteral("/docs"));

    {
      QMutexLocker locker(&useCase.mutex);
      useCase.directories.insert(QStringLiteral("/docs"),
                                 {file(QStringLiteral("updated.txt"),
                                       QStringLiteral("/docs/updated.txt"))});
    }
    refreshButton->click();
    QTRY_COMPARE(widget.model()->entryAt(0).name,
                 QStringLiteral("updated.txt"));
  }

  void showsEmptyAndErrorStates() {
    FakeRemoteBrowserUseCase useCase;
    useCase.directories.insert(QStringLiteral("/empty"), {});
    useCase.failures.insert(
        QStringLiteral("/restricted"),
        smb::core::AppError::fromCode(smb::core::ErrorCode::PermissionDenied,
                                      smb::core::ErrorCategory::Smb,
                                      QStringLiteral("Permission denied.")));

    smb::application::OperationQueue operationQueue(1);
    FakeRemoteFilePrompter prompter;
    smb::ui::RemoteBrowserWidget widget(useCase, useCase, useCase,
                                        operationQueue, prompter);
    widget.setDirectory(directoryResult(QStringLiteral("/"), {}));

    auto *stateLabel =
        widget.findChild<QLabel *>(QStringLiteral("remoteBrowserStateLabel"));
    QVERIFY(stateLabel != nullptr);
    QCOMPARE(stateLabel->text(), QStringLiteral("No files in this folder."));

    widget.openDirectory(QStringLiteral("/empty"));
    QTRY_COMPARE(widget.currentRemotePath(), QStringLiteral("/empty"));
    QCOMPARE(stateLabel->text(), QStringLiteral("No files in this folder."));

    QVector<smb::core::AppError> errors;
    connect(&widget, &smb::ui::RemoteBrowserWidget::directoryOpenFailed, this,
            [&errors](const QString &, const QString &,
                      const smb::core::AppError &error) {
              errors.push_back(error);
            });

    widget.openDirectory(QStringLiteral("/restricted"));

    QTRY_COMPARE(errors.size(), 1);
    QVERIFY(errors.first().code == smb::core::ErrorCode::PermissionDenied);
    QCOMPARE(widget.currentRemotePath(), QStringLiteral("/empty"));
    QVERIFY(stateLabel->text().contains(QStringLiteral("permission"),
                                        Qt::CaseInsensitive));
  }

  void searchFiltersCurrentFolderWithoutSmbRequest() {
    FakeRemoteBrowserUseCase useCase;
    smb::application::OperationQueue operationQueue(1);
    FakeRemoteFilePrompter prompter;
    smb::ui::RemoteBrowserWidget widget(useCase, useCase, useCase,
                                        operationQueue, prompter);
    widget.setDirectory(directoryResult(
        QStringLiteral("/"),
        {directory(QStringLiteral("docs"), QStringLiteral("/docs")),
         file(QStringLiteral("report.txt"), QStringLiteral("/report.txt"))}));

    auto *search =
        widget.findChild<QLineEdit *>(QStringLiteral("remoteFileSearchEdit"));
    auto *view =
        widget.findChild<QTableView *>(QStringLiteral("remoteFilesView"));
    auto *stateLabel =
        widget.findChild<QLabel *>(QStringLiteral("remoteBrowserStateLabel"));
    QVERIFY(search != nullptr);
    QVERIFY(view != nullptr);
    QVERIFY(stateLabel != nullptr);

    QCOMPARE(view->model()->rowCount(), 2);

    search->setText(QStringLiteral("report"));
    QCOMPARE(view->model()->rowCount(), 1);
    QCOMPARE(view->model()
                 ->data(view->model()->index(
                     0, smb::ui::RemoteFileModel::NameColumn))
                 .toString(),
             QStringLiteral("report.txt"));

    search->setText(QStringLiteral("missing"));
    QCOMPARE(view->model()->rowCount(), 0);
    QCOMPARE(stateLabel->text(), QStringLiteral("No matching files."));

    {
      QMutexLocker locker(&useCase.mutex);
      QVERIFY(useCase.requestedPaths.isEmpty());
    }
  }

  void createRenameAndDeleteRefreshCurrentFolder() {
    FakeRemoteBrowserUseCase useCase;
    useCase.directories.insert(QStringLiteral("/"), {});
    smb::application::OperationQueue operationQueue(1);
    FakeRemoteFilePrompter prompter;
    smb::ui::RemoteBrowserWidget widget(useCase, useCase, useCase,
                                        operationQueue, prompter);
    widget.setDirectory(directoryResult(QStringLiteral("/"), {}));

    auto *view =
        widget.findChild<QTableView *>(QStringLiteral("remoteFilesView"));
    auto *createButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserCreateFolderButton"));
    auto *renameButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserRenameButton"));
    auto *deleteButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserDeleteButton"));
    QVERIFY(view != nullptr);
    QVERIFY(createButton != nullptr);
    QVERIFY(renameButton != nullptr);
    QVERIFY(deleteButton != nullptr);

    prompter.nextCreateFolderName = QStringLiteral("New Folder");
    createButton->click();
    QTRY_COMPARE(widget.model()->rowCount(), 1);
    QCOMPARE(widget.model()->entryAt(0).name, QStringLiteral("New Folder"));

    view->selectionModel()->select(view->model()->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect |
                                       QItemSelectionModel::Rows);
    prompter.nextRenameName = QStringLiteral("Renamed");
    renameButton->click();
    QTRY_COMPARE(widget.model()->entryAt(0).name, QStringLiteral("Renamed"));

    view->selectionModel()->select(view->model()->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect |
                                       QItemSelectionModel::Rows);
    deleteButton->click();
    QTRY_COMPARE(widget.model()->rowCount(), 0);
    QCOMPARE(prompter.confirmedDeletes,
             QVector<QString>{QStringLiteral("/Renamed")});
  }

  void operationErrorsAreShownAndInvalidNamesAreRejected() {
    FakeRemoteBrowserUseCase useCase;
    useCase.directories.insert(
        QStringLiteral("/"),
        {file(QStringLiteral("locked.txt"), QStringLiteral("/locked.txt"))});
    useCase.operationFailures.insert(
        QStringLiteral("/locked.txt"),
        smb::core::AppError::fromCode(smb::core::ErrorCode::PermissionDenied,
                                      smb::core::ErrorCategory::Smb,
                                      QStringLiteral("Permission denied.")));

    smb::application::OperationQueue operationQueue(1);
    FakeRemoteFilePrompter prompter;
    smb::ui::RemoteBrowserWidget widget(useCase, useCase, useCase,
                                        operationQueue, prompter);
    widget.setDirectory(directoryResult(
        QStringLiteral("/"),
        {file(QStringLiteral("locked.txt"), QStringLiteral("/locked.txt"))}));

    auto *view =
        widget.findChild<QTableView *>(QStringLiteral("remoteFilesView"));
    auto *createButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserCreateFolderButton"));
    auto *deleteButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserDeleteButton"));
    QVERIFY(view != nullptr);
    QVERIFY(createButton != nullptr);
    QVERIFY(deleteButton != nullptr);

    prompter.nextCreateFolderName = QStringLiteral("bad/name");
    createButton->click();
    QVERIFY(!prompter.errors.isEmpty());

    view->selectionModel()->select(view->model()->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect |
                                       QItemSelectionModel::Rows);
    deleteButton->click();
    QTRY_VERIFY(prompter.errors.size() >= 2);
    QVERIFY(
        prompter.errors.last().contains(QStringLiteral("permission_denied")));
  }

  void downloadAndUploadUseAsyncTransferFlow() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    FakeRemoteBrowserUseCase useCase;
    useCase.directories.insert(
        QStringLiteral("/"),
        {file(QStringLiteral("remote.txt"), QStringLiteral("/remote.txt"))});

    smb::application::OperationQueue operationQueue(1);
    FakeRemoteFilePrompter prompter;
    smb::ui::RemoteBrowserWidget widget(useCase, useCase, useCase,
                                        operationQueue, prompter);
    widget.setDirectory(directoryResult(
        QStringLiteral("/"),
        {file(QStringLiteral("remote.txt"), QStringLiteral("/remote.txt"))}));

    auto *view =
        widget.findChild<QTableView *>(QStringLiteral("remoteFilesView"));
    auto *downloadButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserDownloadButton"));
    auto *uploadButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserUploadButton"));
    QVERIFY(view != nullptr);
    QVERIFY(downloadButton != nullptr);
    QVERIFY(uploadButton != nullptr);

    const auto downloadedPath =
        tempDir.filePath(QStringLiteral("downloaded.txt"));
    prompter.nextDownloadPath = downloadedPath;
    view->selectionModel()->select(view->model()->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect |
                                       QItemSelectionModel::Rows);
    downloadButton->click();
    QTRY_VERIFY(QFile::exists(downloadedPath));
    QFile downloaded(downloadedPath);
    QVERIFY(downloaded.open(QIODevice::ReadOnly));
    QCOMPARE(downloaded.readAll(), QByteArrayLiteral("downloaded"));

    const auto uploadPath = tempDir.filePath(QStringLiteral("upload.txt"));
    {
      QFile upload(uploadPath);
      QVERIFY(upload.open(QIODevice::WriteOnly | QIODevice::Truncate));
      upload.write("uploaded");
    }

    prompter.nextUploadPath = uploadPath;
    uploadButton->click();
    QTRY_VERIFY(widget.model()->rowCount() >= 2);

    bool foundUpload = false;
    for (const auto &entry : widget.model()->entries()) {
      if (entry.name == QStringLiteral("upload.txt")) {
        foundUpload = true;
        break;
      }
    }
    QVERIFY(foundUpload);
  }

  void copyAndMoveSelectedEntriesToDestination() {
    FakeRemoteBrowserUseCase useCase;
    useCase.directories.insert(
        QStringLiteral("/"),
        {file(QStringLiteral("a.txt"), QStringLiteral("/a.txt")),
         file(QStringLiteral("b.txt"), QStringLiteral("/b.txt"))});
    useCase.directories.insert(QStringLiteral("/dest"), {});

    smb::application::OperationQueue operationQueue(1);
    FakeRemoteFilePrompter prompter;
    smb::ui::RemoteBrowserWidget widget(useCase, useCase, useCase,
                                        operationQueue, prompter);
    widget.setDirectory(directoryResult(
        QStringLiteral("/"),
        {file(QStringLiteral("a.txt"), QStringLiteral("/a.txt")),
         file(QStringLiteral("b.txt"), QStringLiteral("/b.txt"))}));

    auto *view =
        widget.findChild<QTableView *>(QStringLiteral("remoteFilesView"));
    auto *copyButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserCopyButton"));
    auto *moveButton = widget.findChild<QPushButton *>(
        QStringLiteral("remoteBrowserMoveButton"));
    QVERIFY(view != nullptr);
    QVERIFY(copyButton != nullptr);
    QVERIFY(moveButton != nullptr);

    view->selectionModel()->select(view->model()->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect |
                                       QItemSelectionModel::Rows);
    view->selectionModel()->select(view->model()->index(1, 0),
                                   QItemSelectionModel::Select |
                                       QItemSelectionModel::Rows);
    prompter.nextCopyDestination = smb::ui::RemoteDestination{
        QStringLiteral("conn-2"), QStringLiteral("/dest")};
    copyButton->click();

    QTRY_COMPARE(prompter.copyPrompts, QVector<int>{2});
    QTRY_VERIFY([&useCase]() {
      QMutexLocker locker(&useCase.mutex);
      return useCase.operationPaths.contains(
                 QStringLiteral("copy:/a.txt:/dest/a.txt")) &&
             useCase.operationPaths.contains(
                 QStringLiteral("copy:/b.txt:/dest/b.txt"));
    }());

    view->selectionModel()->select(view->model()->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect |
                                       QItemSelectionModel::Rows);
    view->selectionModel()->select(view->model()->index(1, 0),
                                   QItemSelectionModel::Select |
                                       QItemSelectionModel::Rows);
    prompter.nextMoveDestination = smb::ui::RemoteDestination{
        QStringLiteral("conn-2"), QStringLiteral("/dest")};
    moveButton->click();
    QTRY_COMPARE(widget.model()->rowCount(), 0);
    QCOMPARE(prompter.movePrompts, QVector<int>{2});
  }

  void dragAndDropLocalFilesUploadsToCurrentFolder() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const auto firstPath = tempDir.filePath(QStringLiteral("first.txt"));
    const auto secondPath = tempDir.filePath(QStringLiteral("second.txt"));
    {
      QFile first(firstPath);
      QVERIFY(first.open(QIODevice::WriteOnly | QIODevice::Truncate));
      first.write("first");
      QFile second(secondPath);
      QVERIFY(second.open(QIODevice::WriteOnly | QIODevice::Truncate));
      second.write("second");
    }

    FakeRemoteBrowserUseCase useCase;
    useCase.directories.insert(QStringLiteral("/"), {});
    smb::application::OperationQueue operationQueue(1);
    FakeRemoteFilePrompter prompter;
    smb::ui::RemoteBrowserWidget widget(useCase, useCase, useCase,
                                        operationQueue, prompter);
    widget.setDirectory(directoryResult(QStringLiteral("/"), {}));
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    QMimeData mimeData;
    mimeData.setUrls(
        {QUrl::fromLocalFile(firstPath), QUrl::fromLocalFile(secondPath)});

    QDragEnterEvent dragEnterEvent(QPoint(10, 10), Qt::CopyAction, &mimeData,
                                   Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &dragEnterEvent);
    QVERIFY(dragEnterEvent.isAccepted());

    QDropEvent dropEvent(QPointF(10, 10), Qt::CopyAction, &mimeData,
                         Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &dropEvent);

    QTRY_VERIFY(widget.model()->rowCount() >= 2);
    bool foundFirst = false;
    bool foundSecond = false;
    for (const auto &entry : widget.model()->entries()) {
      foundFirst = foundFirst || entry.name == QStringLiteral("first.txt");
      foundSecond = foundSecond || entry.name == QStringLiteral("second.txt");
    }
    QVERIFY(foundFirst);
    QVERIFY(foundSecond);

    QMutexLocker locker(&useCase.mutex);
    QVERIFY(
        useCase.operationPaths.contains(QStringLiteral("upload:/first.txt")));
    QVERIFY(
        useCase.operationPaths.contains(QStringLiteral("upload:/second.txt")));
  }

  void preparesSelectedRemoteFilesForExternalDrag() {
    FakeRemoteBrowserUseCase useCase;
    useCase.directories.insert(
        QStringLiteral("/"),
        {file(QStringLiteral("remote.txt"), QStringLiteral("/remote.txt"))});

    smb::application::OperationQueue operationQueue(1);
    FakeRemoteFilePrompter prompter;
    smb::ui::RemoteBrowserWidget widget(useCase, useCase, useCase,
                                        operationQueue, prompter);
    widget.setDirectory(directoryResult(
        QStringLiteral("/"),
        {file(QStringLiteral("remote.txt"), QStringLiteral("/remote.txt"))}));

    auto *view =
        widget.findChild<QTableView *>(QStringLiteral("remoteFilesView"));
    QVERIFY(view != nullptr);
    view->selectionModel()->select(view->model()->index(0, 0),
                                   QItemSelectionModel::ClearAndSelect |
                                       QItemSelectionModel::Rows);

    QVector<QUrl> readyUrls;
    connect(&widget, &smb::ui::RemoteBrowserWidget::externalDragReady, this,
            [&readyUrls](const QVector<QUrl> &urls) { readyUrls = urls; });

    widget.prepareExternalDragForSelected();

    QTRY_COMPARE(readyUrls.size(), 1);
    QVERIFY(readyUrls.first().isLocalFile());
    QFile downloaded(readyUrls.first().toLocalFile());
    QVERIFY(downloaded.open(QIODevice::ReadOnly));
    QCOMPARE(downloaded.readAll(), QByteArrayLiteral("downloaded"));

    QMutexLocker locker(&useCase.mutex);
    QVERIFY(
        useCase.operationPaths.contains(QStringLiteral("download:/remote.txt")));
  }
};

QTEST_MAIN(RemoteBrowserWidgetTest)

#include "test_remote_browser_widget.moc"
