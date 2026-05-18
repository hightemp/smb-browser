#include "ui/TrayController.h"

#include <QAction>
#include <QCloseEvent>
#include <QMainWindow>
#include <QMenu>
#include <QSignalSpy>
#include <QtTest/QtTest>

class TrayControllerTest final : public QObject {
  Q_OBJECT

private slots:
  void buildsMenuWithFavorites() {
    smb::ui::TrayController controller;
    controller.setFavorites({smb::ui::TrayFavoriteConnection{
                                 QStringLiteral("conn-1"),
                                 QStringLiteral("Engineering")},
                             smb::ui::TrayFavoriteConnection{
                                 QStringLiteral("conn-2"),
                                 QStringLiteral("Finance")}});

    auto *menu = controller.menu();
    QVERIFY(menu != nullptr);
    QVERIFY(menu->findChild<QAction *>(QStringLiteral("trayShowAction")) !=
            nullptr);
    QVERIFY(menu->findChild<QAction *>(QStringLiteral("trayExitAction")) !=
            nullptr);

    auto *favorite =
        menu->findChild<QAction *>(QStringLiteral("trayFavoriteAction_conn-1"));
    QVERIFY(favorite != nullptr);
    QCOMPARE(favorite->text(), QStringLiteral("Engineering"));
    QCOMPARE(favorite->data().toString(), QStringLiteral("conn-1"));

    QSignalSpy favoriteSpy(
        &controller, &smb::ui::TrayController::favoriteConnectionRequested);
    favorite->trigger();
    QCOMPARE(favoriteSpy.count(), 1);
    QCOMPARE(favoriteSpy.takeFirst().at(0).toString(),
             QStringLiteral("conn-1"));
  }

  void closeEventHidesWindowWhenCloseToTrayEnabled() {
    QMainWindow window;
    smb::ui::TrayController controller;
    controller.setMainWindow(&window);
    controller.setCloseToTrayEnabled(true);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.isVisible());

    window.close();
    QCoreApplication::processEvents();

    QVERIFY(!window.isVisible());
  }

  void exitActionEmitsExitAndAllowsWindowClose() {
    QMainWindow window;
    smb::ui::TrayController controller;
    controller.setMainWindow(&window);
    controller.setQuitOnExitAction(false);

    auto *exitAction =
        controller.menu()->findChild<QAction *>(QStringLiteral("trayExitAction"));
    QVERIFY(exitAction != nullptr);

    QSignalSpy exitSpy(&controller, &smb::ui::TrayController::exitRequested);
    exitAction->trigger();
    QCOMPARE(exitSpy.count(), 1);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.close());
  }
};

QTEST_MAIN(TrayControllerTest)

#include "test_tray_controller.moc"
