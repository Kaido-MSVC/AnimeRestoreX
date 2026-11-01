// icongen <input.svg> <output-dir>
// Renders the SVG at 16/24/32/48/64/256 px (each size rendered natively, not
// downscaled from 256) to app_<size>.png and writes app.ico containing all of
// them as PNG-compressed entries (supported by Windows Vista and later).
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QtEndian>

#include <cstdio>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    if (argc != 3) {
        std::fprintf(stderr, "usage: icongen <input.svg> <output-dir>\n");
        return 2;
    }
    QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));
    if (!svg.isValid()) {
        std::fprintf(stderr, "invalid SVG\n");
        return 1;
    }
    const QDir out(QString::fromLocal8Bit(argv[2]));
    const int sizes[] = {16, 24, 32, 48, 64, 256};

    QList<QByteArray> pngs;
    for (int s : sizes) {
        QImage img(s, s, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            svg.render(&p, QRectF(0, 0, s, s));
        }
        const QImage argb = img.convertToFormat(QImage::Format_ARGB32);
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        if (!argb.save(&buf, "png") || !argb.save(out.filePath(QStringLiteral("app_%1.png").arg(s)), "png")) {
            std::fprintf(stderr, "cannot write PNG %d\n", s);
            return 1;
        }
        pngs << bytes;
    }

    // ICONDIR + ICONDIRENTRY[n] + PNG payloads (little endian).
    QByteArray ico;
    auto put16 = [&](quint16 v) { char b[2]; qToLittleEndian(v, b); ico.append(b, 2); };
    auto put32 = [&](quint32 v) { char b[4]; qToLittleEndian(v, b); ico.append(b, 4); };
    const int count = int(pngs.size());
    put16(0);
    put16(1);
    put16(quint16(count));
    quint32 offset = 6 + 16 * count;
    for (int i = 0; i < count; ++i) {
        const int s = sizes[i];
        ico.append(char(s >= 256 ? 0 : s));  // width (0 = 256)
        ico.append(char(s >= 256 ? 0 : s));  // height
        ico.append(char(0));                 // palette colours
        ico.append(char(0));                 // reserved
        put16(1);                            // colour planes
        put16(32);                           // bits per pixel
        put32(quint32(pngs[i].size()));
        put32(offset);
        offset += quint32(pngs[i].size());
    }
    for (const QByteArray &p : pngs)
        ico.append(p);

    QFile f(out.filePath(QStringLiteral("app.ico")));
    if (!f.open(QIODevice::WriteOnly) || f.write(ico) != ico.size()) {
        std::fprintf(stderr, "cannot write app.ico\n");
        return 1;
    }
    std::printf("wrote %d PNGs and app.ico (%lld bytes)\n", count, static_cast<long long>(ico.size()));
    return 0;
}
