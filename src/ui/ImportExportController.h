#pragma once

#include "application/ConnectionImportExportService.h"
#include "ui/ImportExportActionPrompter.h"

#include <QObject>

class QPushButton;

namespace smb::ui {

class ImportExportController final : public QObject {
  Q_OBJECT

public:
  ImportExportController(QPushButton &importButton, QPushButton &exportButton,
                         smb::application::ImportExportUseCase &useCase,
                         ImportExportActionPrompter &prompter,
                         QObject *parent = nullptr);

public slots:
  void importConnections();
  void exportConnections();

signals:
  void importCompleted(const smb::application::ImportResult &result);
  void exportCompleted(const QString &filePath);

private:
  smb::core::AppError localIoError(const QString &details) const;

  QPushButton &m_importButton;
  QPushButton &m_exportButton;
  smb::application::ImportExportUseCase &m_useCase;
  ImportExportActionPrompter &m_prompter;
};

} // namespace smb::ui

Q_DECLARE_METATYPE(smb::application::ImportResult)
