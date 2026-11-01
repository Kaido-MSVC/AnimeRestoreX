#pragma once

#include "core/JobSpec.h"

#include <QCoreApplication>
#include <QString>

namespace arx {

class OutputNaming {
    Q_DECLARE_TR_FUNCTIONS(OutputNaming)
public:
    // "<base>_<scale>x" for source ".../cat.png" -> "cat_2x"
    static QString stem(const QString &sourcePath, int scale);

    // n == 0 -> "cat_2x.webp", n >= 1 -> "cat_2x (n).webp"
    static QString fileName(const QString &stem, const QString &ext, int n);

    // Path for the encoder's temporary file in the output directory.
    static QString tempPath(const QString &outputDir, const QString &stem);

    // Renames tempFile to the first free "<stem>[ (n)].<ext>" in outputDir.
    // Never overwrites an existing file. Returns the final path, or an empty
    // string and sets *error on failure (tempFile is left untouched then).
    static QString commit(const QString &tempFile, const QString &outputDir,
                          const QString &stem, const QString &ext, QString *error);
};

} // namespace arx
