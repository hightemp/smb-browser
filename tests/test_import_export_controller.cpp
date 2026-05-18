#include "ui/ImportExportController.h"
#include "ui/MainWindow.h"

#include <QFile>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

class FakeImportExportUseCase final
    : public smb::application::ImportExportUseCase {
public:
  smb::application::ExportConnectionsRequest lastExportRequest;
  QByteArray exportBytes = QByteArrayLiteral("{\"connections\":[]}");
  bool exportShouldFail = false;
  QByteArray lastImportBytes;
  smb::application::DuplicatePolicy lastDuplicatePolicy =
      smb::application::DuplicatePolicy::Skip;

  smb::core::Result<QByteArray> exportConnections(
      const smb::application::ExportConnectionsRequest &request) const override {
    auto *self = const_cast<FakeImportExportUseCase *>(this);
    self->lastExportRequest = request;
    if (exportShouldFail) {
      return smb::core::Result<QByteArray>::failure(
          smb::core::AppError::fromCode(smb::core::ErrorCode::StorageError,
                                        smb::core::ErrorCategory::Storage,
                                        QStringLiteral("export failed")));
    }
    return smb::core::Result<QByteArray>::success(exportBytes);
  }

  smb::core::Result<smb::application::ImportResult>
  importConnections(const QByteArray &bytes,
                    smb::application::DuplicatePolicy duplicatePolicy) override {
    lastImportBytes = bytes;
    lastDuplicatePolicy = duplicatePolicy;
    smb::application::ImportResult result;
    result.connections.push_back(smb::core::Connection::createEmpty());
    return smb::core::Result<smb::application::ImportResult>::success(result);
  }
};

class FakeImportExportPrompter final
    : public smb::ui::ImportExportActionPrompter {
public:
  std::optional<smb::ui::ExportFileRequest> exportRequest;
  std::optional<smb::ui::ImportFileRequest> importRequest;
  QVector<QString> infos;
  QVector<QString> errors;

  std::optional<smb::ui::ExportFileRequest> promptExportFile() override {
    return exportRequest;
  }

  std::optional<smb::ui::ImportFileRequest> promptImportFile() override {
    return importRequest;
  }

  void showInfo(const QString &title, const QString &message) override {
    infos.push_back(title + QStringLiteral(":") + message);
  }

  void showError(const QString &title,
                 const smb::core::AppError &error) override {
    errors.push_back(title + QStringLiteral(":") +
                     smb::core::toString(error.code));
  }
};

} // namespace

class ImportExportControllerTest final : public QObject {
  Q_OBJECT

private slots:
  void exportButtonWritesSelectedFile() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QPushButton importButton;
    QPushButton exportButton;
    FakeImportExportUseCase useCase;
    FakeImportExportPrompter prompter;
    prompter.exportRequest = smb::ui::ExportFileRequest{
        tempDir.filePath(QStringLiteral("connections.json")), false, false};

    smb::ui::ImportExportController controller(importButton, exportButton,
                                               useCase, prompter);
    QSignalSpy exportedSpy(&controller,
                           &smb::ui::ImportExportController::exportCompleted);
    exportButton.click();

    QCOMPARE(exportedSpy.count(), 1);
    QFile file(prompter.exportRequest->filePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), useCase.exportBytes);
    QVERIFY(!useCase.lastExportRequest.includePlainTextPasswords);
    QVERIFY(!prompter.infos.isEmpty());
    QVERIFY(prompter.errors.isEmpty());
  }

  void dangerousExportRequestIsPassedToUseCase() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    QPushButton importButton;
    QPushButton exportButton;
    FakeImportExportUseCase useCase;
    useCase.exportBytes = QByteArrayLiteral("{\"plainTextPassword\":\"secret\"}");
    FakeImportExportPrompter prompter;
    prompter.exportRequest = smb::ui::ExportFileRequest{
        tempDir.filePath(QStringLiteral("danger.json")), true, true};

    smb::ui::ImportExportController controller(importButton, exportButton,
                                               useCase, prompter);
    exportButton.click();

    QVERIFY(useCase.lastExportRequest.includePlainTextPasswords);
    QVERIFY(useCase.lastExportRequest.plainTextPasswordExportConfirmed);
  }

  void importButtonReadsFileAndPassesDuplicatePolicy() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const auto path = tempDir.filePath(QStringLiteral("import.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"schema\":\"smb-browser.connections.export\"}");
    file.close();

    QPushButton importButton;
    QPushButton exportButton;
    FakeImportExportUseCase useCase;
    FakeImportExportPrompter prompter;
    prompter.importRequest = smb::ui::ImportFileRequest{
        path, smb::application::DuplicatePolicy::Replace};

    smb::ui::ImportExportController controller(importButton, exportButton,
                                               useCase, prompter);
    QSignalSpy importedSpy(&controller,
                           &smb::ui::ImportExportController::importCompleted);
    importButton.click();

    QCOMPARE(importedSpy.count(), 1);
    QCOMPARE(useCase.lastImportBytes,
             QByteArrayLiteral("{\"schema\":\"smb-browser.connections.export\"}"));
    QVERIFY(useCase.lastDuplicatePolicy ==
            smb::application::DuplicatePolicy::Replace);
  }

  void mainWindowCanAttachImportExportController() {
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    MainWindow window;
    FakeImportExportUseCase useCase;
    FakeImportExportPrompter prompter;
    prompter.exportRequest = smb::ui::ExportFileRequest{
        tempDir.filePath(QStringLiteral("from-window.json")), false, false};

    window.attachImportExport(useCase, prompter);
    auto *exportButton =
        window.findChild<QPushButton *>(QStringLiteral("exportButton"));
    QVERIFY(exportButton != nullptr);
    exportButton->click();

    QFile file(prompter.exportRequest->filePath);
    QVERIFY(file.exists());
  }
};

QTEST_MAIN(ImportExportControllerTest)

#include "test_import_export_controller.moc"
