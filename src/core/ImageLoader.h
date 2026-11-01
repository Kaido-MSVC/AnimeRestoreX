#pragma once

#include <QCoreApplication>
#include <QImage>
#include <QString>

namespace arx {

struct LoadResult {
    bool ok = false;
    QImage image;          // RGB888 (opaque) or ARGB32 (has transparent pixels), sRGB
    bool hasAlpha = false;
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
