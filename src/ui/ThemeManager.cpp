#include "ui/ThemeManager.h"

#include <QApplication>
#include <QColor>
#include <QPalette>

namespace smb::ui {

namespace {

QPalette lightPalette() {
  QPalette palette;
  palette.setColor(QPalette::Window, QColor(245, 247, 250));
  palette.setColor(QPalette::WindowText, QColor(24, 29, 38));
  palette.setColor(QPalette::Base, QColor(255, 255, 255));
  palette.setColor(QPalette::AlternateBase, QColor(238, 242, 247));
  palette.setColor(QPalette::Text, QColor(24, 29, 38));
  palette.setColor(QPalette::Button, QColor(245, 247, 250));
  palette.setColor(QPalette::ButtonText, QColor(24, 29, 38));
  palette.setColor(QPalette::Highlight, QColor(38, 132, 255));
  palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
  palette.setColor(QPalette::ToolTipText, QColor(24, 29, 38));
  palette.setColor(QPalette::Link, QColor(0, 92, 197));
  return palette;
}

QPalette darkPalette() {
  QPalette palette;
  palette.setColor(QPalette::Window, QColor(32, 36, 42));
  palette.setColor(QPalette::WindowText, QColor(236, 239, 244));
  palette.setColor(QPalette::Base, QColor(24, 27, 32));
  palette.setColor(QPalette::AlternateBase, QColor(39, 44, 52));
  palette.setColor(QPalette::Text, QColor(236, 239, 244));
  palette.setColor(QPalette::Button, QColor(45, 51, 60));
  palette.setColor(QPalette::ButtonText, QColor(236, 239, 244));
  palette.setColor(QPalette::Highlight, QColor(58, 141, 255));
  palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  palette.setColor(QPalette::ToolTipBase, QColor(45, 51, 60));
  palette.setColor(QPalette::ToolTipText, QColor(236, 239, 244));
  palette.setColor(QPalette::Link, QColor(113, 171, 255));
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor(128, 135, 145));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                   QColor(128, 135, 145));
  return palette;
}

} // namespace

ThemeManager::ThemeManager() = default;

smb::core::ThemeMode ThemeManager::themeMode() const { return m_themeMode; }

void ThemeManager::setThemeMode(smb::core::ThemeMode mode) {
  m_themeMode = mode;
}

void ThemeManager::apply(QApplication &application) {
  captureSystemAppearance(application);
  application.setPalette(paletteForMode(m_themeMode, m_systemPalette));
  application.setStyleSheet(styleSheetForMode(m_themeMode));
}

QPalette ThemeManager::paletteForMode(smb::core::ThemeMode mode,
                                      const QPalette &systemPalette) const {
  switch (mode) {
  case smb::core::ThemeMode::System:
    return systemPalette;
  case smb::core::ThemeMode::Light:
    return lightPalette();
  case smb::core::ThemeMode::Dark:
    return darkPalette();
  }

  return systemPalette;
}

QString ThemeManager::styleSheetForMode(smb::core::ThemeMode mode) const {
  if (mode == smb::core::ThemeMode::System) {
    return m_systemStyleSheet;
  }

  return QStringLiteral(
      "QToolTip { border: 1px solid palette(mid); padding: 4px; }");
}

bool ThemeManager::isDarkPalette(const QPalette &palette) {
  return palette.color(QPalette::Window).lightness() <
         palette.color(QPalette::WindowText).lightness();
}

void ThemeManager::captureSystemAppearance(QApplication &application) {
  if (m_hasSystemAppearance) {
    return;
  }

  m_systemPalette = application.palette();
  m_systemStyleSheet = application.styleSheet();
  m_hasSystemAppearance = true;
}

} // namespace smb::ui
