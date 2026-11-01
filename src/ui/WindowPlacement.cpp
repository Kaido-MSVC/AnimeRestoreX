#include "ui/WindowPlacement.h"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

#include <algorithm>

namespace arx {

QRect fitWindowRect(QSize preferred, QSize minimum, const QRect &available, const QMargins &frame,
                    double maxFraction)
{
    const int frameW = frame.left() + frame.right();
    const int frameH = frame.top() + frame.bottom();
    const QSize limit(int(available.width() * maxFraction) - frameW,
                      int(available.height() * maxFraction) - frameH);
    const QSize size = preferred.boundedTo(limit).expandedTo(minimum);

    // Center the framed window; if it is larger than the screen (minimum too
    // big), keep the title bar reachable at the top-left instead.
    const int outerW = size.width() + frameW;
    const int outerH = size.height() + frameH;
    const int x = available.x() + std::max(0, (available.width() - outerW) / 2) + frame.left();
    const int y = available.y() + std::max(0, (available.height() - outerH) / 2) + frame.top();
    return {QPoint(x, y), size};
}

bool fitsInside(const QRect &client, const QRect &available, const QMargins &frame)
{
    return available.contains(client.marginsAdded(frame));
}

void placeWindow(QWidget *window, const QByteArray &state)
{
    window->ensurePolished();
    const QSize minimum = window->minimumSizeHint().expandedTo(window->minimumSize());

    if (!state.isEmpty() && window->restoreGeometry(state)) {
        if (window->isMaximized())
            return;
        // Qt moves an off-screen window back, but a size saved at a lower
        // display scale can still be too large now.
        const QRect g = window->geometry();
        QScreen *screen = QGuiApplication::screenAt(g.center());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (!screen || fitsInside(g, screen->availableGeometry()))
            return;
        window->setGeometry(fitWindowRect(g.size(), minimum, screen->availableGeometry()));
        return;
    }

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    window->setGeometry(fitWindowRect(window->size(), minimum, screen->availableGeometry()));
}

} // namespace arx
