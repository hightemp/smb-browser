#pragma once

#include "ui/ImportExportActionPrompter.h"

#include <QWidget>

namespace smb::ui {

class DialogImportExportActionPrompter final
    : public ImportExportActionPrompter {
public:
  explicit DialogImportExportActionPrompter(QWidget *parent = nullptr);

  std::optional<ExportFileRequest> promptExportFile() override;
  std::optional<ImportFileRequest> promptImportFile() override;
  void showInfo(const QString &title, const QString &message) override;
  void showError(const QString &title,
                 const smb::core::AppError &error) override;

private:
  QWidget *m_parent = nullptr;
};

} // namespace smb::ui
