#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

class QApplication;

// Visual theme "Creative studio" (v1.2, design direction B): warm paper
// surfaces with a vermilion accent in light mode, plum-tinted dark surfaces
// with a coral accent in dark mode. Fusion style + palette + one style sheet
// built from color tokens. Follows the Windows light / dark setting live.
namespace theme {

enum class Mode { Light, Dark };

// Fusion, application font and the theme of the current system color
// scheme; re-applied whenever the system scheme changes.
void install(QApplication &app);

// Applies one mode (palette + style sheet) to the application.
void apply(Mode mode);
Mode currentMode();
// Mode for the current system color scheme (Unknown counts as light).
Mode systemMode();

QPalette palette(Mode mode);
QString styleSheet(Mode mode);

// Token colors of the current mode for code that paints by itself.
QColor color(const char *token);
inline QColor danger() { return color("danger"); }

} // namespace theme
