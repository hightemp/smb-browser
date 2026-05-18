#pragma once

#include "core/Error.h"
#include "core/RemoteFileEntry.h"

#include <QString>
#include <optional>

class QWidget;

namespace smb::ui {

class RemoteFileActionPrompter {
public:
  virtual ~RemoteFileActionPrompter() = default;

  virtual std::optional<QString>
  promptCreateFolderName(QWidget *parent, const QString &currentRemotePath) = 0;
  virtual bool confirmDelete(QWidget *parent,
                             const smb::core::RemoteFileEntry &entry) = 0;
  virtual std::optional<QString>
  promptRename(QWidget *parent, const smb::core::RemoteFileEntry &entry) = 0;
  virtual std::optional<QString>
  promptDownloadPath(QWidget *parent,
                     const smb::core::RemoteFileEntry &entry) = 0;
  virtual std::optional<QString>
  promptUploadPath(QWidget *parent, const QString &currentRemotePath) = 0;
  virtual void showError(QWidget *parent, const QString &title,
                         const smb::core::AppError &error) = 0;
};

} // namespace smb::ui
