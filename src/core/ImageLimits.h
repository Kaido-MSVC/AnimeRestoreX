#pragma once

#include "core/JobSpec.h"

#include <QCoreApplication>
#include <QSize>
#include <QString>
#include <QtGlobal>

namespace arx::limits {

// Decimal megapixels: 1 MP = 1,000,000 pixels.
inline constexpr qint64 kMaxInputPixels = 4'500'000;
inline constexpr qint64 kMaxOutputPixels = 75'000'000;

// Per-format maximum width/height of the final output, 0 = no extra limit.
// WebP: WEBP_MAX_DIMENSION = 16383. JPEG: libjpeg JPEG_MAX_DIMENSION = 65500.
int maxSide(OutputFormat format);

enum class SizeCheck { Ok, Invalid, InputTooLarge, OutputTooLarge, SideTooLong };

// Overflow-safe check for an input of size `in` upscaled by `scale`.
SizeCheck check(QSize in, int scale, OutputFormat format);

qint64 pixelCount(QSize s);

// Check before decoding an image only to display it (preview, list thumbnail): the size
// declared in the file header (QImageReader::size()) against a pixel limit, so that an
// oversized or unreadable image is never decoded. Originals use kMaxInputPixels.
enum class HeaderCheck { Ok, UnknownSize, TooLarge };
HeaderCheck checkHeader(QSize declared, qint64 maxPixels);

// "30.8" for 30,800,000 pixels (decimal megapixels, one decimal place).
QString megapixels(qint64 pixels);

// User-facing rejection messages that include the actual values and limits.
class Messages {
    Q_DECLARE_TR_FUNCTIONS(SizeLimits)
public:
    static QString inputTooLarge(QSize in);
    static QString outputTooLarge(QSize in, int scale);
    static QString sideTooLong(QSize in, int scale, OutputFormat format);
    static QString forCheck(SizeCheck result, QSize in, int scale, OutputFormat format);
};

} // namespace arx::limits
