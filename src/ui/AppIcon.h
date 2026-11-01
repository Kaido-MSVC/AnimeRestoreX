#pragma once

#include <QIcon>

// Window icon built from the PNGs embedded under :/icons (rendered from
// resources/icons/app.svg at development time; no SVG plugin needed at runtime).
inline QIcon appIcon()
{
    QIcon icon;
    for (int s : {16, 24, 32, 48, 64, 256})
        icon.addFile(QStringLiteral(":/icons/app_%1.png").arg(s), QSize(s, s));
    return icon;
}
