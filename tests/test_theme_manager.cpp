#include "ui/ThemeManager.h"

#include <QApplication>
#include <QtTest/QtTest>

class ThemeManagerTest final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    m_originalPalette = application->palette();
    m_originalStyleSheet = application->styleSheet();
  }

  void cleanup() {
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    application->setPalette(m_originalPalette);
    application->setStyleSheet(m_originalStyleSheet);
  }

  void defaultsToSystemMode() {
    smb::ui::ThemeManager manager;

    QVERIFY(manager.themeMode() == smb::core::ThemeMode::System);
    QCOMPARE(manager.paletteForMode(smb::core::ThemeMode::System,
                                    m_originalPalette)
                 .color(QPalette::Window),
             m_originalPalette.color(QPalette::Window));
  }

  void appliesDarkAndLightPalettes() {
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);

    smb::ui::ThemeManager manager;
    manager.setThemeMode(smb::core::ThemeMode::Dark);
    manager.apply(*application);
    QVERIFY(smb::ui::ThemeManager::isDarkPalette(application->palette()));

    const auto darkWindowLightness =
        application->palette().color(QPalette::Window).lightness();

    manager.setThemeMode(smb::core::ThemeMode::Light);
    manager.apply(*application);
    QVERIFY(!smb::ui::ThemeManager::isDarkPalette(application->palette()));
    QVERIFY(application->palette().color(QPalette::Window).lightness() >
            darkWindowLightness);
  }

  void systemModeRestoresCapturedAppearance() {
    auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
    QVERIFY(application != nullptr);

    smb::ui::ThemeManager manager;
    manager.setThemeMode(smb::core::ThemeMode::Dark);
    manager.apply(*application);
    QVERIFY(application->palette().color(QPalette::Window) !=
            m_originalPalette.color(QPalette::Window));

    manager.setThemeMode(smb::core::ThemeMode::System);
    manager.apply(*application);
    QCOMPARE(application->palette().color(QPalette::Window),
             m_originalPalette.color(QPalette::Window));
    QCOMPARE(application->styleSheet(), m_originalStyleSheet);
  }

private:
  QPalette m_originalPalette;
  QString m_originalStyleSheet;
};

QTEST_MAIN(ThemeManagerTest)

#include "test_theme_manager.moc"
