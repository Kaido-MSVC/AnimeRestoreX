#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRandomGenerator>
#include <QtEndian>

namespace testimg {

// Line-art-like synthetic image with some noise so that encoders produce
// quality-dependent output. If `transparent`, the left quarter is fully
// transparent and the next quarter half-transparent.
inline QImage make(int w, int h, bool transparent)
{
    QImage img(w, h, QImage::Format_ARGB32);
    QRandomGenerator rng(1234);
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int n = int(rng.bounded(24));
            const int r = 200 + (x * 40) / w - n / 2;
            const int g = 180 + (y * 60) / h - n / 2;
            const int b = 230 - n;
            int a = 255;
            if (transparent) {
                if (x < w / 4)
                    a = 0;
                else if (x < w / 2)
                    a = 128;
            }
            line[x] = qRgba(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255), a);
        }
    }
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(30, 20, 40), 2));
    p.drawEllipse(QRectF(w * 0.2, h * 0.2, w * 0.6, h * 0.6));
    p.drawLine(QPointF(0, h - 1), QPointF(w - 1, 0));
    return img;
}

inline QByteArray sha256(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
}

// Minimal EXIF (TIFF) block for the metadata tests:
//    0 header | 8 IFD0: Make (out of line, 8 bytes), Orientation, Exif pointer, next = IFD1
//   50 "ARXTest\0" | 58 Exif IFD: PixelXDimension (96), PixelYDimension (64) of type pixType
//   88 IFD1: JPEGInterchangeFormat, ...Length | 118 thumbnail (10 bytes 0xAB)
// Orientation is at 30, PixelXDimension at 68, PixelYDimension at 80, the IFD1 link at 46.
inline QByteArray makeExif(bool le, quint16 orientation, bool thumbnail, quint16 pixType = 3)
{
    QByteArray b(thumbnail ? 128 : 88, '\0');
    auto p16 = [&](int off, quint16 v) { le ? qToLittleEndian(v, b.data() + off) : qToBigEndian(v, b.data() + off); };
    auto p32 = [&](int off, quint32 v) { le ? qToLittleEndian(v, b.data() + off) : qToBigEndian(v, b.data() + off); };
    auto entry = [&](int at, quint16 tag, quint16 type, quint32 count, quint32 value) {
        p16(at, tag);
        p16(at + 2, type);
        p32(at + 4, count);
        if (type == 3 && count == 1) {
            p32(at + 8, 0);
            p16(at + 8, quint16(value));
        } else {
            p32(at + 8, value);
        }
    };
    b.replace(0, 4, le ? QByteArray("II*\0", 4) : QByteArray("MM\0*", 4));
    p32(4, 8);
    p16(8, 3);
    entry(10, 0x010F, 2, 8, 50);
    entry(22, 0x0112, 3, 1, orientation);
    entry(34, 0x8769, 4, 1, 58);
    p32(46, thumbnail ? 88 : 0);
    b.replace(50, 8, QByteArray("ARXTest\0", 8));
    p16(58, 2);
    entry(60, 0xA002, pixType, 1, 96);
    entry(72, 0xA003, pixType, 1, 64);
    p32(84, 0);
    if (thumbnail) {
        p16(88, 2);
        entry(90, 0x0201, 4, 1, 118);
        entry(102, 0x0202, 4, 1, 10);
        p32(114, 0);
        b.replace(118, 10, QByteArray(10, '\xAB'));
    }
    return b;
}

inline quint32 exifU32(const QByteArray &b, int off)
{
    return b.startsWith("II") ? qFromLittleEndian<quint32>(b.constData() + off) : qFromBigEndian<quint32>(b.constData() + off);
}
inline quint16 exifU16(const QByteArray &b, int off)
{
    return b.startsWith("II") ? qFromLittleEndian<quint16>(b.constData() + off) : qFromBigEndian<quint16>(b.constData() + off);
}

} // namespace testimg
