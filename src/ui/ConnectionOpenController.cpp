#include "ui/ConnectionOpenController.h"

#include <QMetaObject>
#include <utility>

namespace smb::ui {

ConnectionOpenController::ConnectionOpenController(
    ConnectionsPanel &panel,
    smb::application::ConnectionOpenUseCase &openUseCase,
    smb::application::OperationQueue &operationQueue,
    ConnectionActionPrompter &prompter, QObject *parent)
    : QObject(parent), m_panel(panel), m_openUseCase(openUseCase),
      m_operationQueue(operationQueue), m_prompter(prompter) {
  qRegisterMetaType<smb::application::OpenConnectionResult>(
      "smb::application::OpenConnectionResult");
  qRegisterMetaType<smb::core::AppError>("smb::core::AppError");

  connect(&m_panel, &ConnectionsPanel::connectRequested, this,
          &ConnectionOpenController::openConnection);
}

void ConnectionOpenController::openConnection(const QString &connectionId) {
  if (connectionId.isEmpty()) {
    return;
  }

  const auto operationId = m_operationQueue.enqueue(
      tr("Open connection"),
      [this, connectionId](const smb::core::OperationContext &context) {
        auto opened = m_openUseCase.open(connectionId, context);
        if (!opened.ok()) {
          deliverOpenFailure(connectionId, opened.error());
          return smb::core::Result<bool>::failure(opened.error());
        }

        deliverOpenSuccess(std::move(opened.value()));
        return smb::core::Result<bool>::success(true);
      });

  emit openStarted(connectionId, operationId);
}

void ConnectionOpenController::deliverOpenFailure(
    const QString &connectionId, const smb::core::AppError &error) {
  QMetaObject::invokeMethod(
      this,
      [this, connectionId, error]() {
        m_prompter.showError(tr("Unable to Open Connection"), error);
        emit connectionOpenFailed(connectionId, error);
      },
      Qt::QueuedConnection);
}

void ConnectionOpenController::deliverOpenSuccess(
    smb::application::OpenConnectionResult result) {
  QMetaObject::invokeMethod(
      this,
      [this, result = std::move(result)]() mutable {
        emit connectionOpened(result);
      },
      Qt::QueuedConnection);
}

} // namespace smb::ui
