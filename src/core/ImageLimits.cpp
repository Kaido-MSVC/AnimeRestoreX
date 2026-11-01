#include "core/ImageLimits.h"

namespace arx::limits {

int maxSide(OutputFormat format)
{
    switch (format) {
    case OutputFormat::WebP: return 16383;
    case OutputFormat::Jpeg: return 65500;
    case OutputFormat::Png:  return 0;
    }
    return 0;
}

qint64 pixelCount(QSize s)
{
    if (s.width() <= 0 || s.height() <= 0)
        return 0;
    return qint64(s.width()) * qint64(s.height());
}

SizeCheck check(QSize in, int scale, OutputFormat format)
{
    if (in.width() <= 0 || in.height() <= 0 || (scale != 2 && scale != 4))
        return SizeCheck::Invalid;

    const qint64 inPixels = pixelCount(in);
    if (inPixels > kMaxInputPixels)
        return SizeCheck::InputTooLarge;

    // Input sides are < 2^31 and scale <= 4, so 64-bit math cannot overflow.
    const qint64 outW = qint64(in.width()) * scale;
    const qint64 outH = qint64(in.height()) * scale;
    if (outW * outH > kMaxOutputPixels)
        return SizeCheck::OutputTooLarge;

    const int side = maxSide(format);
    if (side > 0 && (outW > side || outH > side))
        return SizeCheck::SideTooLong;

    return SizeCheck::Ok;
}

} // namespace arx::limits
