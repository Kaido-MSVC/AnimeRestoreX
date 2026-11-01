#pragma once

#include "core/JobSpec.h"

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

} // namespace arx::limits
