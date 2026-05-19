#include "ui/ConnectivityCheckController.h"

#include <QMetaObject>
#include <utility>

namespace smb::ui {

ConnectivityCheckController::ConnectivityCheckController(
    ConnectionsPanel &panel,
    smb::application::ConnectivityCheckUseCase &checkUseCase,
    smb::application::OperationQueue &operationQueue,
    ConnectionActionPrompter &prompter, QObject *parent)
    : QObject(parent), m_panel(panel), m_checkUseCase(checkUseCase),
      m_operationQueue(operationQueue), m_prompter(prompter) {
  qRegisterMetaType<smb::application::ConnectivityCheckResult>(
      "smb::application::ConnectivityCheckResult");
  qRegisterMetaType<smb::core::AppError>("smb::core::AppError");

  connect(&m_panel, &ConnectionsPanel::checkRequested, this,
          &ConnectivityCheckController::checkConnection);
}

void ConnectivityCheckController::checkConnection(
    const QString &connectionId) {
  if (connectionId.isEmpty()) {
    return;
  }

  const auto operationId = m_operationQueue.enqueue(
      tr("Check connection"),
      [this, connectionId](const smb::core::OperationContext &context) {
        auto checked = m_checkUseCase.check(connectionId, context);
        if (!checked.ok()) {
          deliverCheckFailed(connectionId, checked.error());
          return smb::core::Result<bool>::failure(checked.error());
        }

        auto result = std::move(checked.value());
        if (!result.available) {
          const auto error = result.error.hasError()
                                 ? result.error
                                 : smb::core::AppError::fromCode(
                                       smb::core::ErrorCode::Unknown,
                                       smb::core::ErrorCategory::Smb,
                                       tr("Connection is unavailable."));
          deliverCheckCompleted(connectionId, std::move(result));
          return smb::core::Result<bool>::failure(error);
        }

        deliverCheckCompleted(connectionId, std::move(result));
        return smb::core::Result<bool>::success(true);
      });

  emit checkStarted(connectionId, operationId);
}

void ConnectivityCheckController::deliverCheckCompleted(
    const QString &connectionId,
    smb::application::ConnectivityCheckResult result) {
  QMetaObject::invokeMethod(
      this,
      [this, connectionId, result = std::move(result)]() mutable {
        emit checkCompleted(connectionId, result);
      },
      Qt::QueuedConnection);
}

void ConnectivityCheckController::deliverCheckFailed(
    const QString &connectionId, const smb::core::AppError &error) {
  QMetaObject::invokeMethod(
      this,
      [this, connectionId, error]() {
        m_prompter.showError(tr("Unable to Check Connection"), error);
        emit checkFailed(connectionId, error);
      },
      Qt::QueuedConnection);
}

} // namespace smb::ui
