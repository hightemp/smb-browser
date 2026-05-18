#pragma once

#include "ui/ConnectionActionPrompter.h"

class QWidget;

namespace smb::ui {

class MessageBoxConnectionActionPrompter final
    : public ConnectionActionPrompter {
public:
  explicit MessageBoxConnectionActionPrompter(QWidget *parent = nullptr);

  bool
  confirmDeleteConnection(const smb::core::Connection &connection) override;
  void showError(const QString &title,
                 const smb::core::AppError &error) override;

private:
  QWidget *m_parent = nullptr;
};

} // namespace smb::ui
