#include "ui/StatusPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

namespace smb::ui {

namespace {

bool isTerminal(smb::application::OperationState state) {
  return state == smb::application::OperationState::Completed ||
         state == smb::application::OperationState::Failed ||
         state == smb::application::OperationState::Cancelled;
}

QString stateLabel(smb::application::OperationState state) {
  switch (state) {
  case smb::application::OperationState::Queued:
    return QObject::tr("Queued");
  case smb::application::OperationState::Running:
    return QObject::tr("Running");
  case smb::application::OperationState::Completed:
    return QObject::tr("Completed");
  case smb::application::OperationState::Failed:
    return QObject::tr("Failed");
  case smb::application::OperationState::Cancelled:
    return QObject::tr("Cancelled");
  }

  return QObject::tr("Queued");
}

} // namespace

StatusPanel::StatusPanel(QWidget *parent) : QFrame(parent) {
  setObjectName(QStringLiteral("statusPanel"));
  setFrameShape(QFrame::StyledPanel);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(8, 6, 8, 6);
  layout->setSpacing(8);

  m_connectionStatus = new QLabel(tr("Connection: Not connected"), this);
  m_connectionStatus->setObjectName(QStringLiteral("connectionStatusLabel"));
  layout->addWidget(m_connectionStatus);

  m_lastError = new QLabel(tr("Last error: None"), this);
  m_lastError->setObjectName(QStringLiteral("lastErrorLabel"));
  layout->addWidget(m_lastError, 1);

  m_activeOperation = new QLabel(tr("Operation: Idle"), this);
  m_activeOperation->setObjectName(QStringLiteral("activeOperationLabel"));
  layout->addWidget(m_activeOperation);

  m_progress = new QProgressBar(this);
  m_progress->setObjectName(QStringLiteral("operationProgressBar"));
  m_progress->setRange(0, 100);
  m_progress->setValue(0);
  m_progress->setFixedWidth(180);
  layout->addWidget(m_progress);

  m_cancelButton = new QPushButton(tr("Cancel"), this);
  m_cancelButton->setObjectName(QStringLiteral("cancelOperationButton"));
  m_cancelButton->setEnabled(false);
  layout->addWidget(m_cancelButton);

  connect(m_cancelButton, &QPushButton::clicked, this,
          &StatusPanel::cancelActiveOperation);
}

void StatusPanel::bindOperationQueue(
    smb::application::OperationQueue *operationQueue) {
  if (m_operationQueue != nullptr) {
    disconnect(m_operationQueue, nullptr, this, nullptr);
  }

  m_operationQueue = operationQueue;
  if (m_operationQueue == nullptr) {
    m_activeOperationId.clear();
    m_activeOperation->setText(tr("Operation: Idle"));
    m_cancelButton->setEnabled(false);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    return;
  }

  connect(m_operationQueue, &smb::application::OperationQueue::operationChanged,
          this, &StatusPanel::handleOperationChanged);
}

void StatusPanel::setConnectionStatus(const QString &status) {
  m_connectionStatus->setText(status.trimmed().isEmpty()
                                  ? tr("Connection: Not connected")
                                  : status.trimmed());
}

void StatusPanel::setLastError(const smb::core::AppError &error) {
  if (!error.hasError()) {
    clearLastError();
    return;
  }

  const auto message = error.userMessage.isEmpty()
                           ? smb::core::defaultUserMessage(error.code)
                           : error.userMessage;
  const auto details = m_sanitizer.sanitize(error.sanitizedTechnicalDetails);
  if (details.isEmpty()) {
    m_lastError->setText(tr("Last error: %1").arg(message));
  } else {
    m_lastError->setText(tr("Last error: %1 (%2)").arg(message, details));
  }
}

void StatusPanel::clearLastError() { m_lastError->setText(tr("Last error: None")); }

QString StatusPanel::connectionStatusText() const {
  return m_connectionStatus->text();
}

QString StatusPanel::lastErrorText() const { return m_lastError->text(); }

QString StatusPanel::activeOperationText() const {
  return m_activeOperation->text();
}

QString StatusPanel::activeOperationId() const { return m_activeOperationId; }

void StatusPanel::handleOperationChanged(
    const smb::application::OperationSnapshot &snapshot) {
  updateProgress(snapshot);

  m_activeOperation->setText(
      tr("Operation: %1 (%2)").arg(snapshot.name, stateLabel(snapshot.state)));

  if (isTerminal(snapshot.state)) {
    updateTerminalOperation(snapshot);
    return;
  }

  m_activeOperationId = snapshot.id;
  m_cancelButton->setEnabled(true);
}

void StatusPanel::cancelActiveOperation() {
  if (m_operationQueue == nullptr || m_activeOperationId.isEmpty()) {
    return;
  }

  const auto operationId = m_activeOperationId;
  emit cancelRequested(operationId);
  m_operationQueue->cancel(operationId);
}

void StatusPanel::updateProgress(
    const smb::application::OperationSnapshot &snapshot) {
  if (snapshot.progress.totalBytes <= 0) {
    m_progress->setRange(0, 0);
    return;
  }

  m_progress->setRange(0, 100);
  const auto value = static_cast<int>(
      qBound<qint64>(0, snapshot.progress.bytesTransferred * 100 /
                            snapshot.progress.totalBytes,
                     100));
  m_progress->setValue(value);
}

void StatusPanel::updateTerminalOperation(
    const smb::application::OperationSnapshot &snapshot) {
  if (snapshot.state == smb::application::OperationState::Completed) {
    m_progress->setRange(0, 100);
    m_progress->setValue(100);
  }
  if (snapshot.state == smb::application::OperationState::Failed ||
      snapshot.state == smb::application::OperationState::Cancelled) {
    setLastError(snapshot.error);
  }
  if (m_activeOperationId == snapshot.id ||
      snapshot.state == smb::application::OperationState::Completed) {
    m_activeOperationId.clear();
    m_cancelButton->setEnabled(false);
  }
}

} // namespace smb::ui
