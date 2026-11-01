#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRandomGenerator>

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

} // namespace testimg
