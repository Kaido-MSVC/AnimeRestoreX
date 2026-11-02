#include "TestImages.h"

#include "core/AppSettings.h"
#include "core/FolderScanner.h"
#include "core/Language.h"
#include "core/ImageEncoder.h"
#include "core/ImageLimits.h"
#include "core/ImageLoader.h"
#include "core/OutputNaming.h"
#include "core/ProcessJob.h"
#include "core/Progress.h"
#include "core/Waifu2xRunner.h"

#include <QBuffer>
#include <QDir>
#include <QImageReader>
#include <QImageWriter>
#include <QSettings>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <limits>

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
    void folderScanTopLevelNaturalOrder();
    void sizeMessagesIncludeValues();
    void languageFromUiTag_data();
    void languageFromUiTag();
    void languagePriority();
    void settingsSeparateKeysAndValidation();
    void folderFallbackPerItem();
    void waifu2xPasses();
    void progressPlanFollowsPasses();
    void estimatorStageProgressAndCalibration();
    void estimatorBatchWeightAndRemaining();
    void estimatorUsesEngineFraction();
    void progressRatesPersist();
    void deviceNameFromStderr();
    void estimatorWaitsForDevice();

private:
    QByteArray encodeToBytes(const QImage &img, const EncodeOptions &opt);
    QTemporaryDir m_dir;
};

void TestCore::initTestCase()
{
    QImageReader::setAllocationLimit(1024);
    QVERIFY(m_dir.isValid());
    // Keep test settings out of the real registry.
    QCoreApplication::setOrganizationName(QStringLiteral("Kaido-MSVC-Test"));
    QCoreApplication::setApplicationName(QStringLiteral("AnimeRestoreX-tst_core"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.filePath(QStringLiteral("settings")));
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

void TestCore::folderScanTopLevelNaturalOrder()
{
    const QString dir = m_dir.filePath(QStringLiteral("掃描 フォルダ"));
    QVERIFY(QDir().mkpath(dir + QStringLiteral("/sub")));
    for (const char *name : {"b10.png", "b2.PNG", "a.jpg", "c.webp", "d.jpeg", "x.bmp", "notes.txt",
                             "sub/inner.png"}) {
        QFile f(QDir(dir).filePath(QString::fromLatin1(name)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    QStringList names;
    for (const QString &p : scanFolder(dir))
        names << QFileInfo(p).fileName();
    QCOMPARE(names, (QStringList{QStringLiteral("a.jpg"), QStringLiteral("b2.PNG"),
                                 QStringLiteral("b10.png"), QStringLiteral("c.webp"),
                                 QStringLiteral("d.jpeg")}));
    QVERIFY(isSupportedImageFile(QStringLiteral("X.WEBP")));
    QVERIFY(!isSupportedImageFile(QStringLiteral("x.gif")));
}

void TestCore::sizeMessagesIncludeValues()
{
    // The real-world case reported in stage 2: 4800 x 6416 = 30.8 MP.
    const QString in = limits::Messages::inputTooLarge({4800, 6416});
    QVERIFY2(in.contains(QStringLiteral("4800 x 6416")), qPrintable(in));
    QVERIFY2(in.contains(QStringLiteral("30.8 MP")), qPrintable(in));
    QVERIFY2(in.contains(QStringLiteral("4.5 MP")), qPrintable(in));

    const QString side = limits::Messages::forCheck(limits::SizeCheck::SideTooLong, {4500, 1000}, 4,
                                                    OutputFormat::WebP);
    QVERIFY2(side.contains(QStringLiteral("18000")) && side.contains(QStringLiteral("16383"))
                 && side.contains(QStringLiteral("WebP")),
             qPrintable(side));

    const QString out = limits::Messages::outputTooLarge({5000, 5000}, 2);
    QVERIFY2(out.contains(QStringLiteral("100.0 MP")) && out.contains(QStringLiteral("75.0 MP")),
             qPrintable(out));

    // Through the loader (rejected from the header, before decoding).
    const QString path = m_dir.filePath(QStringLiteral("big.png"));
    QImage big(3000, 1501, QImage::Format_RGB32);
    big.fill(Qt::white);
    QVERIFY(big.save(path, "png"));
    const LoadResult r = ImageLoader::load(path);
    QVERIFY(!r.ok);
    QVERIFY2(r.error.contains(QStringLiteral("4.5 MP")) && r.error.contains(QStringLiteral("3000 x 1501")),
             qPrintable(r.error));
}

void TestCore::languageFromUiTag_data()
{
    QTest::addColumn<QString>("tag");
    QTest::addColumn<int>("expected");
    const int en = int(Language::English), zh = int(Language::TraditionalChinese),
              ja = int(Language::Japanese);
    QTest::newRow("zh-TW") << "zh-TW" << zh;
    QTest::newRow("zh-HK") << "zh-HK" << zh;
    QTest::newRow("zh-MO") << "zh-MO" << zh;
    QTest::newRow("zh-Hant") << "zh-Hant" << zh;
    QTest::newRow("zh-Hant-TW") << "zh-Hant-TW" << zh;
    QTest::newRow("zh_TW underscore") << "zh_TW" << zh;
    QTest::newRow("ja") << "ja" << ja;
    QTest::newRow("ja-JP") << "ja-JP" << ja;
    QTest::newRow("en-US") << "en-US" << en;
    QTest::newRow("en-GB") << "en-GB" << en;
    QTest::newRow("zh-CN") << "zh-CN" << en;
    QTest::newRow("zh-SG") << "zh-SG" << en;
    QTest::newRow("zh-Hans") << "zh-Hans" << en;
    QTest::newRow("zh-Hans-HK") << "zh-Hans-HK" << en;  // explicit script wins
    QTest::newRow("bare zh") << "zh" << en;
    QTest::newRow("ko-KR") << "ko-KR" << en;
    QTest::newRow("fr-FR") << "fr-FR" << en;
    QTest::newRow("empty") << "" << en;
    QTest::newRow("garbage") << "???" << en;
}

void TestCore::languageFromUiTag()
{
    QFETCH(QString, tag);
    QFETCH(int, expected);
    QCOMPARE(int(arx::languageFromUiTag(tag)), expected);
}

void TestCore::languagePriority()
{
    // Manual setting wins over the system language.
    QCOMPARE(effectiveLanguage(Language::Japanese, QStringLiteral("zh-TW")), Language::Japanese);
    QCOMPARE(effectiveLanguage(Language::English, QStringLiteral("ja-JP")), Language::English);
    // No manual setting: system rule; the primary language only.
    QCOMPARE(effectiveLanguage(std::nullopt, QStringLiteral("zh-TW")), Language::TraditionalChinese);
    QCOMPARE(effectiveLanguage(std::nullopt, QStringLiteral("de-DE")), Language::English);
    QCOMPARE(effectiveLanguage(std::nullopt, QString()), Language::English);
    // Invalid stored values are not a manual setting.
    QVERIFY(!languageFromCode(QStringLiteral("zh_CN")).has_value());
    QVERIFY(!languageFromCode(QString()).has_value());
    QCOMPARE(languageFromCode(QStringLiteral("ja")), std::optional<Language>(Language::Japanese));
    // The real Windows query returns something non-empty on this machine.
    qInfo() << "Windows primary UI language:" << windowsPrimaryUiLanguage();
    QVERIFY(!windowsPrimaryUiLanguage().isEmpty());
}

void TestCore::settingsSeparateKeysAndValidation()
{
    const QString a = m_dir.filePath(QStringLiteral("來源 A"));
    const QString b = m_dir.filePath(QStringLiteral("出力 B"));
    QVERIFY(QDir().mkpath(a));
    QVERIFY(QDir().mkpath(b));
    {
        AppSettings s;
        QVERIFY(!s.manualLanguage().has_value());  // nothing stored yet
        s.setSourceDir(a);
        s.setOutputDir(b);
        s.setNoiseLevel(3);
        s.setTileSize(200);
        s.setManualLanguage(Language::Japanese);
        QVERIFY(s.sync());
    }
    {
        AppSettings s;  // "restart"
        QCOMPARE(s.sourceDir(), a);
        QCOMPARE(s.outputDir(), b);
        QCOMPARE(s.noiseLevel(), 3);
        QCOMPARE(s.tileSize(), 200);
        QCOMPARE(s.manualLanguage(), std::optional<Language>(Language::Japanese));
    }
    QSettings raw;
    QVERIFY(raw.contains(QStringLiteral("paths/sourceDir")));
    QVERIFY(raw.contains(QStringLiteral("paths/outputDir")));
    QVERIFY(raw.value(QStringLiteral("paths/sourceDir")) != raw.value(QStringLiteral("paths/outputDir")));

    // Corrupt values fall back to safe defaults.
    raw.setValue(QStringLiteral("model/noiseLevel"), 9);
    raw.setValue(QStringLiteral("model/tileSize"), QStringLiteral("abc"));
    raw.setValue(QStringLiteral("ui/language"), QStringLiteral("klingon"));
    raw.sync();
    AppSettings s;
    QCOMPARE(s.noiseLevel(), AppSettings::kDefaultNoise);
    QCOMPARE(s.tileSize(), AppSettings::kDefaultTile);
    QVERIFY(!s.manualLanguage().has_value());
    raw.clear();
}

void TestCore::folderFallbackPerItem()
{
    const QString ok = m_dir.filePath(QStringLiteral("有效"));
    QVERIFY(QDir().mkpath(ok));
    bool fellBack = true;
    QCOMPARE(resolveFolder(ok, &fellBack), QDir::cleanPath(ok));
    QVERIFY(!fellBack);
    const QString gone = m_dir.filePath(QStringLiteral("消えた"));
    QCOMPARE(resolveFolder(gone, &fellBack), defaultImageFolder());
    QVERIFY(fellBack);
    QVERIFY(QFileInfo(defaultImageFolder()).isDir());
}

void TestCore::waifu2xPasses()
{
    Waifu2xParams p;
    p.noise = 1;
    p.tile = 200;

    p.scale = 2;
    QList<Waifu2xParams> ps = Waifu2xRunner::passes(p);
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps[0].scale, 2);
    QCOMPARE(ps[0].noise, 1);
    QCOMPARE(ps[0].tile, 200);

    // 4x: denoise + 2x, then upscale-only 2x (denoise runs once).
    p.scale = 4;
    ps = Waifu2xRunner::passes(p);
    QCOMPARE(ps.size(), 2);
    QCOMPARE(ps[0].scale, 2);
    QCOMPARE(ps[0].noise, 1);
    QCOMPARE(ps[1].scale, 2);
    QCOMPARE(ps[1].noise, -1);
    QCOMPARE(ps[1].tile, 200);
    const QStringList args = Waifu2xRunner::arguments(ps[1], QStringLiteral("a.png"), QStringLiteral("b.png"));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("-n")) + 1), QStringLiteral("-1"));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("-s")) + 1), QStringLiteral("2"));

    // No denoise at all: both passes upscale only.
    p.noise = -1;
    ps = Waifu2xRunner::passes(p);
    QCOMPARE(ps.size(), 2);
    QCOMPARE(ps[0].noise, -1);
    QCOMPARE(ps[1].noise, -1);

    // 1x (v1.1): one denoise-only pass with the noiseN_model.
    p.noise = 2;
    p.scale = 1;
    ps = Waifu2xRunner::passes(p);
    QCOMPARE(ps.size(), 1);
    QCOMPARE(ps[0].scale, 1);
    QCOMPARE(ps[0].noise, 2);
    QCOMPARE(Waifu2xRunner::modelBaseName(ps[0]), QStringLiteral("noise2_model"));
    const QStringList args1 = Waifu2xRunner::arguments(ps[0], QStringLiteral("a.png"), QStringLiteral("b.png"));
    QCOMPARE(args1.at(args1.indexOf(QStringLiteral("-s")) + 1), QStringLiteral("1"));
    // 1x without denoise does nothing and is rejected before any file check.
    p.noise = -1;
    QVERIFY(Waifu2xRunner::checkInstall(p).contains(QStringLiteral("1x")));
    QCOMPARE(limits::check({10, 10}, 1, OutputFormat::Png), limits::SizeCheck::Ok);

    // Defaults decided after A9.
    QCOMPARE(AppSettings::kDefaultNoise, 1);
    QCOMPARE(AppSettings::kDefaultTile, 0);
}

void TestCore::progressPlanFollowsPasses()
{
    const QString img = m_dir.filePath(QStringLiteral("plan 計画.png"));
    QVERIFY(testimg::make(200, 100, false).save(img, "png"));  // 0.02 MP
    JobSpec spec;
    spec.waifu2x.noise = 1;
    spec.waifu2x.scale = 4;
    spec.encode.format = OutputFormat::WebP;
    spec.encode.webpLossless = true;

    ItemPlan plan = ProcessJob::progressPlan(img, spec);
    QCOMPARE(plan.stages.size(), 4);  // load, two inference passes, encode
    QCOMPARE(plan.stages[0].stage, Stage::Load);
    QCOMPARE(plan.stages[0].megapixels, 0.02);
    QCOMPARE(plan.stages[1].stage, Stage::Inference);
    QCOMPARE(plan.stages[1].pass, 0);
    QCOMPARE(plan.stages[1].rateKey, QStringLiteral("waifu2x/noise1_scale2.0x_model"));
    QCOMPARE(plan.stages[1].megapixels, 0.08);   // 2x output
    QCOMPARE(plan.stages[2].pass, 1);
    QCOMPARE(plan.stages[2].rateKey, QStringLiteral("waifu2x/scale2.0x_model"));
    QCOMPARE(plan.stages[2].megapixels, 0.32);   // 4x output
    QCOMPARE(plan.stages[3].stage, Stage::Encode);
    QCOMPARE(plan.stages[3].rateKey, QStringLiteral("encode/webp-lossless"));
    QCOMPARE(plan.weight, 0.32);

    spec.waifu2x.scale = 1;
    plan = ProcessJob::progressPlan(img, spec);
    QCOMPARE(plan.stages.size(), 3);
    QCOMPARE(plan.stages[1].rateKey, QStringLiteral("waifu2x/noise1_model"));
    QCOMPARE(plan.weight, 0.02);

    // Unreadable header: size unknown, the estimator uses an average weight.
    plan = ProcessJob::progressPlan(m_dir.filePath(QStringLiteral("missing.png")), spec);
    QCOMPARE(plan.weight, 0.0);
}

namespace {

// One 2x item: load 1 MP (0.1 s/MP), inference 4 MP (0.5 s/MP + 1 s), encode 4 MP (0.25 s/MP).
// Expected: 0.1 + 3 + 1 = 4.1 s.
ItemPlan testPlan(double inMp)
{
    ItemPlan p;
    p.weight = inMp * 4;
    p.stages << StagePlan{Stage::Load, 0, QStringLiteral("load"), inMp, 0.1, 0}
             << StagePlan{Stage::Inference, 0, QStringLiteral("model"), inMp * 4, 0.5, 1.0}
             << StagePlan{Stage::Encode, 0, QStringLiteral("encode"), inMp * 4, 0.25, 0};
    return p;
}

} // namespace

void TestCore::estimatorStageProgressAndCalibration()
{
    ProgressEstimator e;
    e.startBatch({testPlan(1.0)}, 0);
    e.itemStarted(0, 0);
    e.stageStarted(0, {Stage::Load, 0, 1}, 0);
    e.stageStarted(0, {Stage::Inference, 0, 1}, 100);  // load took 0.1 s as expected

    // Halfway through the 3 s inference stage: (0.1 + 1.5) / 4.1 of the item.
    ProgressSnapshot s = e.snapshot(1600);
    QCOMPARE(s.stage, Stage::Inference);
    QCOMPARE(s.passCount, 1);
    QVERIFY(qAbs(s.itemFraction - 1.6 / 4.1) < 1e-3);
    QCOMPARE(s.remainingSec, -1.0);  // no inference speed measured yet

    // A stage that runs far longer than expected never shows as complete,
    // and the progress keeps rising.
    const double late1 = e.snapshot(20000).itemFraction;
    const double late2 = e.snapshot(60000).itemFraction;
    QVERIFY(late1 < 0.99 && late2 <= 0.99 && late2 >= late1);

    // Inference really took 7 s (1 s overhead + 6 s for 4 MP = 1.5 s/MP).
    e.stageStarted(0, {Stage::Encode, 0, 1}, 7100);
    QVERIFY(e.hasMeasuredInference());
    QCOMPARE(e.learnedRates().value(QStringLiteral("model")), 1.5);
    QVERIFY(e.snapshot(7100).itemFraction >= late2);  // never goes back
    e.itemFinished(0, true, 8100);
    QCOMPARE(e.learnedRates().value(QStringLiteral("encode")), 0.25);
    QCOMPARE(e.snapshot(8100).batchFraction, 1.0);

    // The next measurement is blended with the previous one.
    e.startBatch({testPlan(1.0)}, 0);
    e.itemStarted(0, 0);
    e.stageStarted(0, {Stage::Inference, 0, 1}, 0);
    e.stageStarted(0, {Stage::Encode, 0, 1}, 3000);  // 0.5 s/MP this time
    QCOMPARE(e.learnedRates().value(QStringLiteral("model")), 1.0);

    // A failed or canceled item does not count its interrupted stage.
    e.startBatch({testPlan(1.0)}, 0);
    e.itemStarted(0, 0);
    e.stageStarted(0, {Stage::Inference, 0, 1}, 0);
    e.itemFinished(0, false, 100);
    QCOMPARE(e.learnedRates().value(QStringLiteral("model")), 1.0);
}

void TestCore::estimatorBatchWeightAndRemaining()
{
    ProgressEstimator e;
    e.setLearnedRates({{QStringLiteral("model"), 0.5}, {QStringLiteral("bad"), -1.0}});
    QVERIFY(!e.learnedRates().contains(QStringLiteral("bad")));
    // A small and a 3x larger item: the batch is weighted by output pixels.
    e.startBatch({testPlan(1.0), testPlan(3.0)}, 0);
    QVERIFY(e.hasMeasuredInference());
    ProgressSnapshot s = e.snapshot(0);
    QCOMPARE(s.index, -1);
    QVERIFY(qAbs(s.remainingSec - (4.1 + 0.3 + 7.0 + 3.0)) < 1e-6);

    e.itemStarted(0, 0);
    e.stageStarted(0, {Stage::Load, 0, 1}, 0);
    e.itemFinished(0, true, 100);  // finished early (e.g. tiny result)
    s = e.snapshot(100);
    QCOMPARE(s.batchFraction, 0.25);  // 4 of 16 output MP
    QVERIFY(qAbs(s.remainingSec - 10.3) < 1e-6);

    // An item whose size is unknown counts as an average one.
    ItemPlan unknown = testPlan(1.0);
    unknown.weight = 0;
    e.startBatch({testPlan(1.0), unknown}, 0);
    e.itemStarted(0, 0);
    e.itemFinished(0, false, 10);
    QCOMPARE(e.snapshot(10).batchFraction, 0.5);

    // An unmeasured model uses its default scaled by the measured machine speed.
    ProgressEstimator f;
    f.setLearnedRates({{QStringLiteral("model"), 1.0}});  // twice the default 0.5
    ItemPlan other = testPlan(1.0);
    other.stages[1].rateKey = QStringLiteral("other-model");
    f.startBatch({testPlan(1.0), other}, 0);
    QCOMPARE(f.stageSeconds(other.stages[1]), 1.0 + 4 * 1.0);
}

void TestCore::estimatorUsesEngineFraction()
{
    // An engine that reports real progress overrides the time estimate.
    ProgressEstimator e;
    e.startBatch({testPlan(1.0)}, 0);
    e.itemStarted(0, 0);
    e.stageStarted(0, {Stage::Inference, 0, 1}, 0);
    e.stageFraction(0, 0.5, 100);
    ProgressSnapshot s = e.snapshot(100);
    QVERIFY(qAbs(s.itemFraction - (0.1 + 0.5 * 3.0) / 4.1) < 1e-3);
    e.stageFraction(0, 0.2, 200);  // never goes back
    QVERIFY(e.snapshot(200).itemFraction >= s.itemFraction);
    e.stageFraction(1, 0.9, 200);  // other item: ignored
    QVERIFY(qAbs(e.snapshot(200).itemFraction - s.itemFraction) < 1e-9);
}

void TestCore::progressRatesPersist()
{
    const QString gtx = QStringLiteral("NVIDIA GeForce GTX 1060 6GB");
    const QString other = QStringLiteral("AMD Radeon RX 7600 / test");  // "/" must not split groups
    {
        AppSettings s;
        s.setProgressRates(gtx, {{QStringLiteral("waifu2x/noise1_scale2.0x_model"), 0.42},
                                 {QStringLiteral("encode/png"), 0.1},
                                 {QStringLiteral("broken"), std::numeric_limits<double>::infinity()}});
        s.setProgressRates(other, {{QStringLiteral("waifu2x/noise1_scale2.0x_model"), 0.2}});
        s.setProgressDevice(gtx);
        QVERIFY(s.sync());
    }
    {
        QSettings raw;
        raw.setValue(QStringLiteral("progress/rates/") + gtx + QStringLiteral("/junk"), QStringLiteral("abc"));
    }
    AppSettings s;
    QCOMPARE(s.progressDevice(), gtx);
    const QHash<QString, double> rates = s.progressRates(gtx);
    QCOMPARE(rates.size(), 2);
    QCOMPARE(rates.value(QStringLiteral("waifu2x/noise1_scale2.0x_model")), 0.42);
    QCOMPARE(rates.value(QStringLiteral("encode/png")), 0.1);
    // Each GPU keeps its own speeds; an unknown GPU has none.
    QCOMPARE(s.progressRates(other).value(QStringLiteral("waifu2x/noise1_scale2.0x_model")), 0.2);
    QCOMPARE(s.progressRates(other).size(), 1);
    QVERIFY(s.progressRates(QStringLiteral("Intel Arc A770")).isEmpty());
}

void TestCore::deviceNameFromStderr()
{
    // Lines as printed by waifu2x-ncnn-vulkan 20250915 on the GTX 1060 (one line per property).
    const QByteArray one =
        "[0 NVIDIA GeForce GTX 1060 6GB]  queueC=2[8]  queueT=1[2]\r\n"
        "[0 NVIDIA GeForce GTX 1060 6GB]  fp16-p/s/u/a=1/1/1/0  int8-p/s/u/a=1/1/1/1\r\n"
        "[0 NVIDIA GeForce GTX 1060 6GB]  subgroup=32(32~32)  ops=1/1/1/1/1/1/1/1/1/1\r\n"
        "decode image x failed\r\n";
    QCOMPARE(Waifu2xRunner::deviceName(one), QStringLiteral("NVIDIA GeForce GTX 1060 6GB"));
    const QByteArray two =
        "[1 AMD Radeon(TM) Graphics]  queueC=1[2]\n"
        "[0 NVIDIA GeForce RTX 4060]  queueC=2[8]\n";
    QCOMPARE(Waifu2xRunner::deviceName(two),
             QStringLiteral("NVIDIA GeForce RTX 4060 + AMD Radeon(TM) Graphics"));
    QVERIFY(Waifu2xRunner::deviceName("invalid gpu device\n").isEmpty());
}

void TestCore::estimatorWaitsForDevice()
{
    // Speeds of a GPU that is not confirmed yet give no remaining time.
    ProgressEstimator e;
    e.setLearnedRates({{QStringLiteral("model"), 0.5}});
    e.setDeviceConfirmed(false);
    e.startBatch({testPlan(1.0)}, 0);
    QCOMPARE(e.snapshot(0).remainingSec, -1.0);
    e.setDeviceConfirmed(true);
    QVERIFY(e.snapshot(0).remainingSec > 0);
}

QTEST_GUILESS_MAIN(TestCore)
#include "tst_core.moc"
