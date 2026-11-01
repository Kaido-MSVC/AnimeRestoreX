#include "core/FolderScanner.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace arx {

bool isSupportedImageFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("png") || suffix == QLatin1String("jpg")
        || suffix == QLatin1String("jpeg") || suffix == QLatin1String("webp");
}

QStringList scanFolder(const QString &dir)
{
    const QDir d(dir);
    const QFileInfoList entries = d.entryInfoList(
        {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
         QStringLiteral("*.webp")},
        QDir::Files | QDir::Readable | QDir::NoDotAndDotDot);

    QStringList files;
    files.reserve(entries.size());
    for (const QFileInfo &fi : entries) {
        if (isSupportedImageFile(fi.fileName()))
            files << fi.absoluteFilePath();
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) {
        return collator.compare(QFileInfo(a).fileName(), QFileInfo(b).fileName()) < 0;
    });
    return files;
}

} // namespace arx
