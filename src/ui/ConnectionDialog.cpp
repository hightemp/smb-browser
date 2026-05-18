#include "ui/ConnectionDialog.h"

#include "core/PathNormalizer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace smb::ui {

namespace {

int authTypeValue(smb::core::AuthType authType) {
  return static_cast<int>(authType);
}

smb::core::AuthType authTypeFromValue(int value) {
  switch (static_cast<smb::core::AuthType>(value)) {
  case smb::core::AuthType::Guest:
    return smb::core::AuthType::Guest;
  case smb::core::AuthType::Anonymous:
    return smb::core::AuthType::Anonymous;
  case smb::core::AuthType::CurrentUser:
    return smb::core::AuthType::CurrentUser;
  case smb::core::AuthType::Password:
  default:
    return smb::core::AuthType::Password;
  }
}

} // namespace

ConnectionDialog::ConnectionDialog(QWidget *parent) : QDialog(parent) {
  setObjectName(QStringLiteral("connectionDialog"));
  setWindowTitle(tr("Connection"));
  setModal(true);

  auto *layout = new QVBoxLayout(this);
  auto *form = new QFormLayout();
  form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

  m_nameEdit = new QLineEdit(this);
  m_nameEdit->setObjectName(QStringLiteral("connectionNameEdit"));
  form->addRow(tr("Name"), m_nameEdit);

  m_pathEdit = new QLineEdit(this);
  m_pathEdit->setObjectName(QStringLiteral("smbPathEdit"));
  m_pathEdit->setPlaceholderText(tr("smb://server/share"));
  form->addRow(tr("SMB path"), m_pathEdit);

  m_normalizedPreview = new QLabel(tr("Normalized URI: -"), this);
  m_normalizedPreview->setObjectName(
      QStringLiteral("normalizedUriPreviewLabel"));
  form->addRow(tr("Preview"), m_normalizedPreview);

  m_authTypeCombo = new QComboBox(this);
  m_authTypeCombo->setObjectName(QStringLiteral("authTypeCombo"));
  m_authTypeCombo->addItem(tr("Password"),
                           authTypeValue(smb::core::AuthType::Password));
  m_authTypeCombo->addItem(tr("Guest"),
                           authTypeValue(smb::core::AuthType::Guest));
  m_authTypeCombo->addItem(tr("Anonymous"),
                           authTypeValue(smb::core::AuthType::Anonymous));
  m_authTypeCombo->addItem(tr("Current user"),
                           authTypeValue(smb::core::AuthType::CurrentUser));
  form->addRow(tr("Authentication"), m_authTypeCombo);

  m_usernameEdit = new QLineEdit(this);
  m_usernameEdit->setObjectName(QStringLiteral("usernameEdit"));
  m_usernameEdit->setPlaceholderText(tr("DOMAIN\\user or user@domain"));
  form->addRow(tr("Username"), m_usernameEdit);

  m_domainEdit = new QLineEdit(this);
  m_domainEdit->setObjectName(QStringLiteral("domainEdit"));
  form->addRow(tr("Domain / workgroup"), m_domainEdit);

  m_passwordEdit = new QLineEdit(this);
  m_passwordEdit->setObjectName(QStringLiteral("passwordEdit"));
  m_passwordEdit->setEchoMode(QLineEdit::Password);
  form->addRow(tr("Password"), m_passwordEdit);

  m_commentEdit = new QLineEdit(this);
  m_commentEdit->setObjectName(QStringLiteral("commentEdit"));
  form->addRow(tr("Comment"), m_commentEdit);

  m_groupEdit = new QLineEdit(this);
  m_groupEdit->setObjectName(QStringLiteral("groupEdit"));
  form->addRow(tr("Group"), m_groupEdit);

  m_favoriteCheckBox = new QCheckBox(tr("Favorite"), this);
  m_favoriteCheckBox->setObjectName(QStringLiteral("favoriteCheckBox"));
  form->addRow(QString(), m_favoriteCheckBox);

  layout->addLayout(form);

  m_validationMessage = new QLabel(this);
  m_validationMessage->setObjectName(QStringLiteral("validationMessageLabel"));
  m_validationMessage->setWordWrap(true);
  layout->addWidget(m_validationMessage);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
  buttons->setObjectName(QStringLiteral("connectionDialogButtons"));
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, this,
          &ConnectionDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this,
          &ConnectionDialog::reject);
  connect(m_pathEdit, &QLineEdit::textChanged, this,
          &ConnectionDialog::updatePreview);
  connect(m_authTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
          this, &ConnectionDialog::updateAuthControls);

  updateAuthControls();
  updatePreview();
}

void ConnectionDialog::setConnection(const smb::core::Connection &connection) {
  m_connection = connection;
  m_nameEdit->setText(connection.name);
  m_pathEdit->setText(connection.inputPath.isEmpty() ? connection.normalizedUri
                                                     : connection.inputPath);
  m_usernameEdit->setText(connection.username);
  m_domainEdit->setText(connection.domain);
  m_commentEdit->setText(connection.comment);
  m_groupEdit->setText(connection.groupId);
  m_favoriteCheckBox->setChecked(connection.isFavorite);

  const auto comboIndex =
      m_authTypeCombo->findData(authTypeValue(connection.authType));
  m_authTypeCombo->setCurrentIndex(comboIndex < 0 ? 0 : comboIndex);
  updateAuthControls();
  updatePreview();
}

smb::core::Connection ConnectionDialog::connection() const {
  return m_connection;
}

std::optional<smb::core::CredentialSecret>
ConnectionDialog::passwordSecret() const {
  return m_secret;
}

void ConnectionDialog::setPasswordRequired(bool required) {
  m_passwordRequired = required;
}

void ConnectionDialog::accept() {
  setValidationMessage({});

  const auto normalized =
      smb::core::PathNormalizer::normalizeSmbPath(m_pathEdit->text());
  if (!normalized.ok()) {
    setValidationMessage(normalized.error().userMessage);
    return;
  }

  const auto authType = selectedAuthType();
  const auto identity = smb::core::PathNormalizer::normalizeIdentity(
      authType, m_usernameEdit->text(), m_domainEdit->text());
  if (!identity.ok()) {
    setValidationMessage(identity.error().userMessage);
    return;
  }

  const auto password = m_passwordEdit->text();
  if (authType == smb::core::AuthType::Password && m_passwordRequired &&
      password.isEmpty()) {
    setValidationMessage(tr("Password is required."));
    return;
  }

  m_connection.name = m_nameEdit->text().trimmed();
  m_connection.inputPath = normalized.value().inputPath;
  m_connection.normalizedUri = normalized.value().normalizedUri;
  m_connection.server = normalized.value().server;
  m_connection.share = normalized.value().share;
  m_connection.initialRemotePath = normalized.value().initialRemotePath;
  m_connection.authType = identity.value().authType;
  m_connection.domain = identity.value().domain;
  m_connection.username = identity.value().username;
  m_connection.comment = m_commentEdit->text().trimmed();
  m_connection.groupId = m_groupEdit->text().trimmed();
  m_connection.isFavorite = m_favoriteCheckBox->isChecked();

  if (authType == smb::core::AuthType::Password && !password.isEmpty()) {
    m_secret = smb::core::CredentialSecret{password.toUtf8()};
  } else {
    m_secret = std::nullopt;
  }

  QDialog::accept();
}

void ConnectionDialog::updateAuthControls() {
  const auto passwordAuth = selectedAuthType() == smb::core::AuthType::Password;
  m_usernameEdit->setEnabled(passwordAuth);
  m_domainEdit->setEnabled(passwordAuth);
  m_passwordEdit->setVisible(passwordAuth);
  m_passwordEdit->setEnabled(passwordAuth);
}

void ConnectionDialog::updatePreview() {
  const auto normalized =
      smb::core::PathNormalizer::normalizeSmbPath(m_pathEdit->text());
  if (normalized.ok()) {
    m_normalizedPreview->setText(
        tr("Normalized URI: %1").arg(normalized.value().normalizedUri));
  } else {
    m_normalizedPreview->setText(tr("Normalized URI: -"));
  }
}

smb::core::AuthType ConnectionDialog::selectedAuthType() const {
  return authTypeFromValue(m_authTypeCombo->currentData().toInt());
}

void ConnectionDialog::setValidationMessage(const QString &message) {
  m_validationMessage->setText(message);
  m_validationMessage->setVisible(!message.isEmpty());
}

} // namespace smb::ui
