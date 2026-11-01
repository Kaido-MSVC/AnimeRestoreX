#include "core/ImageEncoder.h"

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
