#pragma once

#include <QString>
#include <QStringList>

namespace arx {

// .png / .jpg / .jpeg / .webp (case-insensitive). Content is validated later.
bool isSupportedImageFile(const QString &path);

// Lists supported image files directly inside `dir` (not recursive),
// in natural order ("2.png" before "10.png"). Hidden files are skipped.
QStringList scanFolder(const QString &dir);

} // namespace arx
