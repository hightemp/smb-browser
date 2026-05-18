#include "ui/DialogImportExportActionPrompter.h"

#include <QAbstractButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>

namespace smb::ui {

DialogImportExportActionPrompter::DialogImportExportActionPrompter(
    QWidget *parent)
    : m_parent(parent) {}

std::optional<ExportFileRequest>
DialogImportExportActionPrompter::promptExportFile() {
  const auto filePath = QFileDialog::getSaveFileName(
      m_parent, QObject::tr("Export Connections"), QString(),
      QObject::tr("SMB Browser export (*.json);;JSON files (*.json)"));
  if (filePath.isEmpty()) {
    return std::nullopt;
  }

  QMessageBox modeBox(m_parent);
  modeBox.setWindowTitle(QObject::tr("Export Connections"));
  modeBox.setText(QObject::tr("Export connections without passwords by "
                              "default. Plain-text password export is "
                              "dangerous and should only be used manually."));
  auto *safeButton =
      modeBox.addButton(QObject::tr("Export Without Passwords"),
                        QMessageBox::AcceptRole);
  auto *dangerButton =
      modeBox.addButton(QObject::tr("Export Plain-Text Passwords"),
                        QMessageBox::DestructiveRole);
  modeBox.addButton(QMessageBox::Cancel);
  modeBox.setDefaultButton(safeButton);
  modeBox.exec();

  if (modeBox.clickedButton() == nullptr ||
      modeBox.standardButton(modeBox.clickedButton()) == QMessageBox::Cancel) {
    return std::nullopt;
  }

  ExportFileRequest request;
  request.filePath = filePath;
  if (modeBox.clickedButton() != static_cast<QAbstractButton *>(dangerButton)) {
    return request;
  }

  const auto confirmation = QMessageBox::warning(
      m_parent, QObject::tr("Plain-Text Password Export"),
      QObject::tr("This export will write saved passwords into the selected "
                  "file without encryption. Anyone with access to the file can "
                  "read them. Continue only if you will protect or delete the "
                  "file yourself."),
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
  if (confirmation != QMessageBox::Yes) {
    return std::nullopt;
  }

  request.includePlainTextPasswords = true;
  request.plainTextPasswordExportConfirmed = true;
  return request;
}

std::optional<ImportFileRequest>
DialogImportExportActionPrompter::promptImportFile() {
  const auto filePath = QFileDialog::getOpenFileName(
      m_parent, QObject::tr("Import Connections"), QString(),
      QObject::tr("SMB Browser export (*.json);;JSON files (*.json)"));
  if (filePath.isEmpty()) {
    return std::nullopt;
  }

  QMessageBox duplicateBox(m_parent);
  duplicateBox.setWindowTitle(QObject::tr("Import Connections"));
  duplicateBox.setText(QObject::tr("Choose how to handle duplicate connection "
                                   "IDs found in the import file."));
  auto *skipButton =
      duplicateBox.addButton(QObject::tr("Skip Duplicates"),
                             QMessageBox::AcceptRole);
  auto *replaceButton =
      duplicateBox.addButton(QObject::tr("Replace Duplicates"),
                             QMessageBox::DestructiveRole);
  auto *copyButton =
      duplicateBox.addButton(QObject::tr("Create Copies"),
                             QMessageBox::ActionRole);
  duplicateBox.addButton(QMessageBox::Cancel);
  duplicateBox.setDefaultButton(skipButton);
  duplicateBox.exec();

  if (duplicateBox.clickedButton() == nullptr ||
      duplicateBox.standardButton(duplicateBox.clickedButton()) ==
          QMessageBox::Cancel) {
    return std::nullopt;
  }

  ImportFileRequest request;
  request.filePath = filePath;
  if (duplicateBox.clickedButton() ==
      static_cast<QAbstractButton *>(replaceButton)) {
    request.duplicatePolicy = smb::application::DuplicatePolicy::Replace;
  } else if (duplicateBox.clickedButton() ==
             static_cast<QAbstractButton *>(copyButton)) {
    request.duplicatePolicy = smb::application::DuplicatePolicy::CreateCopy;
  }
  return request;
}

void DialogImportExportActionPrompter::showInfo(const QString &title,
                                                const QString &message) {
  QMessageBox::information(m_parent, title, message);
}

void DialogImportExportActionPrompter::showError(
    const QString &title, const smb::core::AppError &error) {
  QMessageBox::warning(m_parent, title, error.userMessage);
}

} // namespace smb::ui
