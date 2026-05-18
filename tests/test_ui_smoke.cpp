#include "storage/SqliteStorage.h"
#include "ui/ConnectionActionPrompter.h"
#include "ui/ConnectionDialog.h"
#include "ui/ConnectionManagementController.h"
#include "ui/ConnectionsPanel.h"
#include "ui/LocalizationManager.h"
#include "ui/LogViewer.h"
#include "ui/MainWindow.h"
#include "ui/RemoteFileModel.h"
#include "ui/SettingsDialog.h"

#include <QComboBox>
#include <QFileInfo>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTableView>
#include <QtTest/QtTest>
#include <memory>

namespace {

class FakeCredentialStore final : public smb::core::CredentialStore {
public:
  smb::core::Result<QString>
  save(const QString &ownerId,
       const smb::core::CredentialSecret &secret) override {
    const auto ref = QStringLiteral("fake:%1").arg(ownerId);
    values.insert(ref, secret.bytes);
    return smb::core::Result<QString>::success(ref);
  }

  smb::core::Result<smb::core::CredentialSecret>
  load(const QString &credentialRef) const override {
    if (!values.contains(credentialRef)) {
      return smb::core::Result<smb::core::CredentialSecret>::failure(
          smb::core::AppError::fromCode(
              smb::core::ErrorCode::CredentialNotFound,
              smb::core::ErrorCategory::Credentials,
              QStringLiteral("missing credential")));
    }
    return smb::core::Result<smb::core::CredentialSecret>::success(
        smb::core::CredentialSecret{values.value(credentialRef)});
  }

  smb::core::Result<bool>
  update(const QString &credentialRef,
         const smb::core::CredentialSecret &secret) override {
    values.insert(credentialRef, secret.bytes);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> remove(const QString &credentialRef) override {
    values.remove(credentialRef);
    return smb::core::Result<bool>::success(true);
  }

  smb::core::Result<bool> isAvailable() const override {
    return smb::core::Result<bool>::success(true);
  }

  QHash<QString, QByteArray> values;
};

class FakeConnectionPrompter final : public smb::ui::ConnectionActionPrompter {
public:
  bool
  confirmDeleteConnection(const smb::core::Connection &connection) override {
    confirmedDeletes.push_back(connection.id);
    return true;
  }

  void showError(const QString &title,
                 const smb::core::AppError &error) override {
    errors.push_back(title + QStringLiteral(":") +
                     smb::core::toString(error.code));
  }

  QVector<QString> confirmedDeletes;
  QVector<QString> errors;
};

struct Fixture {
  QTemporaryDir tempDir;
  smb::infrastructure::SqliteStorage storage;
  std::unique_ptr<smb::infrastructure::ConnectionRepository> repository;
  std::unique_ptr<FakeCredentialStore> credentialStore;
  std::unique_ptr<smb::application::ConnectionService> service;
};

std::unique_ptr<Fixture> createFixture() {
  auto fixture = std::make_unique<Fixture>();
  if (!fixture->tempDir.isValid()) {
    return fixture;
  }
  const auto opened = fixture->storage.open(
      fixture->tempDir.filePath(QStringLiteral("app.db")));
  if (opened.hasError()) {
    return fixture;
  }
  const auto migrated = fixture->storage.migrate();
  if (migrated.hasError()) {
    return fixture;
  }

  fixture->repository =
      std::make_unique<smb::infrastructure::ConnectionRepository>(
          fixture->storage.database());
  fixture->credentialStore = std::make_unique<FakeCredentialStore>();
  fixture->service = std::make_unique<smb::application::ConnectionService>(
      *fixture->repository, *fixture->credentialStore);
  return fixture;
}

void fillConnectionDialog(smb::ui::ConnectionDialog &dialog,
                          const QString &name) {
  dialog.findChild<QLineEdit *>(QStringLiteral("connectionNameEdit"))
      ->setText(name);
  dialog.findChild<QLineEdit *>(QStringLiteral("smbPathEdit"))
      ->setText(QStringLiteral("server/share"));
  dialog.findChild<QLineEdit *>(QStringLiteral("usernameEdit"))
      ->setText(QStringLiteral("DOMAIN\\user"));
  dialog.findChild<QLineEdit *>(QStringLiteral("passwordEdit"))
      ->setText(QStringLiteral("synthetic-secret"));
}

void selectFirstConnection(smb::ui::ConnectionsPanel &panel) {
  auto *list = panel.findChild<QListView *>(QStringLiteral("connectionsList"));
  QVERIFY(list != nullptr);
  list->selectionModel()->select(list->model()->index(0, 0),
                                 QItemSelectionModel::ClearAndSelect |
                                     QItemSelectionModel::Rows);
}

} // namespace

class UiSmokeTest final : public QObject {
  Q_OBJECT

private slots:
  void mainWindowOpensSettingsAndLogViewer() {
    MainWindow window;

    auto *settingsButton =
        window.findChild<QPushButton *>(QStringLiteral("settingsButton"));
    auto *logsButton =
        window.findChild<QPushButton *>(QStringLiteral("logsButton"));
    QVERIFY(settingsButton != nullptr);
    QVERIFY(logsButton != nullptr);

    settingsButton->click();
    QTRY_VERIFY(window.findChild<smb::ui::SettingsDialog *>() != nullptr);

    logsButton->click();
    QTRY_VERIFY(window.findChild<smb::ui::LogViewer *>() != nullptr);
  }

  void addEditDeleteConnectionFlow() {
    auto fixture = createFixture();
    QVERIFY(fixture->service != nullptr);

    smb::ui::ConnectionDialog addDialog;
    fillConnectionDialog(addDialog, QStringLiteral("Engineering"));
    addDialog.accept();
    QCOMPARE(addDialog.result(), int(QDialog::Accepted));

    const auto created = fixture->service->create(
        addDialog.connection(), addDialog.passwordSecret());
    QVERIFY(created.ok());

    smb::ui::ConnectionDialog editDialog;
    editDialog.setPasswordRequired(false);
    editDialog.setConnection(created.value());
    editDialog.findChild<QLineEdit *>(QStringLiteral("connectionNameEdit"))
        ->setText(QStringLiteral("Engineering Edited"));
    editDialog.accept();
    QCOMPARE(editDialog.result(), int(QDialog::Accepted));

    const auto updated = fixture->service->update(
        editDialog.connection(), editDialog.passwordSecret());
    QVERIFY(updated.ok());
    QCOMPARE(updated.value().name, QStringLiteral("Engineering Edited"));

    smb::ui::ConnectionsPanel panel;
    FakeConnectionPrompter prompter;
    smb::ui::ConnectionManagementController controller(panel, *fixture->service,
                                                       prompter);
    controller.refreshConnections();
    selectFirstConnection(panel);
    panel
        .findChild<QPushButton *>(QStringLiteral("panelDeleteConnectionButton"))
        ->click();

    const auto listed = fixture->service->list();
    QVERIFY(listed.ok());
    QVERIFY(listed.value().isEmpty());
    QVERIFY(!prompter.confirmedDeletes.isEmpty());
    QVERIFY(prompter.errors.isEmpty());
  }

  void browserModelDisplaysRemoteEntries() {
    smb::ui::RemoteFileModel model;
    smb::core::RemoteFileEntry entry;
    entry.name = QStringLiteral("report.txt");
    entry.remotePath = QStringLiteral("/report.txt");
    entry.type = smb::core::RemoteFileType::File;
    entry.size = 42;
    model.setEntries({entry});

    QTableView table;
    table.setModel(&model);

    QCOMPARE(table.model()->rowCount(), 1);
    QCOMPARE(table.model()->index(0, 0).data().toString(),
             QStringLiteral("report.txt"));
  }

  void languageCanSwitchBetweenEnglishAndRussian() {
#ifndef SMB_BROWSER_TRANSLATION_DIR
    QSKIP("No compiled translation directory configured.");
#else
    const auto translationPath =
        QStringLiteral(SMB_BROWSER_TRANSLATION_DIR "/smb-browser_ru.qm");
    if (!QFileInfo::exists(translationPath)) {
      QSKIP("Compiled Russian translation is not available.");
    }

    auto *application = QCoreApplication::instance();
    QVERIFY(application != nullptr);

    smb::ui::LocalizationManager manager(
        {QStringLiteral(SMB_BROWSER_TRANSLATION_DIR)});
    manager.setLanguageMode(smb::core::LanguageMode::Russian);
    const auto russian = manager.apply(*application);
    QVERIFY(russian.translatorInstalled);
    QCOMPARE(QCoreApplication::translate("MainWindow", "Ready"),
             QStringLiteral("Готово"));

    manager.setLanguageMode(smb::core::LanguageMode::English);
    const auto english = manager.apply(*application);
    QVERIFY(!english.translatorInstalled);
    QCOMPARE(QCoreApplication::translate("MainWindow", "Ready"),
             QStringLiteral("Ready"));
#endif
  }
};

QTEST_MAIN(UiSmokeTest)

#include "test_ui_smoke.moc"
