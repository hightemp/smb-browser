#include "ui/ImportExportController.h"

#include <QFile>
#include <QPushButton>
#include <QSaveFile>

namespace smb::ui {

ImportExportController::ImportExportController(
    QPushButton &importButton, QPushButton &exportButton,
    smb::application::ImportExportUseCase &useCase,
    ImportExportActionPrompter &prompter, QObject *parent)
    : QObject(parent), m_importButton(importButton), m_exportButton(exportButton),
      m_useCase(useCase), m_prompter(prompter) {
  qRegisterMetaType<smb::application::ImportResult>(
      "smb::application::ImportResult");
  connect(&m_importButton, &QPushButton::clicked, this,
          &ImportExportController::importConnections);
  connect(&m_exportButton, &QPushButton::clicked, this,
          &ImportExportController::exportConnections);
}

void ImportExportController::importConnections() {
  const auto request = m_prompter.promptImportFile();
  if (!request.has_value()) {
    return;
  }

  QFile file(request->filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    m_prompter.showError(tr("Unable to Import Connections"),
                         localIoError(file.errorString()));
    return;
  }

  const auto imported =
      m_useCase.importConnections(file.readAll(), request->duplicatePolicy);
  if (!imported.ok()) {
    m_prompter.showError(tr("Unable to Import Connections"), imported.error());
    return;
  }

  m_prompter.showInfo(
      tr("Import Complete"),
      tr("Imported %1 connection(s). Skipped %2 duplicate(s). %3 record error(s).")
          .arg(imported.value().connections.size())
          .arg(imported.value().skippedDuplicates)
          .arg(imported.value().errors.size()));
  emit importCompleted(imported.value());
}

void ImportExportController::exportConnections() {
  const auto request = m_prompter.promptExportFile();
  if (!request.has_value()) {
    return;
  }

  smb::application::ExportConnectionsRequest exportRequest;
  exportRequest.includePlainTextPasswords = request->includePlainTextPasswords;
  exportRequest.plainTextPasswordExportConfirmed =
      request->plainTextPasswordExportConfirmed;

  const auto exported = m_useCase.exportConnections(exportRequest);
  if (!exported.ok()) {
    m_prompter.showError(tr("Unable to Export Connections"), exported.error());
    return;
  }

  QSaveFile file(request->filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    m_prompter.showError(tr("Unable to Export Connections"),
                         localIoError(file.errorString()));
    return;
  }
  if (file.write(exported.value()) != exported.value().size()) {
    m_prompter.showError(tr("Unable to Export Connections"),
                         localIoError(file.errorString()));
    return;
  }
  if (!file.commit()) {
    m_prompter.showError(tr("Unable to Export Connections"),
                         localIoError(file.errorString()));
    return;
  }

  m_prompter.showInfo(tr("Export Complete"),
                      tr("Connections exported to %1.").arg(request->filePath));
  emit exportCompleted(request->filePath);
}

smb::core::AppError
ImportExportController::localIoError(const QString &details) const {
  return smb::core::AppError::fromCode(smb::core::ErrorCode::LocalIoError,
                                      smb::core::ErrorCategory::Transfer,
                                      details, false);
}

} // namespace smb::ui
