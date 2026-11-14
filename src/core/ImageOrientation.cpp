#include "core/ImageOrientation.h"

#include <QColorSpace>
#include <QTransform>

namespace arx::orientation {

QImageIOHandler::Transformations fromExif(int orientation)
{
    // Same table as Qt's JPEG handler and Pillow ImageOps.exif_transpose; for 5 and 7 the
    // mirror / flip comes first, then the clockwise 90-degree turn:
    //  5 TRANSPOSE   out(x, y) = in(y, x)              = flip, then rotate 90
    //  7 TRANSVERSE  out(x, y) = in(W-1-y, H-1-x)      = mirror, then rotate 90
    switch (orientation) {
    case 2: return QImageIOHandler::TransformationMirror;
    case 3: return QImageIOHandler::TransformationRotate180;
    case 4: return QImageIOHandler::TransformationFlip;
    case 5: return QImageIOHandler::TransformationFlipAndRotate90;
    case 6: return QImageIOHandler::TransformationRotate90;
    case 7: return QImageIOHandler::TransformationMirrorAndRotate90;
    case 8: return QImageIOHandler::TransformationRotate270;
    default: return QImageIOHandler::TransformationNone;
    }
}

QImage apply(const QImage &image, QImageIOHandler::Transformations t)
{
    if (t == QImageIOHandler::TransformationNone || image.isNull())
        return image;
    Qt::Orientations flip;
    if (t & QImageIOHandler::TransformationMirror)
        flip |= Qt::Horizontal;
    if (t & QImageIOHandler::TransformationFlip)
        flip |= Qt::Vertical;
    QImage out = flip ? image.flipped(flip) : image;
    // QTransform::rotate(90) is exact (m12 = 1, m21 = -1), and QImage::transformed moves the
    // pixels without resampling for a quarter turn (verified against Pillow in tst_core).
    if (t & QImageIOHandler::TransformationRotate90)
        out = out.transformed(QTransform().rotate(90));
    return out;
}

QSize applied(QSize stored, QImageIOHandler::Transformations t)
{
    return (t & QImageIOHandler::TransformationRotate90) ? stored.transposed() : stored;
}

} // namespace arx::orientation

namespace arx {

OrientedReader::OrientedReader(const QString &path, const metadata::SourceInfo *meta)
    : m_reader(path)
{
    m_reader.setDecideFormatFromContent(true);
    const QByteArray fmt = m_reader.format().toLower();
    m_applyHere = fmt == "png" || fmt == "webp";
    // JPEG: Qt reads and applies the EXIF orientation (autoTransform). PNG / WebP: Qt 6.12
    // reports none; autoTransform is switched off so that a later Qt that starts applying it
    // cannot turn the image twice.
    m_reader.setAutoTransform(!m_applyHere);
    m_stored = m_reader.size();
    if (m_applyHere) {
        // Unreadable EXIF or an invalid value: exifOrientation gives 1, nothing is turned.
        const int o = meta ? metadata::exifOrientation(meta->exif)
                           : metadata::exifOrientation(metadata::read(path).exif);
        m_transform = orientation::fromExif(o);
    } else {
        m_transform = m_reader.transformation();
    }
}

void OrientedReader::scaleToFit(int side)
{
    // The decoder scales before Qt applies an orientation (Qt 6.12), and here the
    // orientation is applied after decoding as well, so the box is in stored orientation.
    if (m_stored.isValid())
        m_reader.setScaledSize(m_stored.scaled(side, side, Qt::KeepAspectRatio));
}

QImage OrientedReader::read()
{
    QImage img = m_reader.read();
    if (img.isNull() || !m_applyHere || m_transform == QImageIOHandler::TransformationNone)
        return img;
    // Palette and 1-bit images go to 32 bits first (lossless), so that the quarter turn is
    // a plain pixel move for every format the PNG reader returns.
    if (img.format() == QImage::Format_Indexed8 || img.depth() < 8)
        img = img.convertToFormat(img.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    const QColorSpace cs = img.colorSpace();
    QImage out = orientation::apply(img, m_transform);
    out.setColorSpace(cs);  // only the tag; the loader decides about conversions afterwards
    return out;
}

} // namespace arx
