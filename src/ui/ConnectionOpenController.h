#pragma once

#include "application/ConnectionOpenService.h"
#include "application/OperationQueue.h"
#include "ui/ConnectionActionPrompter.h"
#include "ui/ConnectionsPanel.h"

#include <QObject>

namespace smb::ui {

class ConnectionOpenController final : public QObject {
  Q_OBJECT

public:
  ConnectionOpenController(ConnectionsPanel &panel,
                           smb::application::ConnectionOpenUseCase &openUseCase,
                           smb::application::OperationQueue &operationQueue,
                           ConnectionActionPrompter &prompter,
                           QObject *parent = nullptr);

public slots:
  void openConnection(const QString &connectionId);

signals:
  void openStarted(const QString &connectionId, const QString &operationId);
  void connectionOpened(const smb::application::OpenConnectionResult &result);
  void connectionOpenFailed(const QString &connectionId,
                            const smb::core::AppError &error);

private:
  void deliverOpenFailure(const QString &connectionId,
                          const smb::core::AppError &error);
  void deliverOpenSuccess(smb::application::OpenConnectionResult result);

  ConnectionsPanel &m_panel;
  smb::application::ConnectionOpenUseCase &m_openUseCase;
  smb::application::OperationQueue &m_operationQueue;
  ConnectionActionPrompter &m_prompter;
};

} // namespace smb::ui
