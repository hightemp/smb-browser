#include "ui/MessageBoxConnectionActionPrompter.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QWidget>

namespace smb::ui {

namespace {

QString errorText(const smb::core::AppError &error) {
  if (!error.userMessage.isEmpty()) {
    return error.userMessage;
  }
  if (!error.sanitizedTechnicalDetails.isEmpty()) {
    return error.sanitizedTechnicalDetails;
  }
  return QCoreApplication::translate("ConnectionActionPrompter",
                                     "Operation failed.");
}

} // namespace

MessageBoxConnectionActionPrompter::MessageBoxConnectionActionPrompter(
    QWidget *parent)
    : m_parent(parent) {}

bool MessageBoxConnectionActionPrompter::confirmDeleteConnection(
    const smb::core::Connection &connection) {
  const auto name =
      connection.name.isEmpty() ? connection.normalizedUri : connection.name;
  const auto text = QCoreApplication::translate(
                        "ConnectionActionPrompter",
                        "Delete connection \"%1\"? The saved credential will "
                        "also be removed if it is not shared.")
                        .arg(name);
  return QMessageBox::question(
             m_parent,
             QCoreApplication::translate("ConnectionActionPrompter",
                                         "Delete Connection"),
             text, QMessageBox::Yes | QMessageBox::No,
             QMessageBox::No) == QMessageBox::Yes;
}

void MessageBoxConnectionActionPrompter::showError(
    const QString &title, const smb::core::AppError &error) {
  QMessageBox::warning(m_parent, title, errorText(error));
}

} // namespace smb::ui
