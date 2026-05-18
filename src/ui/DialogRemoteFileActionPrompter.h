#pragma once

#include "ui/RemoteFileActionPrompter.h"

namespace smb::ui {

class DialogRemoteFileActionPrompter final : public RemoteFileActionPrompter {
public:
  std::optional<QString>
  promptCreateFolderName(QWidget *parent,
                         const QString &currentRemotePath) override;
  bool confirmDelete(QWidget *parent,
                     const smb::core::RemoteFileEntry &entry) override;
  std::optional<QString>
  promptRename(QWidget *parent,
               const smb::core::RemoteFileEntry &entry) override;
  std::optional<QString>
  promptDownloadPath(QWidget *parent,
                     const smb::core::RemoteFileEntry &entry) override;
  std::optional<QString>
  promptUploadPath(QWidget *parent, const QString &currentRemotePath) override;
  void showError(QWidget *parent, const QString &title,
                 const smb::core::AppError &error) override;
};

} // namespace smb::ui
