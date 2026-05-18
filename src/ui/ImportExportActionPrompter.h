#pragma once

#include "application/ImportExportService.h"
#include "core/Error.h"

#include <QString>
#include <optional>

namespace smb::ui {

struct ExportFileRequest {
  QString filePath;
  bool includePlainTextPasswords = false;
  bool plainTextPasswordExportConfirmed = false;
};

struct ImportFileRequest {
  QString filePath;
  smb::application::DuplicatePolicy duplicatePolicy =
      smb::application::DuplicatePolicy::Skip;
};

class ImportExportActionPrompter {
public:
  virtual ~ImportExportActionPrompter() = default;

  virtual std::optional<ExportFileRequest> promptExportFile() = 0;
  virtual std::optional<ImportFileRequest> promptImportFile() = 0;
  virtual void showInfo(const QString &title, const QString &message) = 0;
  virtual void showError(const QString &title,
                         const smb::core::AppError &error) = 0;
};

} // namespace smb::ui
