#pragma once

#include "core/SmbClient.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include <QUuid>
#include <functional>
#include <memory>

namespace smb::application {

enum class OperationState {
  Queued,
  Running,
  Completed,
  Failed,
  Cancelled,
};

struct OperationSnapshot {
  QString id;
  QString name;
  OperationState state = OperationState::Queued;
  smb::core::TransferProgress progress;
  smb::core::AppError error = smb::core::AppError::none();
};

using QueuedOperation =
    std::function<smb::core::Result<bool>(const smb::core::OperationContext &)>;

class OperationQueue final : public QObject {
  Q_OBJECT

public:
  explicit OperationQueue(int maxConcurrentOperations = 1,
                          QObject *parent = nullptr);
  ~OperationQueue() override;

  QString enqueue(QString name, QueuedOperation operation);
  bool cancel(const QString &operationId);
  OperationSnapshot snapshot(const QString &operationId) const;
  QVector<OperationSnapshot> snapshots() const;
  void waitForDone();

signals:
  void operationChanged(const smb::application::OperationSnapshot &snapshot);

private:
  struct OperationData;

  void setState(const QString &operationId, OperationState state,
                smb::core::AppError error = smb::core::AppError::none());
  void setProgress(const QString &operationId,
                   const smb::core::TransferProgress &progress);
  void postChanged(const QString &operationId);

  mutable QMutex m_mutex;
  QHash<QString, std::shared_ptr<OperationData>> m_operations;
  QThreadPool m_threadPool;
};

QString toString(OperationState state);

} // namespace smb::application

Q_DECLARE_METATYPE(smb::application::OperationSnapshot)
