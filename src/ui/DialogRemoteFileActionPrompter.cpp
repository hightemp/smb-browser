#include "ui/DialogRemoteFileActionPrompter.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>

namespace smb::ui {

std::optional<QString> DialogRemoteFileActionPrompter::promptCreateFolderName(
    QWidget *parent, const QString &currentRemotePath) {
  bool accepted = false;
  const auto name = QInputDialog::getText(
      parent, QObject::tr("Create Folder"),
      QObject::tr("Folder name in %1").arg(currentRemotePath),
      QLineEdit::Normal, QString(), &accepted);
  if (!accepted) {
    return std::nullopt;
  }
  return name;
}

bool DialogRemoteFileActionPrompter::confirmDelete(
    QWidget *parent, const smb::core::RemoteFileEntry &entry) {
  return QMessageBox::question(parent, QObject::tr("Delete Remote Item"),
                               QObject::tr("Delete '%1'?").arg(entry.name),
                               QMessageBox::Yes | QMessageBox::Cancel,
                               QMessageBox::Cancel) == QMessageBox::Yes;
}

std::optional<QString> DialogRemoteFileActionPrompter::promptRename(
    QWidget *parent, const smb::core::RemoteFileEntry &entry) {
  bool accepted = false;
  const auto name = QInputDialog::getText(
      parent, QObject::tr("Rename Remote Item"), QObject::tr("New name"),
      QLineEdit::Normal, entry.name, &accepted);
  if (!accepted) {
    return std::nullopt;
  }
  return name;
}

std::optional<QString> DialogRemoteFileActionPrompter::promptDownloadPath(
    QWidget *parent, const smb::core::RemoteFileEntry &entry) {
  const auto path = QFileDialog::getSaveFileName(
      parent, QObject::tr("Download File"), entry.name);
  if (path.isEmpty()) {
    return std::nullopt;
  }
  return path;
}

std::optional<QString> DialogRemoteFileActionPrompter::promptUploadPath(
    QWidget *parent, const QString &currentRemotePath) {
  const auto path =
      QFileDialog::getOpenFileName(parent, QObject::tr("Upload File"),
                                   QFileInfo(currentRemotePath).absolutePath());
  if (path.isEmpty()) {
    return std::nullopt;
  }
  return path;
}

void DialogRemoteFileActionPrompter::showError(
    QWidget *parent, const QString &title, const smb::core::AppError &error) {
  const auto details = error.sanitizedTechnicalDetails.isEmpty()
                           ? error.userMessage
                           : error.sanitizedTechnicalDetails;
  QMessageBox::critical(parent, title, details);
}

} // namespace smb::ui
