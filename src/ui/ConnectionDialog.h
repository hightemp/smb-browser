#pragma once

#include "core/Connection.h"
#include "core/CredentialStore.h"

#include <QDialog>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

namespace smb::ui {

class ConnectionDialog final : public QDialog {
  Q_OBJECT

public:
  explicit ConnectionDialog(QWidget *parent = nullptr);

  void setConnection(const smb::core::Connection &connection);
  smb::core::Connection connection() const;
  std::optional<smb::core::CredentialSecret> passwordSecret() const;
  void setPasswordRequired(bool required);

public slots:
  void accept() override;

private:
  void updateAuthControls();
  void updatePreview();
  smb::core::AuthType selectedAuthType() const;
  void setValidationMessage(const QString &message);

  smb::core::Connection m_connection;
  std::optional<smb::core::CredentialSecret> m_secret;
  bool m_passwordRequired = true;

  QLineEdit *m_nameEdit = nullptr;
  QLineEdit *m_pathEdit = nullptr;
  QComboBox *m_authTypeCombo = nullptr;
  QLineEdit *m_usernameEdit = nullptr;
  QLineEdit *m_domainEdit = nullptr;
  QLineEdit *m_passwordEdit = nullptr;
  QLineEdit *m_commentEdit = nullptr;
  QLineEdit *m_groupEdit = nullptr;
  QCheckBox *m_favoriteCheckBox = nullptr;
  QLabel *m_normalizedPreview = nullptr;
  QLabel *m_validationMessage = nullptr;
};

} // namespace smb::ui
