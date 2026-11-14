#include "core/ImageEncoder.h"
#include "core/ImageMetadata.h"

#include <QBuffer>
#include <QImageWriter>
#include <QPainter>

#include <algorithm>

namespace arx {

QByteArray ImageEncoder::formatName(OutputFormat f)
{
    switch (f) {
    case OutputFormat::Png:  return "png";
    case OutputFormat::Jpeg: return "jpeg";
    case OutputFormat::WebP: return "webp";
    }
    return "png";
}

QString ImageEncoder::extension(OutputFormat f)
{
    switch (f) {
    case OutputFormat::Png:  return QStringLiteral("png");
    case OutputFormat::Jpeg: return QStringLiteral("jpg");
    case OutputFormat::WebP: return QStringLiteral("webp");
    }
    return QStringLiteral("png");
}

int ImageEncoder::effectiveQuality(const EncodeOptions &opt)
{
    switch (opt.format) {
    case OutputFormat::Png:
        return -1;
    case OutputFormat::Jpeg:
        return std::clamp(opt.quality, 0, 100);
    case OutputFormat::WebP:
        return opt.webpLossless ? 100 : std::clamp(opt.quality, 0, 99);
    }
    return -1;
}

QImage ImageEncoder::prepare(const QImage &image, OutputFormat f)
{
    if (f != OutputFormat::Jpeg || !image.hasAlphaChannel())
        return image;

    QImage bg(image.size(), QImage::Format_RGB32);
    if (bg.isNull())
        return {};
    bg.fill(Qt::white);
    {
        QPainter p(&bg);
        p.drawImage(0, 0, image);
    }
    return bg.convertToFormat(QImage::Format_RGB888);
}

void ImageEncoder::toGrayscale(uchar *pixels, int width, int height, int channels, qsizetype stride)
{
    for (int y = 0; y < height; ++y) {
        uchar *p = pixels + stride * y;
        for (int x = 0; x < width; ++x, p += channels)
            p[0] = p[1] = p[2] = luma(p[0], p[1], p[2]);
    }
}

QImage ImageEncoder::grayPlane(const QImage &rgb888)
{
    QImage g(rgb888.size(), QImage::Format_Grayscale8);
    if (g.isNull() || rgb888.format() != QImage::Format_RGB888)
        return {};
    for (int y = 0; y < g.height(); ++y) {
        const uchar *s = rgb888.constScanLine(y);
        uchar *d = g.scanLine(y);
        for (int x = 0; x < g.width(); ++x)
            d[x] = s[3 * x];
    }
    return g;
}

bool ImageEncoder::encode(const QImage &image, const EncodeOptions &opt, const Extras &extras, QIODevice *device,
                          QString *error, QString *metadataError)
{
    if (!extras.grayscale && extras.icc.isEmpty() && extras.exif.isEmpty())
        return encode(image, opt, device, error);

    QImage prepared = prepare(image, opt.format);
    // JPEG composites onto white first (RGB888, R = G = B stays equal), as nunif removes
    // the alpha of the LA image. PNG with alpha and WebP (no grayscale mode) stay RGB(A).
    if (extras.grayscale && !prepared.isNull() && opt.format != OutputFormat::WebP && !prepared.hasAlphaChannel())
        prepared = grayPlane(prepared.convertToFormat(QImage::Format_RGB888));
    if (prepared.isNull()) {
        if (error)
            *error = tr("Out of memory while preparing the output image.");
        return false;
    }
    if (extras.icc.isEmpty() && extras.exif.isEmpty())
        return encode(prepared, opt, device, error);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    if (!encode(prepared, opt, &buffer, error))
        return false;
    buffer.close();
    prepared = QImage();
    auto withMeta = metadata::embed(buffer.data(), opt.format, extras.icc, extras.exif);
    const QByteArray &bytes = withMeta ? *withMeta : buffer.data();
    if (!withMeta && metadataError)
        *metadataError = withMeta.error();
    if (device->write(bytes) != bytes.size()) {
        if (error)
            *error = tr("Failed to write the output file: %1").arg(device->errorString());
        return false;
    }
    return true;
}

bool ImageEncoder::encode(const QImage &image, const EncodeOptions &opt, QIODevice *device,
                          QString *error)
{
    const QImage prepared = prepare(image, opt.format);
    if (prepared.isNull()) {
        if (error)
            *error = tr("Out of memory while preparing the output image.");
        return false;
    }

    QImageWriter writer(device, formatName(opt.format));
    const int q = effectiveQuality(opt);
    if (q >= 0)
        writer.setQuality(q);

    if (!writer.canWrite()) {
        if (error)
            *error = tr("The %1 encoder is not available: %2")
                         .arg(QString::fromLatin1(formatName(opt.format)).toUpper(),
                              writer.errorString());
        return false;
    }
    if (!writer.write(prepared)) {
        if (error)
            *error = tr("Failed to encode the output image: %1").arg(writer.errorString());
        return false;
    }
    return true;
}

} // namespace arx
