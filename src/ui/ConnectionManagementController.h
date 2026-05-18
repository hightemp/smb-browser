#pragma once

#include "application/ConnectionService.h"
#include "ui/ConnectionActionPrompter.h"
#include "ui/ConnectionsPanel.h"

#include <QObject>

namespace smb::ui {

class ConnectionManagementController final : public QObject {
  Q_OBJECT

public:
  ConnectionManagementController(ConnectionsPanel &panel,
                                 smb::application::ConnectionService &service,
                                 ConnectionActionPrompter &prompter,
                                 QObject *parent = nullptr);

public slots:
  void refreshConnections();
  void addConnection();
  void editConnection(const QString &connectionId);
  void deleteConnection(const QString &connectionId);

signals:
  void connectionsRefreshed();
  void connectionAdded(const QString &connectionId);
  void connectionUpdated(const QString &connectionId);
  void connectionDeleted(const QString &connectionId);

private:
  ConnectionsPanel &m_panel;
  smb::application::ConnectionService &m_service;
  ConnectionActionPrompter &m_prompter;
};

} // namespace smb::ui
