#include "TestImages.h"

#include "core/ImageEncoder.h"
#include "core/ImageLimits.h"
#include "core/ImageLoader.h"
#include "core/OutputNaming.h"

#include <QBuffer>
#include <QDir>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

using namespace arx;

class TestCore : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void sizeLimits();
    void naming();
    void commitDoesNotOverwrite();
    void rejects16Bit();
    void rejectsCorruptAndUnsupported();
    void rejectsApng();
    void appliesOrientation();
    void opaqueAlphaIsDropped();
    void webpLosslessIsLossless();
    void webpLossyUsesVp8AndQuality();
    void jpegQualityAndWhiteBackground();
    void pngKeepsAlpha();

private:
    QByteArray encodeToBytes(const QImage &img, const EncodeOptions &opt);
    QTemporaryDir m_dir;
};

void TestCore::initTestCase()
{
    QImageReader::setAllocationLimit(1024);
    QVERIFY(m_dir.isValid());
    QVERIFY(QImageWriter::supportedImageFormats().contains("webp"));
    QVERIFY(QImageWriter::supportedImageFormats().contains("jpeg"));
}

QByteArray TestCore::encodeToBytes(const QImage &img, const EncodeOptions &opt)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    QString err;
    if (!ImageEncoder::encode(img, opt, &buf, &err))
        qWarning() << err;
    return bytes;
}

void TestCore::sizeLimits()
{
    using limits::SizeCheck;
    // 3000 x 1500 = 4.5 MP; 4x -> 12000 x 6000 = 72 MP
    QCOMPARE(limits::check({3000, 1500}, 4, OutputFormat::Png), SizeCheck::Ok);
    QCOMPARE(limits::check({3000, 1500}, 4, OutputFormat::WebP), SizeCheck::Ok);
    QCOMPARE(limits::check({3000, 1501}, 2, OutputFormat::Png), SizeCheck::InputTooLarge);
    // 4500 x 1000 4x -> 18000 x 4000: too wide for WebP, fine for PNG/JPEG
    QCOMPARE(limits::check({4500, 1000}, 4, OutputFormat::WebP), SizeCheck::SideTooLong);
    QCOMPARE(limits::check({4500, 1000}, 4, OutputFormat::Png), SizeCheck::Ok);
    QCOMPARE(limits::check({4500, 1000}, 4, OutputFormat::Jpeg), SizeCheck::Ok);
    QCOMPARE(limits::check({4096, 1000}, 4, OutputFormat::WebP), SizeCheck::SideTooLong);
    QCOMPARE(limits::check({4095, 1000}, 4, OutputFormat::WebP), SizeCheck::Ok);  // 16380
    QCOMPARE(limits::check({0, 10}, 2, OutputFormat::Png), SizeCheck::Invalid);
    QCOMPARE(limits::check({10, 10}, 3, OutputFormat::Png), SizeCheck::Invalid);
    // Large sides do not overflow
    QCOMPARE(limits::check({2'000'000'000, 2'000'000'000}, 4, OutputFormat::Png),
             SizeCheck::InputTooLarge);
}

void TestCore::naming()
{
    QCOMPARE(OutputNaming::stem(QStringLiteral("D:/画像 フォルダ/貓 圖.final.png"), 2),
             QStringLiteral("貓 圖.final_2x"));
    QCOMPARE(OutputNaming::fileName(QStringLiteral("cat_4x"), QStringLiteral("webp"), 0),
             QStringLiteral("cat_4x.webp"));
    QCOMPARE(OutputNaming::fileName(QStringLiteral("cat_4x"), QStringLiteral("webp"), 2),
             QStringLiteral("cat_4x (2).webp"));
}

void TestCore::commitDoesNotOverwrite()
{
    const QString out = m_dir.filePath(QStringLiteral("naming 測試"));
    QVERIFY(QDir().mkpath(out));
    const QString existing = QDir(out).filePath(QStringLiteral("cat_2x.png"));
    {
        QFile f(existing);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("original");
    }
    for (int i = 1; i <= 2; ++i) {
        const QString tmp = OutputNaming::tempPath(out, QStringLiteral("cat_2x"));
        QFile f(tmp);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        f.write("new");
        f.close();
        QString err;
        const QString final = OutputNaming::commit(tmp, out, QStringLiteral("cat_2x"),
                                                   QStringLiteral("png"), &err);
        QCOMPARE(QFileInfo(final).fileName(), QStringLiteral("cat_2x (%1).png").arg(i));
        QVERIFY(!QFileInfo::exists(tmp));
    }
    QFile f(existing);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("original"));
}

void TestCore::rejects16Bit()
{
    const QString path = m_dir.filePath(QStringLiteral("deep16.png"));
    QImage img = testimg::make(32, 16, false).convertToFormat(QImage::Format_RGBA64);
    QVERIFY(img.save(path, "png"));
    QImageReader probe(path);
    QVERIFY2(ImageLoader::isHighBitDepth(probe.imageFormat()), "Qt should report a 16-bit format");
    const LoadResult r = ImageLoader::load(path);
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains(QStringLiteral("16-bit")));
}

void TestCore::rejectsCorruptAndUnsupported()
{
    const QString corrupt = m_dir.filePath(QStringLiteral("corrupt.png"));
    {
        QFile f(corrupt);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\x89PNG\r\n\x1a\n garbage garbage");
    }
    LoadResult r = ImageLoader::load(corrupt);
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());

    const QString bmp = m_dir.filePath(QStringLiteral("x.bmp"));
    QVERIFY(testimg::make(8, 8, false).save(bmp, "bmp"));
    r = ImageLoader::load(bmp);
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains(QStringLiteral("Unsupported")));

    r = ImageLoader::load(m_dir.filePath(QStringLiteral("does-not-exist.png")));
    QVERIFY(!r.ok);
}

void TestCore::rejectsApng()
{
    // Insert an acTL chunk right after IHDR (8 sig + 25 IHDR bytes).
    QByteArray png;
    {
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        QVERIFY(testimg::make(16, 16, false).save(&b, "png"));
    }
    QByteArray actl;
    char len[4];
    qToBigEndian<quint32>(8, len);
    actl.append(len, 4).append("acTL").append(QByteArray(8, '\0')).append(QByteArray(4, '\0'));
    png.insert(33, actl);
    const QString path = m_dir.filePath(QStringLiteral("anim.png"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(png);
    }
    QVERIFY(ImageLoader::isApng(path));
    const LoadResult r = ImageLoader::load(path);
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains(QStringLiteral("Animated")));

    const QString plain = m_dir.filePath(QStringLiteral("plain.png"));
    QVERIFY(testimg::make(16, 16, false).save(plain, "png"));
    QVERIFY(!ImageLoader::isApng(plain));
}

void TestCore::appliesOrientation()
{
    const QString path = m_dir.filePath(QStringLiteral("rot.jpg"));
    QImageWriter w(path, "jpeg");
    QVERIFY(w.supportsOption(QImageIOHandler::ImageTransformation));
    w.setTransformation(QImageIOHandler::TransformationRotate90);
    QVERIFY(w.write(testimg::make(40, 20, false).convertToFormat(QImage::Format_RGB32)));
    const LoadResult r = ImageLoader::load(path);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.image.size(), QSize(20, 40));
}

void TestCore::opaqueAlphaIsDropped()
{
    const QString path = m_dir.filePath(QStringLiteral("opaque_rgba.png"));
    QVERIFY(testimg::make(16, 16, false).save(path, "png"));  // ARGB32 with all alpha 255
    LoadResult r = ImageLoader::load(path);
    QVERIFY(r.ok);
    QVERIFY(!r.hasAlpha);
    QCOMPARE(r.image.format(), QImage::Format_RGB888);

    const QString tpath = m_dir.filePath(QStringLiteral("transparent.png"));
    QVERIFY(testimg::make(16, 16, true).save(tpath, "png"));
    r = ImageLoader::load(tpath);
    QVERIFY(r.ok);
    QVERIFY(r.hasAlpha);
    QCOMPARE(r.image.format(), QImage::Format_ARGB32);
}

void TestCore::webpLosslessIsLossless()
{
    const QImage src = testimg::make(64, 48, true);
    EncodeOptions opt;
    opt.format = OutputFormat::WebP;
    opt.webpLossless = true;
    opt.quality = 10;  // must be ignored
    QCOMPARE(ImageEncoder::effectiveQuality(opt), 100);
    const QByteArray bytes = encodeToBytes(src, opt);
    QVERIFY(bytes.startsWith("RIFF"));
    QVERIFY(bytes.contains("VP8L"));
    QVERIFY(!bytes.contains("VP8 "));

    QImage decoded = QImage::fromData(bytes, "webp").convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(decoded.size(), src.size());
    // Compare unpremultiplied pixels; fully transparent pixels may have any RGB.
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x) {
            const QRgb a = src.pixel(x, y), b = decoded.pixel(x, y);
            QCOMPARE(qAlpha(b), qAlpha(a));
            if (qAlpha(a) != 0)
                QCOMPARE(b, a);
        }
}

void TestCore::webpLossyUsesVp8AndQuality()
{
    const QImage src = testimg::make(128, 96, false).convertToFormat(QImage::Format_RGB888);
    EncodeOptions opt;
    opt.format = OutputFormat::WebP;
    opt.webpLossless = false;
    opt.quality = 100;
    QCOMPARE(ImageEncoder::effectiveQuality(opt), 99);  // 100 would switch to lossless
    opt.quality = 10;
    const QByteArray low = encodeToBytes(src, opt);
    opt.quality = 95;
    const QByteArray high = encodeToBytes(src, opt);
    QVERIFY(low.contains("VP8 "));
    QVERIFY(!low.contains("VP8L"));
    QVERIFY(high.contains("VP8 "));
    QVERIFY2(high.size() > low.size(),
             qPrintable(QStringLiteral("q95=%1 q10=%2").arg(high.size()).arg(low.size())));

    // Lossy with alpha keeps an alpha channel.
    const QByteArray withAlpha = encodeToBytes(testimg::make(64, 48, true), opt);
    QVERIFY(withAlpha.contains("ALPH"));
    const QImage dec = QImage::fromData(withAlpha, "webp");
    QVERIFY(dec.hasAlphaChannel());
    QCOMPARE(qAlpha(dec.pixel(2, 2)), 0);
}

void TestCore::jpegQualityAndWhiteBackground()
{
    const QImage src = testimg::make(128, 96, false).convertToFormat(QImage::Format_RGB888);
    EncodeOptions opt;
    opt.format = OutputFormat::Jpeg;
    opt.quality = 10;
    const QByteArray low = encodeToBytes(src, opt);
    opt.quality = 95;
    const QByteArray high = encodeToBytes(src, opt);
    QVERIFY(low.startsWith("\xFF\xD8"));
    QVERIFY(high.size() > low.size());

    const QByteArray t = encodeToBytes(testimg::make(64, 48, true), opt);
    const QImage dec = QImage::fromData(t, "jpeg");
    QVERIFY(!dec.isNull());
    const QRgb px = dec.pixel(4, 24);  // fully transparent area
    QVERIFY2(qRed(px) > 245 && qGreen(px) > 245 && qBlue(px) > 245, "expected white background");
}

void TestCore::pngKeepsAlpha()
{
    const QImage src = testimg::make(64, 48, true);
    EncodeOptions opt;
    opt.format = OutputFormat::Png;
    const QImage dec = QImage::fromData(encodeToBytes(src, opt), "png")
                           .convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(dec, src);
}

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
