#include "core/ImageLoader.h"
#include "core/ImageLimits.h"
#include "core/ImageMetadata.h"

#include <QColorSpace>
#include <QFile>
#include <QImageReader>
#include <QtEndian>

namespace arx {

bool ImageLoader::isHighBitDepth(QImage::Format format)
{
    switch (format) {
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied:
    case QImage::Format_Grayscale16:
    case QImage::Format_RGBX16FPx4:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA16FPx4_Premultiplied:
    case QImage::Format_RGBX32FPx4:
    case QImage::Format_RGBA32FPx4:
    case QImage::Format_RGBA32FPx4_Premultiplied:
    case QImage::Format_BGR30:
    case QImage::Format_A2BGR30_Premultiplied:
    case QImage::Format_RGB30:
    case QImage::Format_A2RGB30_Premultiplied:
        return true;
    default:
        return false;
    }
}

// Qt's PNG reader silently returns only the default image of an APNG, so
// look for an acTL chunk before the first IDAT chunk.
bool ImageLoader::isApng(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    static const char sig[8] = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n'};
    if (f.read(8) != QByteArray(sig, 8))
        return false;
    for (int guard = 0; guard < 10000; ++guard) {
        const QByteArray hdr = f.read(8);
        if (hdr.size() != 8)
            return false;
        const quint32 len = qFromBigEndian<quint32>(hdr.constData());
        const QByteArray type = hdr.mid(4, 4);
        if (type == "acTL")
            return true;
        if (type == "IDAT" || type == "IEND")
            return false;
        // skip data + CRC
        if (!f.seek(f.pos() + qint64(len) + 4))
            return false;
    }
    return false;
}

bool ImageLoader::hasTransparentPixel(const QImage &image)
{
    if (!image.hasAlphaChannel())
        return false;
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(line[x]) != 255)
                return true;
        }
    }
    return false;
}

LoadResult ImageLoader::load(const QString &path)
{
    LoadResult r;

    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);

    if (!reader.canRead()) {
        r.error = tr("Cannot read image: %1").arg(reader.errorString());
        return r;
    }

    const QByteArray fmt = reader.format().toLower();
    if (fmt != "png" && fmt != "jpeg" && fmt != "jpg" && fmt != "webp") {
        r.error = tr("Unsupported image format (%1). Only PNG, JPEG and WebP are supported.")
                      .arg(QString::fromLatin1(fmt));
        return r;
    }

    if (reader.imageCount() > 1 || (fmt == "png" && isApng(path))) {
        r.error = tr("Animated or multi-frame images are not supported.");
        return r;
    }
    r.meta = metadata::read(path);

    if (isHighBitDepth(reader.imageFormat())) {
        r.error = tr("16-bit or HDR images are not supported. Please convert to 8 bits per channel first.");
        return r;
    }

    const QSize declared = reader.size();
    if (declared.isValid() && limits::pixelCount(declared) > limits::kMaxInputPixels) {
        r.error = limits::Messages::inputTooLarge(declared);
        return r;
    }

    QImage img = reader.read();
    if (img.isNull()) {
        r.error = tr("Cannot decode image: %1").arg(reader.errorString());
        return r;
    }
    if (isHighBitDepth(img.format())) {
        r.error = tr("16-bit or HDR images are not supported. Please convert to 8 bits per channel first.");
        return r;
    }
    if (limits::pixelCount(img.size()) > limits::kMaxInputPixels) {
        r.error = limits::Messages::inputTooLarge(img.size());
        return r;
    }

    // With autoTransform, Qt applies the orientation it reports (Qt 6.12: JPEG EXIF only;
    // PNG eXIf and WebP EXIF are read as stored and report TransformationNone).
    r.orientationApplied = reader.transformation() != QImageIOHandler::TransformationNone;

    // Colour (v2.1 P3): an embedded RGB ICC profile, or a GRAY profile on a grayscale source,
    // is kept as is: the pixels stay in that colour space and the output gets the same
    // profile back (ProcessJob). Otherwise the old handling: a non-sRGB colour space Qt knows
    // (e.g. PNG gAMA / cHRM, or a profile that cannot be kept) is converted to sRGB and the
    // output is untagged. The working image never carries a colour space, so no Qt
    // conversion below touches the pixel values.
    const QByteArray iccSpace = metadata::iccColorSpace(r.meta.icc);
    const bool keepIcc = iccSpace == "RGB " || (iccSpace == "GRAY" && r.meta.grayscale);
    if (!keepIcc)
        r.meta.icc.clear();
    const QColorSpace cs = img.colorSpace();
    if (!keepIcc && cs.isValid() && cs != QColorSpace(QColorSpace::SRgb)) {
        const QImage::Format target = img.hasAlphaChannel() ? QImage::Format_ARGB32
                                                            : QImage::Format_RGB32;
        img = img.convertToFormat(target);
        img.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
    }
    img.setColorSpace(QColorSpace());

    r.hasAlpha = hasTransparentPixel(img);
    img = img.convertToFormat(r.hasAlpha ? QImage::Format_ARGB32 : QImage::Format_RGB888);
    if (img.isNull()) {
        r.error = tr("Out of memory while converting the image.");
        return r;
    }

    r.ok = true;
    r.image = std::move(img);
    return r;
}

} // namespace arx
