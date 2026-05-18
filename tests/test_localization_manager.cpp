#include "ui/LocalizationManager.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QtTest/QtTest>

class LocalizationManagerTest final : public QObject {
  Q_OBJECT

private slots:
  void defaultsToEnglish() {
    smb::ui::LocalizationManager manager;

    QVERIFY(manager.languageMode() == smb::core::LanguageMode::English);
    QVERIFY(manager.resolveLanguageMode(smb::core::LanguageMode::English,
                                        QLocale(QLocale::Russian)) ==
            smb::core::LanguageMode::English);
    QCOMPARE(manager.translationFileName(smb::core::LanguageMode::English),
             QString());
  }

  void resolvesSystemLocaleWithEnglishFallback() {
    smb::ui::LocalizationManager manager;

    QVERIFY(manager.resolveLanguageMode(smb::core::LanguageMode::System,
                                        QLocale(QLocale::Russian,
                                                QLocale::Russia)) ==
            smb::core::LanguageMode::Russian);
    QVERIFY(manager.resolveLanguageMode(smb::core::LanguageMode::System,
                                        QLocale(QLocale::German,
                                                QLocale::Germany)) ==
            smb::core::LanguageMode::English);
  }

  void russianModeUsesStableTranslationFileName() {
    smb::ui::LocalizationManager manager;

    QCOMPARE(manager.localeForMode(smb::core::LanguageMode::Russian).language(),
             QLocale::Russian);
    QCOMPARE(manager.translationFileName(smb::core::LanguageMode::Russian),
             QStringLiteral("smb-browser_ru.qm"));
  }

  void applyEnglishDoesNotInstallTranslator() {
    auto *application = QCoreApplication::instance();
    QVERIFY(application != nullptr);

    smb::ui::LocalizationManager manager;
    manager.setLanguageMode(smb::core::LanguageMode::English);

    const auto result = manager.apply(*application);
    QVERIFY(result.effectiveMode == smb::core::LanguageMode::English);
    QVERIFY(!result.translatorInstalled);
    QVERIFY(result.error.isEmpty());
  }

  void loadsCompiledRussianTranslationWhenAvailable() {
#ifndef SMB_BROWSER_TRANSLATION_DIR
    QSKIP("No compiled translation directory configured.");
#else
    auto *application = QCoreApplication::instance();
    QVERIFY(application != nullptr);

    const auto translationPath =
        QStringLiteral(SMB_BROWSER_TRANSLATION_DIR
                       "/smb-browser_ru.qm");
    if (!QFileInfo::exists(translationPath)) {
      QSKIP("Compiled Russian translation is not available.");
    }

    smb::ui::LocalizationManager manager(
        {QStringLiteral(SMB_BROWSER_TRANSLATION_DIR)});
    manager.setLanguageMode(smb::core::LanguageMode::Russian);

    const auto result = manager.apply(*application);
    QVERIFY(result.effectiveMode == smb::core::LanguageMode::Russian);
    QVERIFY(result.translatorInstalled);
    QCOMPARE(QCoreApplication::translate("MainWindow", "Ready"),
             QStringLiteral("Готово"));
  }
#endif
};

QTEST_MAIN(LocalizationManagerTest)

#include "test_localization_manager.moc"
