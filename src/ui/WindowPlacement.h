#pragma once

#include <QMargins>
#include <QRect>
#include <QSize>

class QWidget;

namespace arx {

// Window frame (title bar + borders) assumed before the window is shown and
// its real frame is known; logical pixels.
inline constexpr QMargins kEstimatedFrame{8, 32, 8, 8};

// Client geometry for a top-level window on `available` (a screen's available
// geometry, i.e. without the taskbar). The size is `preferred`, shrunk so the
// framed window covers at most `maxFraction` of the available area, but never
// below `minimum`; the framed window is centered. Pure function for tests.
QRect fitWindowRect(QSize preferred, QSize minimum, const QRect &available,
                    const QMargins &frame = kEstimatedFrame, double maxFraction = 0.9);

// True if the framed client rect lies completely inside `available`.
bool fitsInside(const QRect &client, const QRect &available, const QMargins &frame = kEstimatedFrame);

// Restores `state` (QWidget::saveGeometry) if possible, otherwise or if the
// restored window would not fit its screen, places the window with
// fitWindowRect(). Call before the first show().
void placeWindow(QWidget *window, const QByteArray &state);

} // namespace arx
