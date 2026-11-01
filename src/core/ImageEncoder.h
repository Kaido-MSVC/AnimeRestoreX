#pragma once

#include "core/JobSpec.h"

#include <QCoreApplication>
#include <QImage>
#include <QString>

class QIODevice;

namespace arx {

class ImageEncoder {
    Q_DECLARE_TR_FUNCTIONS(ImageEncoder)
public:
    static QByteArray formatName(OutputFormat f);   // "png" / "jpeg" / "webp"
    static QString extension(OutputFormat f);       // "png" / "jpg" / "webp"

    // Quality value actually handed to QImageWriter (-1 = writer default).
    // WebP: lossless -> 100 (Qt's handler switches to lossless at 100), lossy -> 0..99.
    static int effectiveQuality(const EncodeOptions &opt);

    // JPEG: composites transparent images onto white. Others: unchanged.
    static QImage prepare(const QImage &image, OutputFormat f);

    // Encodes into an already opened device. Returns false and sets *error on failure.
    static bool encode(const QImage &image, const EncodeOptions &opt, QIODevice *device,
                       QString *error);
};

} // namespace arx
