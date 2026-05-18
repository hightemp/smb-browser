#include "ui/LocalizationManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTranslator>
#include <utility>

namespace smb::ui {

namespace {

QStringList deduplicate(QStringList values) {
  QStringList result;
  for (const auto &value : values) {
    if (!value.isEmpty() && !result.contains(value)) {
      result.push_back(value);
    }
  }
  return result;
}

} // namespace

LocalizationManager::LocalizationManager()
    : m_translationDirectories(defaultTranslationDirectories()) {}

LocalizationManager::LocalizationManager(QStringList translationDirectories)
    : m_translationDirectories(deduplicate(std::move(translationDirectories))) {
}

LocalizationManager::~LocalizationManager() { removeCurrentTranslator(); }

smb::core::LanguageMode LocalizationManager::languageMode() const {
  return m_languageMode;
}

void LocalizationManager::setLanguageMode(smb::core::LanguageMode mode) {
  m_languageMode = mode;
}

LocalizationApplyResult
LocalizationManager::apply(QCoreApplication &application) {
  return apply(application, QLocale::system());
}

LocalizationApplyResult
LocalizationManager::apply(QCoreApplication &application,
                           const QLocale &systemLocale) {
  removeCurrentTranslator();

  LocalizationApplyResult result;
  result.requestedMode = m_languageMode;
  result.effectiveMode = resolveLanguageMode(m_languageMode, systemLocale);
  result.effectiveLocale = localeForMode(result.effectiveMode);

  const auto fileName = translationFileName(result.effectiveMode);
  if (fileName.isEmpty()) {
    return result;
  }

  auto translator = std::make_unique<QTranslator>();
  for (const auto &directory : m_translationDirectories) {
    if (translator->load(fileName, directory)) {
      result.translationFile = QDir(directory).filePath(fileName);
      result.translatorInstalled = application.installTranslator(translator.get());
      if (result.translatorInstalled) {
        m_application = &application;
        m_translator = std::move(translator);
      }
      return result;
    }
  }

  result.error = QStringLiteral("Translation file not found: %1").arg(fileName);
  return result;
}

smb::core::LanguageMode LocalizationManager::resolveLanguageMode(
    smb::core::LanguageMode requestedMode, const QLocale &systemLocale) const {
  if (requestedMode == smb::core::LanguageMode::System) {
    return systemLocale.language() == QLocale::Russian
               ? smb::core::LanguageMode::Russian
               : smb::core::LanguageMode::English;
  }

  if (requestedMode == smb::core::LanguageMode::Russian) {
    return smb::core::LanguageMode::Russian;
  }

  return smb::core::LanguageMode::English;
}

QLocale LocalizationManager::localeForMode(smb::core::LanguageMode mode) const {
  if (mode == smb::core::LanguageMode::Russian) {
    return QLocale(QLocale::Russian, QLocale::Russia);
  }

  return QLocale(QLocale::English, QLocale::UnitedStates);
}

QString LocalizationManager::translationFileName(
    smb::core::LanguageMode mode) const {
  if (mode == smb::core::LanguageMode::Russian) {
    return QStringLiteral("smb-browser_ru.qm");
  }

  return {};
}

QStringList LocalizationManager::translationDirectories() const {
  return m_translationDirectories;
}

QStringList LocalizationManager::defaultTranslationDirectories() {
  QStringList directories;

#ifdef SMB_BROWSER_TRANSLATION_DIR
  directories.push_back(QString::fromUtf8(SMB_BROWSER_TRANSLATION_DIR));
#endif

  const auto appDir = QCoreApplication::applicationDirPath();
  if (!appDir.isEmpty()) {
    directories.push_back(QDir(appDir).filePath(QStringLiteral("i18n")));
    directories.push_back(QDir(appDir).filePath(QStringLiteral("translations")));
    directories.push_back(QDir(appDir).filePath(
        QStringLiteral("../share/smb-browser/i18n")));
  }
  directories.push_back(QStringLiteral(":/i18n"));

  return deduplicate(std::move(directories));
}

void LocalizationManager::removeCurrentTranslator() {
  if (m_application != nullptr && m_translator != nullptr) {
    m_application->removeTranslator(m_translator.get());
  }
  m_translator.reset();
  m_application = nullptr;
}

} // namespace smb::ui
