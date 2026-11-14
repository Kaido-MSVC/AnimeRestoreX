#pragma once

#include "core/ImageMetadata.h"

#include <QCoreApplication>
#include <QImage>
#include <QString>

namespace arx {

struct LoadResult {
    bool ok = false;
    // RGB888 (opaque) or ARGB32 (has transparent pixels), no colour space attached: the
    // values are sRGB, or in the colour space of meta.icc when that is not empty.
    QImage image;
    bool hasAlpha = false;
    // ICC profile kept for the output (RGB profile, or GRAY profile on a grayscale source;
    // empty otherwise), EXIF block, and whether the source counts as grayscale.
    metadata::SourceInfo meta;
    bool orientationApplied = false;  // the pixels were turned upright from the file's orientation
    QString error;
};

class ImageLoader {
    Q_DECLARE_TR_FUNCTIONS(ImageLoader)
public:
    // Reads a PNG/JPEG/WebP still image, applies orientation metadata and
    // normalises it to 8 bits per channel. Rejects 16-bit/float, animated
    // and oversized (> 4.5 MP) inputs instead of silently degrading them.
    static LoadResult load(const QString &path);

    // Helpers exposed for tests.
    static bool isApng(const QString &path);
    static bool hasTransparentPixel(const QImage &image);
    static bool isHighBitDepth(QImage::Format format);
};

} // namespace arx
