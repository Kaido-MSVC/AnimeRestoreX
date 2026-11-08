#include "core/ImageLimits.h"

#include <algorithm>

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

HeaderCheck checkHeader(QSize declared, qint64 maxPixels)
{
    if (!declared.isValid() || declared.isEmpty())
        return HeaderCheck::UnknownSize;
    return pixelCount(declared) > maxPixels ? HeaderCheck::TooLarge : HeaderCheck::Ok;
}

SizeCheck check(QSize in, int scale, OutputFormat format)
{
    if (in.width() <= 0 || in.height() <= 0 || (scale != 1 && scale != 2 && scale != 4))
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

QString megapixels(qint64 pixels)
{
    return QString::number(double(pixels) / 1'000'000.0, 'f', 1);
}

namespace {
QString formatName(OutputFormat f)
{
    switch (f) {
    case OutputFormat::Png:  return QStringLiteral("PNG");
    case OutputFormat::Jpeg: return QStringLiteral("JPEG");
    case OutputFormat::WebP: return QStringLiteral("WebP");
    }
    return {};
}
} // namespace

QString Messages::inputTooLarge(QSize in)
{
    return tr("Input too large: %1 x %2 = %3 MP > %4 MP limit")
        .arg(in.width()).arg(in.height())
        .arg(megapixels(pixelCount(in)), megapixels(kMaxInputPixels));
}

QString Messages::outputTooLarge(QSize in, int scale)
{
    const qint64 w = qint64(in.width()) * scale, h = qint64(in.height()) * scale;
    return tr("Output too large: %1 x %2 = %3 MP > %4 MP limit")
        .arg(w).arg(h).arg(megapixels(w * h), megapixels(kMaxOutputPixels));
}

QString Messages::sideTooLong(QSize in, int scale, OutputFormat format)
{
    const qint64 w = qint64(in.width()) * scale, h = qint64(in.height()) * scale;
    return tr("Output side too long for %1: %2 x %3, longest side %4 px > %5 px limit")
        .arg(formatName(format)).arg(w).arg(h).arg(std::max(w, h)).arg(maxSide(format));
}

QString Messages::forCheck(SizeCheck result, QSize in, int scale, OutputFormat format)
{
    switch (result) {
    case SizeCheck::Ok:             return {};
    case SizeCheck::Invalid:        return tr("Invalid image size or scale.");
    case SizeCheck::InputTooLarge:  return inputTooLarge(in);
    case SizeCheck::OutputTooLarge: return outputTooLarge(in, scale);
    case SizeCheck::SideTooLong:    return sideTooLong(in, scale, format);
    }
    return {};
}

} // namespace arx::limits
