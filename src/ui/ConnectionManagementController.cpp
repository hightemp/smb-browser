#include "ui/ConnectionManagementController.h"

#include "ui/ConnectionDialog.h"

namespace smb::ui {

ConnectionManagementController::ConnectionManagementController(
    ConnectionsPanel &panel, smb::application::ConnectionService &service,
    ConnectionActionPrompter &prompter, QObject *parent)
    : QObject(parent), m_panel(panel), m_service(service),
      m_prompter(prompter) {
  connect(&m_panel, &ConnectionsPanel::addRequested, this,
          &ConnectionManagementController::addConnection);
  connect(&m_panel, &ConnectionsPanel::editRequested, this,
          &ConnectionManagementController::editConnection);
  connect(&m_panel, &ConnectionsPanel::deleteRequested, this,
          &ConnectionManagementController::deleteConnection);
}

void ConnectionManagementController::refreshConnections() {
  const auto listed = m_service.list();
  if (!listed.ok()) {
    m_prompter.showError(tr("Unable to Load Connections"), listed.error());
    return;
  }

  m_panel.setConnections(listed.value());
  emit connectionsRefreshed();
}

void ConnectionManagementController::addConnection() {
  ConnectionDialog dialog(&m_panel);
  dialog.setPasswordRequired(true);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const auto created =
      m_service.create(dialog.connection(), dialog.passwordSecret());
  if (!created.ok()) {
    m_prompter.showError(tr("Unable to Add Connection"), created.error());
    return;
  }

  refreshConnections();
  emit connectionAdded(created.value().id);
}

void ConnectionManagementController::editConnection(
    const QString &connectionId) {
  const auto connection = m_service.getById(connectionId);
  if (!connection.ok()) {
    m_prompter.showError(tr("Unable to Edit Connection"), connection.error());
    return;
  }

  ConnectionDialog dialog(&m_panel);
  dialog.setPasswordRequired(false);
  dialog.setConnection(connection.value());
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const auto updated =
      m_service.update(dialog.connection(), dialog.passwordSecret());
  if (!updated.ok()) {
    m_prompter.showError(tr("Unable to Edit Connection"), updated.error());
    return;
  }

  refreshConnections();
  emit connectionUpdated(updated.value().id);
}

void ConnectionManagementController::deleteConnection(
    const QString &connectionId) {
  const auto connection = m_service.getById(connectionId);
  if (!connection.ok()) {
    m_prompter.showError(tr("Unable to Delete Connection"), connection.error());
    return;
  }

  if (!m_prompter.confirmDeleteConnection(connection.value())) {
    return;
  }

  const auto removed = m_service.remove(connectionId);
  if (!removed.ok()) {
    m_prompter.showError(tr("Unable to Delete Connection"), removed.error());
    return;
  }
  if (!removed.value()) {
    m_prompter.showError(
        tr("Unable to Delete Connection"),
        smb::core::AppError::fromCode(smb::core::ErrorCode::FileNotFound,
                                      smb::core::ErrorCategory::Storage,
                                      tr("Connection was not found.")));
    return;
  }

  refreshConnections();
  emit connectionDeleted(connectionId);
}

} // namespace smb::ui
