#pragma once

#include "application/ConnectivityCheckService.h"
#include "application/OperationQueue.h"
#include "ui/ConnectionActionPrompter.h"
#include "ui/ConnectionsPanel.h"

#include <QObject>

namespace smb::ui {

class ConnectivityCheckController final : public QObject {
  Q_OBJECT

public:
  ConnectivityCheckController(
      ConnectionsPanel &panel,
      smb::application::ConnectivityCheckUseCase &checkUseCase,
      smb::application::OperationQueue &operationQueue,
      ConnectionActionPrompter &prompter, QObject *parent = nullptr);

public slots:
  void checkConnection(const QString &connectionId);

signals:
  void checkStarted(const QString &connectionId, const QString &operationId);
  void checkCompleted(
      const QString &connectionId,
      const smb::application::ConnectivityCheckResult &result);
  void checkFailed(const QString &connectionId,
                   const smb::core::AppError &error);

private:
  void deliverCheckCompleted(
      const QString &connectionId,
      smb::application::ConnectivityCheckResult result);
  void deliverCheckFailed(const QString &connectionId,
                          const smb::core::AppError &error);

  ConnectionsPanel &m_panel;
  smb::application::ConnectivityCheckUseCase &m_checkUseCase;
  smb::application::OperationQueue &m_operationQueue;
  ConnectionActionPrompter &m_prompter;
};

} // namespace smb::ui
