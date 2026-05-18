#pragma once

#include "application/OperationQueue.h"
#include "core/Error.h"
#include "core/LogSanitizer.h"

#include <QFrame>

class QLabel;
class QProgressBar;
class QPushButton;

namespace smb::ui {

class StatusPanel final : public QFrame {
  Q_OBJECT

public:
  explicit StatusPanel(QWidget *parent = nullptr);

  void bindOperationQueue(smb::application::OperationQueue *operationQueue);
  void setConnectionStatus(const QString &status);
  void setLastError(const smb::core::AppError &error);
  void clearLastError();

  QString connectionStatusText() const;
  QString lastErrorText() const;
  QString activeOperationText() const;
  QString activeOperationId() const;

signals:
  void cancelRequested(const QString &operationId);

private:
  void handleOperationChanged(
      const smb::application::OperationSnapshot &snapshot);
  void cancelActiveOperation();
  void updateProgress(const smb::application::OperationSnapshot &snapshot);
  void updateTerminalOperation(
      const smb::application::OperationSnapshot &snapshot);

  smb::application::OperationQueue *m_operationQueue = nullptr;
  smb::core::LogSanitizer m_sanitizer;
  QLabel *m_connectionStatus = nullptr;
  QLabel *m_lastError = nullptr;
  QLabel *m_activeOperation = nullptr;
  QProgressBar *m_progress = nullptr;
  QPushButton *m_cancelButton = nullptr;
  QString m_activeOperationId;
};

} // namespace smb::ui
