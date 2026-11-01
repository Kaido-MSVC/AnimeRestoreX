#include "core/OutputNaming.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

namespace arx {

QString OutputNaming::stem(const QString &sourcePath, int scale)
{
    return QStringLiteral("%1_%2x").arg(QFileInfo(sourcePath).completeBaseName()).arg(scale);
}

QString OutputNaming::fileName(const QString &stem, const QString &ext, int n)
{
    if (n <= 0)
        return QStringLiteral("%1.%2").arg(stem, ext);
    return QStringLiteral("%1 (%2).%3").arg(stem).arg(n).arg(ext);
}

QString OutputNaming::tempPath(const QString &outputDir, const QString &stem)
{
    const quint32 r = QRandomGenerator::global()->generate();
    return QDir(outputDir).filePath(
        QStringLiteral(".%1.arx-%2.tmp").arg(stem).arg(r, 8, 16, QLatin1Char('0')));
}

QString OutputNaming::commit(const QString &tempFile, const QString &outputDir,
                             const QString &stem, const QString &ext, QString *error)
{
    const QDir dir(outputDir);
    constexpr int kMaxAttempts = 10000;
    for (int n = 0; n < kMaxAttempts; ++n) {
        const QString target = dir.filePath(fileName(stem, ext, n));
        if (QFileInfo::exists(target))
            continue;
        // QFile::rename never replaces an existing destination.
        if (QFile::rename(tempFile, target))
            return target;
        if (QFileInfo::exists(target))
            continue;  // created concurrently; try the next number
        if (error)
            *error = tr("Cannot create the output file %1.").arg(QDir::toNativeSeparators(target));
        return {};
    }
    if (error)
        *error = tr("Too many files with the same name in the output folder.");
    return {};
}

} // namespace arx
