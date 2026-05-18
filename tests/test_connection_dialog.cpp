#include "ui/ConnectionDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QtTest/QtTest>

class ConnectionDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void updatesNormalizedPreview() {
    smb::ui::ConnectionDialog dialog;
    auto *path = dialog.findChild<QLineEdit *>(QStringLiteral("smbPathEdit"));
    auto *preview =
        dialog.findChild<QLabel *>(QStringLiteral("normalizedUriPreviewLabel"));

    QVERIFY(path != nullptr);
    QVERIFY(preview != nullptr);

    path->setText(QStringLiteral("\\\\server\\share"));

    QVERIFY(preview->text().contains(QStringLiteral("smb://server/share")));
  }

  void acceptsPasswordConnection() {
    smb::ui::ConnectionDialog dialog;

    dialog.findChild<QLineEdit *>(QStringLiteral("connectionNameEdit"))
        ->setText(QStringLiteral("Finance"));
    dialog.findChild<QLineEdit *>(QStringLiteral("smbPathEdit"))
        ->setText(QStringLiteral("server/share/folder"));
    dialog.findChild<QLineEdit *>(QStringLiteral("usernameEdit"))
        ->setText(QStringLiteral("DOMAIN\\user"));
    dialog.findChild<QLineEdit *>(QStringLiteral("passwordEdit"))
        ->setText(QStringLiteral("synthetic-secret"));
    dialog.findChild<QLineEdit *>(QStringLiteral("groupEdit"))
        ->setText(QStringLiteral("Accounting"));
    dialog.findChild<QCheckBox *>(QStringLiteral("favoriteCheckBox"))
        ->setChecked(true);

    dialog.accept();

    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    const auto connection = dialog.connection();
    QCOMPARE(connection.name, QStringLiteral("Finance"));
    QCOMPARE(connection.normalizedUri, QStringLiteral("smb://server/share"));
    QCOMPARE(connection.initialRemotePath, QStringLiteral("folder"));
    QCOMPARE(connection.domain, QStringLiteral("DOMAIN"));
    QCOMPARE(connection.username, QStringLiteral("user"));
    QCOMPARE(connection.groupId, QStringLiteral("Accounting"));
    QVERIFY(connection.isFavorite);
    QVERIFY(dialog.passwordSecret().has_value());
    QCOMPARE(dialog.passwordSecret()->bytes,
             QByteArrayLiteral("synthetic-secret"));
  }

  void guestAuthHidesPasswordAndDoesNotRequireSecret() {
    smb::ui::ConnectionDialog dialog;
    auto *auth = dialog.findChild<QComboBox *>(QStringLiteral("authTypeCombo"));
    auto *password =
        dialog.findChild<QLineEdit *>(QStringLiteral("passwordEdit"));
    QVERIFY(auth != nullptr);
    QVERIFY(password != nullptr);

    auth->setCurrentIndex(1);
    QVERIFY(!password->isVisible());

    dialog.findChild<QLineEdit *>(QStringLiteral("connectionNameEdit"))
        ->setText(QStringLiteral("Guest Share"));
    dialog.findChild<QLineEdit *>(QStringLiteral("smbPathEdit"))
        ->setText(QStringLiteral("server/share"));

    dialog.accept();

    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(dialog.connection().authType == smb::core::AuthType::Guest);
    QVERIFY(!dialog.passwordSecret().has_value());
  }

  void rejectsMissingPasswordWhenRequired() {
    smb::ui::ConnectionDialog dialog;
    dialog.findChild<QLineEdit *>(QStringLiteral("connectionNameEdit"))
        ->setText(QStringLiteral("Finance"));
    dialog.findChild<QLineEdit *>(QStringLiteral("smbPathEdit"))
        ->setText(QStringLiteral("server/share"));
    dialog.findChild<QLineEdit *>(QStringLiteral("usernameEdit"))
        ->setText(QStringLiteral("user"));

    dialog.accept();

    QVERIFY(dialog.result() != int(QDialog::Accepted));
    QVERIFY(
        dialog.findChild<QLabel *>(QStringLiteral("validationMessageLabel"))
            ->text()
            .contains(QStringLiteral("Password")));
  }

  void rejectsConflictingDomain() {
    smb::ui::ConnectionDialog dialog;
    dialog.findChild<QLineEdit *>(QStringLiteral("connectionNameEdit"))
        ->setText(QStringLiteral("Finance"));
    dialog.findChild<QLineEdit *>(QStringLiteral("smbPathEdit"))
        ->setText(QStringLiteral("server/share"));
    dialog.findChild<QLineEdit *>(QStringLiteral("usernameEdit"))
        ->setText(QStringLiteral("DOMAIN\\user"));
    dialog.findChild<QLineEdit *>(QStringLiteral("domainEdit"))
        ->setText(QStringLiteral("OTHER"));
    dialog.findChild<QLineEdit *>(QStringLiteral("passwordEdit"))
        ->setText(QStringLiteral("synthetic-secret"));

    dialog.accept();

    QVERIFY(dialog.result() != int(QDialog::Accepted));
  }
};

QTEST_MAIN(ConnectionDialogTest)

#include "test_connection_dialog.moc"
