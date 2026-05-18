#include "application/OperationQueue.h"

#include <QMetaObject>
#include <QRunnable>
#include <utility>

namespace smb::application {

struct OperationQueue::OperationData {
  QString id;
  QString name;
  OperationState state = OperationState::Queued;
  smb::core::TransferProgress progress;
  smb::core::AppError error = smb::core::AppError::none();
  std::shared_ptr<smb::core::CancellationToken> cancellationToken;
};

namespace {

smb::core::AppError cancelledError() {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::OperationCancelled,
                                       smb::core::ErrorCategory::General,
                                       QStringLiteral("Operation cancelled."),
                                       false);
}

class LambdaRunnable final : public QRunnable {
public:
  explicit LambdaRunnable(std::function<void()> function)
      : m_function(std::move(function)) {}

  void run() override {
    if (m_function) {
      m_function();
    }
  }

private:
  std::function<void()> m_function;
};

} // namespace

OperationQueue::OperationQueue(int maxConcurrentOperations, QObject *parent)
    : QObject(parent) {
  qRegisterMetaType<smb::application::OperationSnapshot>(
      "smb::application::OperationSnapshot");
  m_threadPool.setMaxThreadCount(qMax(1, maxConcurrentOperations));
}

OperationQueue::~OperationQueue() {
  {
    QMutexLocker locker(&m_mutex);
    for (const auto &operation : std::as_const(m_operations)) {
      operation->cancellationToken->cancel();
    }
  }
  m_threadPool.waitForDone();
}

QString OperationQueue::enqueue(QString name, QueuedOperation operation) {
  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  auto data = std::make_shared<OperationData>();
  data->id = id;
  data->name = std::move(name);
  data->cancellationToken = std::make_shared<smb::core::CancellationToken>();

  {
    QMutexLocker locker(&m_mutex);
    m_operations.insert(id, data);
  }
  postChanged(id);

  auto token = data->cancellationToken;
  m_threadPool.start(new LambdaRunnable(
      [this, id, token, operation = std::move(operation)]() mutable {
        if (!operation) {
          setState(id, OperationState::Failed,
                   smb::core::AppError::fromCode(
                       smb::core::ErrorCode::Unknown,
                       smb::core::ErrorCategory::General,
                       QStringLiteral("Operation callback is empty.")));
          return;
        }

        if (token->isCancellationRequested()) {
          setState(id, OperationState::Cancelled, cancelledError());
          return;
        }

        setState(id, OperationState::Running);

        smb::core::OperationContext context;
        context.cancellationToken = token.get();
        context.progressCallback =
            [this, id](const smb::core::TransferProgress &progress) {
              setProgress(id, progress);
            };

        const auto result = operation(context);
        if (token->isCancellationRequested() ||
            (!result.ok() &&
             result.error().code == smb::core::ErrorCode::OperationCancelled)) {
          setState(id, OperationState::Cancelled,
                   result.ok() ? cancelledError() : result.error());
        } else if (result.ok()) {
          setState(id, OperationState::Completed);
        } else {
          setState(id, OperationState::Failed, result.error());
        }
      }));
  return id;
}

bool OperationQueue::cancel(const QString &operationId) {
  std::shared_ptr<OperationData> data;
  {
    QMutexLocker locker(&m_mutex);
    data = m_operations.value(operationId);
    if (data == nullptr) {
      return false;
    }
    data->cancellationToken->cancel();
  }
  return true;
}

OperationSnapshot OperationQueue::snapshot(const QString &operationId) const {
  QMutexLocker locker(&m_mutex);
  const auto data = m_operations.value(operationId);
  if (data == nullptr) {
    return {};
  }
  return OperationSnapshot{data->id, data->name, data->state, data->progress,
                           data->error};
}

QVector<OperationSnapshot> OperationQueue::snapshots() const {
  QMutexLocker locker(&m_mutex);
  QVector<OperationSnapshot> result;
  result.reserve(m_operations.size());
  for (const auto &data : m_operations) {
    result.push_back(OperationSnapshot{data->id, data->name, data->state,
                                       data->progress, data->error});
  }
  return result;
}

void OperationQueue::waitForDone() { m_threadPool.waitForDone(); }

void OperationQueue::setState(const QString &operationId, OperationState state,
                              smb::core::AppError error) {
  {
    QMutexLocker locker(&m_mutex);
    const auto data = m_operations.value(operationId);
    if (data == nullptr) {
      return;
    }
    data->state = state;
    data->error = std::move(error);
  }
  postChanged(operationId);
}

void OperationQueue::setProgress(const QString &operationId,
                                 const smb::core::TransferProgress &progress) {
  {
    QMutexLocker locker(&m_mutex);
    const auto data = m_operations.value(operationId);
    if (data == nullptr) {
      return;
    }
    data->progress = progress;
  }
  postChanged(operationId);
}

void OperationQueue::postChanged(const QString &operationId) {
  const auto snapshotValue = snapshot(operationId);
  QMetaObject::invokeMethod(
      this, [this, snapshotValue]() { emit operationChanged(snapshotValue); },
      Qt::QueuedConnection);
}

QString toString(OperationState state) {
  switch (state) {
  case OperationState::Queued:
    return QStringLiteral("queued");
  case OperationState::Running:
    return QStringLiteral("running");
  case OperationState::Completed:
    return QStringLiteral("completed");
  case OperationState::Failed:
    return QStringLiteral("failed");
  case OperationState::Cancelled:
    return QStringLiteral("cancelled");
  }

  return QStringLiteral("queued");
}

} // namespace smb::application
