#pragma once

#include "core/Settings.h"

#include <QLocale>
#include <QString>
#include <QStringList>
#include <memory>

class QCoreApplication;
class QTranslator;

namespace smb::ui {

struct LocalizationApplyResult {
  smb::core::LanguageMode requestedMode = smb::core::LanguageMode::English;
  smb::core::LanguageMode effectiveMode = smb::core::LanguageMode::English;
  QLocale effectiveLocale = QLocale(QLocale::English, QLocale::UnitedStates);
  QString translationFile;
  QString error;
  bool translatorInstalled = false;
};

class LocalizationManager final {
public:
  LocalizationManager();
  explicit LocalizationManager(QStringList translationDirectories);
  ~LocalizationManager();

  smb::core::LanguageMode languageMode() const;
  void setLanguageMode(smb::core::LanguageMode mode);

  LocalizationApplyResult apply(QCoreApplication &application);
  LocalizationApplyResult apply(QCoreApplication &application,
                                const QLocale &systemLocale);

  smb::core::LanguageMode
  resolveLanguageMode(smb::core::LanguageMode requestedMode,
                      const QLocale &systemLocale) const;
  QLocale localeForMode(smb::core::LanguageMode mode) const;
  QString translationFileName(smb::core::LanguageMode mode) const;

  QStringList translationDirectories() const;
  static QStringList defaultTranslationDirectories();

private:
  void removeCurrentTranslator();

  smb::core::LanguageMode m_languageMode = smb::core::LanguageMode::English;
  QStringList m_translationDirectories;
  QCoreApplication *m_application = nullptr;
  std::unique_ptr<QTranslator> m_translator;
};

} // namespace smb::ui
