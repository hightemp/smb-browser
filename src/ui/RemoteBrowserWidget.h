#pragma once

#include "application/ConnectionOpenService.h"
#include "application/OperationQueue.h"
#include "ui/RemoteFileActionPrompter.h"
#include "ui/RemoteFileModel.h"

#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;

namespace smb::ui {

class RemoteBrowserWidget final : public QWidget {
  Q_OBJECT

public:
  explicit RemoteBrowserWidget(
      smb::application::RemoteDirectoryUseCase &directoryUseCase,
      smb::application::RemoteFileOperationUseCase &fileOperationUseCase,
      smb::application::RemoteFileTransferUseCase &fileTransferUseCase,
      smb::application::OperationQueue &operationQueue,
      RemoteFileActionPrompter &prompter, QWidget *parent = nullptr);

  void setDirectory(smb::application::OpenConnectionResult result);
  void clear();

  QString currentConnectionId() const;
  QString currentRemotePath() const;
  RemoteFileModel *model() const;

public slots:
  void openDirectory(const QString &remotePath);
  void goBack();
  void goForward();
  void goUp();
  void refresh();
  void createFolder();
  void deleteSelected();
  void renameSelected();
  void downloadSelected();
  void uploadFile();
  void copySelected();
  void moveSelected();

signals:
  void directoryLoadStarted(const QString &connectionId,
                            const QString &remotePath,
                            const QString &operationId);
  void directoryOpened(const QString &connectionId, const QString &remotePath);
  void directoryOpenFailed(const QString &connectionId,
                           const QString &remotePath,
                           const smb::core::AppError &error);
  void fileActivated(const smb::core::RemoteFileEntry &entry);
  void remoteOperationStarted(const QString &operationName,
                              const QString &operationId);
  void remoteOperationCompleted(const QString &operationName);
  void remoteOperationFailed(const QString &operationName,
                             const smb::core::AppError &error);

private:
  enum class HistoryMode {
    Push,
    Replace,
    Back,
    Forward,
  };

  void requestDirectory(const QString &remotePath, HistoryMode historyMode);
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void applyDirectory(smb::application::OpenConnectionResult result,
                      HistoryMode historyMode, const QString &previousPath);
  void deliverFailure(const QString &connectionId, const QString &remotePath,
                      const smb::core::AppError &error);
  void runFileOperation(const QString &operationName,
                        std::function<smb::core::Result<bool>(
                            const smb::core::OperationContext &)>
                            operation,
                        bool refreshAfterSuccess = true);
  void showLoading(const QString &remotePath);
  void showDirectoryState();
  void showError(const smb::core::AppError &error);
  void updateActionState();
  smb::core::RemoteFileEntry selectedEntry() const;
  QVector<smb::core::RemoteFileEntry> selectedEntries() const;
  smb::core::Result<bool>
  copyOrMoveEntries(bool move,
                    const QVector<smb::core::RemoteFileEntry> &entries,
                    const RemoteDestination &destination,
                    const smb::core::OperationContext &context);
  void uploadLocalFiles(QVector<QString> localPaths);

  static QString normalizeRemotePath(QString remotePath);
  static QString parentRemotePath(const QString &remotePath);
  static QString joinRemotePath(const QString &parentPath,
                                const QString &childName);
  static bool isValidEntryName(const QString &name);
  static smb::core::AppError invalidNameError();

  smb::application::RemoteDirectoryUseCase &m_directoryUseCase;
  smb::application::RemoteFileOperationUseCase &m_fileOperationUseCase;
  smb::application::RemoteFileTransferUseCase &m_fileTransferUseCase;
  smb::application::OperationQueue &m_operationQueue;
  RemoteFileActionPrompter &m_prompter;
  RemoteFileModel *m_model = nullptr;
  RemoteFileFilterProxyModel *m_filterModel = nullptr;
  QTableView *m_tableView = nullptr;
  QLabel *m_stateLabel = nullptr;
  QLineEdit *m_searchEdit = nullptr;
  QPushButton *m_backButton = nullptr;
  QPushButton *m_forwardButton = nullptr;
  QPushButton *m_upButton = nullptr;
  QPushButton *m_refreshButton = nullptr;
  QPushButton *m_createFolderButton = nullptr;
  QPushButton *m_uploadButton = nullptr;
  QPushButton *m_downloadButton = nullptr;
  QPushButton *m_copyButton = nullptr;
  QPushButton *m_moveButton = nullptr;
  QPushButton *m_deleteButton = nullptr;
  QPushButton *m_renameButton = nullptr;
  QString m_connectionId;
  QString m_currentRemotePath;
  QVector<QString> m_backStack;
  QVector<QString> m_forwardStack;
};

} // namespace smb::ui
