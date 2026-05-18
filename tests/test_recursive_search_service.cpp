#include "application/RecursiveSearchService.h"

#include <QHash>
#include <QMutex>
#include <QtTest/QtTest>

namespace {

QString normalizePath(QString path) {
  path = path.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (path.isEmpty()) {
    return QStringLiteral("/");
  }
  if (!path.startsWith(QLatin1Char('/'))) {
    path.prepend(QLatin1Char('/'));
  }
  while (path.size() > 1 && path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  return path;
}

smb::core::Connection connection() {
  auto value = smb::core::Connection::createEmpty();
  value.id = QStringLiteral("conn-1");
  value.name = QStringLiteral("Engineering");
  value.normalizedUri = QStringLiteral("smb://server/share");
  return value;
}

smb::core::RemoteFileEntry directory(const QString &name, const QString &path) {
  smb::core::RemoteFileEntry entry;
  entry.name = name;
  entry.remotePath = path;
  entry.type = smb::core::RemoteFileType::Directory;
  return entry;
}

smb::core::RemoteFileEntry file(const QString &name, const QString &path) {
  smb::core::RemoteFileEntry entry;
  entry.name = name;
  entry.remotePath = path;
  entry.type = smb::core::RemoteFileType::File;
  return entry;
}

class FakeDirectoryUseCase final
    : public smb::application::RemoteDirectoryUseCase {
public:
  smb::core::Result<smb::application::OpenConnectionResult>
  listDirectory(const QString &, const QString &remotePath,
                const smb::core::OperationContext &context) override {
    QMutexLocker locker(&mutex);
    const auto path = normalizePath(remotePath);
    requestedPaths.push_back(path);

    if (context.cancellationToken != nullptr &&
        context.cancellationToken->isCancellationRequested()) {
      return smb::core::Result<smb::application::OpenConnectionResult>::failure(
          smb::core::AppError::fromCode(
              smb::core::ErrorCode::OperationCancelled,
              smb::core::ErrorCategory::General,
              QStringLiteral("cancelled")));
    }
    if (failures.contains(path)) {
      return smb::core::Result<smb::application::OpenConnectionResult>::failure(
          failures.value(path));
    }

    smb::application::OpenConnectionResult result;
    result.connection = connection();
    result.currentRemotePath = path;
    result.entries = directories.value(path);
    return smb::core::Result<smb::application::OpenConnectionResult>::success(
        result);
  }

  QMutex mutex;
  QHash<QString, QVector<smb::core::RemoteFileEntry>> directories;
  QHash<QString, smb::core::AppError> failures;
  QVector<QString> requestedPaths;
};

} // namespace

class RecursiveSearchServiceTest final : public QObject {
  Q_OBJECT

private slots:
  void findsMatchingFilesAndDirectoriesWithinDepthLimit() {
    FakeDirectoryUseCase useCase;
    useCase.directories.insert(
        QStringLiteral("/"),
        {directory(QStringLiteral("docs"), QStringLiteral("/docs")),
         file(QStringLiteral("readme.txt"), QStringLiteral("/readme.txt"))});
    useCase.directories.insert(
        QStringLiteral("/docs"),
        {file(QStringLiteral("budget.xlsx"), QStringLiteral("/docs/budget.xlsx")),
         directory(QStringLiteral("archive"), QStringLiteral("/docs/archive"))});
    useCase.directories.insert(
        QStringLiteral("/docs/archive"),
        {file(QStringLiteral("budget-old.xlsx"),
              QStringLiteral("/docs/archive/budget-old.xlsx"))});

    smb::application::RecursiveSearchService service(useCase);
    smb::application::RecursiveSearchOptions options;
    options.connectionId = QStringLiteral("conn-1");
    options.query = QStringLiteral("budget");
    options.maxDepth = 1;

    const auto result = service.search(options);
    QVERIFY(result.ok());
    QCOMPARE(result.value().entries.size(), 1);
    QCOMPARE(result.value().entries.at(0).remotePath,
             QStringLiteral("/docs/budget.xlsx"));
    QCOMPARE(result.value().scannedDirectories, 2);
    QVERIFY(!result.value().limitReached);
  }

  void respectsMaxResults() {
    FakeDirectoryUseCase useCase;
    useCase.directories.insert(
        QStringLiteral("/"),
        {file(QStringLiteral("a.txt"), QStringLiteral("/a.txt")),
         file(QStringLiteral("b.txt"), QStringLiteral("/b.txt"))});

    smb::application::RecursiveSearchService service(useCase);
    smb::application::RecursiveSearchOptions options;
    options.connectionId = QStringLiteral("conn-1");
    options.query = QStringLiteral(".txt");
    options.maxResults = 1;

    const auto result = service.search(options);
    QVERIFY(result.ok());
    QCOMPARE(result.value().entries.size(), 1);
    QVERIFY(result.value().limitReached);
  }

  void cancellationStopsSearch() {
    FakeDirectoryUseCase useCase;
    useCase.directories.insert(QStringLiteral("/"),
                               {directory(QStringLiteral("docs"),
                                          QStringLiteral("/docs"))});

    smb::application::RecursiveSearchService service(useCase);
    smb::application::RecursiveSearchOptions options;
    options.connectionId = QStringLiteral("conn-1");

    smb::core::CancellationToken token;
    token.cancel();
    smb::core::OperationContext context;
    context.cancellationToken = &token;

    const auto result = service.search(options, context);
    QVERIFY(!result.ok());
    QVERIFY(result.error().code == smb::core::ErrorCode::OperationCancelled);
  }

  void directoryFailureStopsWithTypedError() {
    FakeDirectoryUseCase useCase;
    useCase.directories.insert(
        QStringLiteral("/"),
        {directory(QStringLiteral("restricted"),
                   QStringLiteral("/restricted"))});
    useCase.failures.insert(
        QStringLiteral("/restricted"),
        smb::core::AppError::fromCode(smb::core::ErrorCode::PermissionDenied,
                                      smb::core::ErrorCategory::Smb,
                                      QStringLiteral("permission denied")));

    smb::application::RecursiveSearchService service(useCase);
    smb::application::RecursiveSearchOptions options;
    options.connectionId = QStringLiteral("conn-1");
    options.maxDepth = 1;

    const auto result = service.search(options);
    QVERIFY(!result.ok());
    QVERIFY(result.error().code == smb::core::ErrorCode::PermissionDenied);
  }
};

QTEST_MAIN(RecursiveSearchServiceTest)

#include "test_recursive_search_service.moc"
