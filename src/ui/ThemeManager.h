#pragma once

#include "core/Settings.h"

#include <QPalette>
#include <QString>

class QApplication;

namespace smb::ui {

class ThemeManager final {
public:
  ThemeManager();

  smb::core::ThemeMode themeMode() const;
  void setThemeMode(smb::core::ThemeMode mode);

  void apply(QApplication &application);

  QPalette paletteForMode(smb::core::ThemeMode mode,
                          const QPalette &systemPalette) const;
  QString styleSheetForMode(smb::core::ThemeMode mode) const;

  static bool isDarkPalette(const QPalette &palette);

private:
  void captureSystemAppearance(QApplication &application);

  smb::core::ThemeMode m_themeMode = smb::core::ThemeMode::System;
  QPalette m_systemPalette;
  QString m_systemStyleSheet;
  bool m_hasSystemAppearance = false;
};

} // namespace smb::ui
