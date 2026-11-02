// End-to-end tests that run the real waifu2x-ncnn-vulkan on the local GPU.
// They only check that processing completes with correct sizes and formats;
// timing and memory measurements belong to the manual A9 checklist.
#include "TestImages.h"

#include "core/BatchRunner.h"
#include "core/ImageLoader.h"
#include "core/ProcessJob.h"
#include "core/Waifu2xRunner.h"

#include <QDir>
#include <QDirIterator>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest>

#include <chrono>
#include <thread>

using namespace arx;

namespace {

// Failure messages keep the reason, the failing step and the engine stderr,
// so a failure seen once can be traced from the test log afterwards.
QString why(const ItemResult &r)
{
    return r.diagnostics.isEmpty() ? r.error : r.error + QLatin1Char('\n') + r.diagnostics;
}

QString why(const RunResult &r)
{
    return QStringLiteral("%1\nengine stderr:\n%2").arg(r.error, r.stderrText);
}

} // namespace

class TestPipeline : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void png2xOpaque();
    void webp4xLossyAlpha();
    void webpLosslessAndJpeg();
    void sameNameGetsNumbered();
    void corruptInputFailsCleanly();
    void missingModelIsReported();
    void cancelKillsProcess();
    void batchMixedContinuesAfterFailure();
    void batchPreconditionStops();
    void batchEngineFailureStops();
    void batchCanceledBeforeStart();
    void fourXIsTwoStage();
    void fourXNeedsUpscaleOnlyModel();
    void oneXDenoiseOnly();
    void stagesAreReportedInOrder();

private:
    JobSpec spec(OutputFormat f, int scale) const;
    QStringList leftovers() const;
    ItemResult runOne(const QString &src, const JobSpec &s);

    QTemporaryDir m_root;
    QString m_engineDir;   // copy of waifu2x under a CJK + space path
    QString m_srcDir;
    QString m_outDir;
    QString m_opaque;
    QString m_alpha;
};

void TestPipeline::initTestCase()
{
    QImageReader::setAllocationLimit(1024);
    const QString w2x = QStringLiteral(ARX_WAIFU2X_DIR);
    if (!QFileInfo::exists(w2x + QStringLiteral("/waifu2x-ncnn-vulkan.exe")))
        QSKIP("waifu2x not fetched; run tools/fetch-waifu2x.ps1");
    QVERIFY(m_root.isValid());

    // Engine, sources and outputs all live under CJK / Japanese / space paths.
    m_engineDir = m_root.filePath(QStringLiteral("アプリ 程式/waifu2x"));
    m_srcDir = m_root.filePath(QStringLiteral("來源 画像"));
    m_outDir = m_root.filePath(QStringLiteral("輸出 フォルダ"));
    QVERIFY(QDir().mkpath(m_engineDir + QStringLiteral("/models-cunet")));
    QVERIFY(QDir().mkpath(m_srcDir));
    QVERIFY(QDir().mkpath(m_outDir));
    QVERIFY(QFile::copy(w2x + QStringLiteral("/waifu2x-ncnn-vulkan.exe"),
                        m_engineDir + QStringLiteral("/waifu2x-ncnn-vulkan.exe")));
    QVERIFY(QFile::copy(w2x + QStringLiteral("/vcomp140.dll"),
                        m_engineDir + QStringLiteral("/vcomp140.dll")));
    const QDir models(w2x + QStringLiteral("/models-cunet"));
    for (const QString &f : models.entryList(QDir::Files))
        QVERIFY(QFile::copy(models.filePath(f), m_engineDir + QStringLiteral("/models-cunet/") + f));

    m_opaque = QDir(m_srcDir).filePath(QStringLiteral("線畫 テスト.jpg"));
    QVERIFY(testimg::make(96, 64, false).convertToFormat(QImage::Format_RGB32).save(m_opaque, "jpeg", 70));
    m_alpha = QDir(m_srcDir).filePath(QStringLiteral("透明 sample.png"));
    QVERIFY(testimg::make(80, 60, true).save(m_alpha, "png"));
}

JobSpec TestPipeline::spec(OutputFormat f, int scale) const
{
    JobSpec s;
    s.outputDir = m_outDir;
    s.waifu2x.exePath = m_engineDir + QStringLiteral("/waifu2x-ncnn-vulkan.exe");
    s.waifu2x.modelDir = m_engineDir + QStringLiteral("/models-cunet");
    s.waifu2x.noise = 1;
    s.waifu2x.scale = scale;
    s.waifu2x.tile = 0;
    s.encode.format = f;
    s.encode.quality = 90;
    return s;
}

QStringList TestPipeline::leftovers() const
{
    return QDir(m_outDir).entryList({QStringLiteral("*.tmp")}, QDir::Files | QDir::Hidden);
}

ItemResult TestPipeline::runOne(const QString &src, const JobSpec &s)
{
    std::atomic_bool cancel{false};
    const QString pre = ProcessJob::checkPreconditions(s);
    if (!pre.isEmpty()) {
        ItemResult r;
        r.error = pre;
        r.diagnostics = QStringLiteral("step: preconditions");
        return r;
    }
    return ProcessJob::run(src, s, cancel);
}

void TestPipeline::png2xOpaque()
{
    const QByteArray before = testimg::sha256(m_opaque);
    const ItemResult r = runOne(m_opaque, spec(OutputFormat::Png, 2));
    QVERIFY2(r.ok, qPrintable(why(r)));
    QCOMPARE(QFileInfo(r.outputPath).fileName(), QStringLiteral("線畫 テスト_2x.png"));
    QImageReader rd(r.outputPath);
    QCOMPARE(rd.format(), QByteArray("png"));
    QCOMPARE(rd.size(), QSize(192, 128));
    QCOMPARE(testimg::sha256(m_opaque), before);  // source untouched
    QVERIFY(leftovers().isEmpty());
}

void TestPipeline::webp4xLossyAlpha()
{
    const ItemResult r = runOne(m_alpha, spec(OutputFormat::WebP, 4));
    QVERIFY2(r.ok, qPrintable(why(r)));
    QFile f(r.outputPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    QVERIFY(bytes.contains("VP8 "));
    const QImage img = QImage::fromData(bytes, "webp");
    QCOMPARE(img.size(), QSize(320, 240));
    QVERIFY(img.hasAlphaChannel());
    QVERIFY(qAlpha(img.pixel(10, 120)) < 16);    // transparent quarter
    QVERIFY(qAlpha(img.pixel(300, 120)) > 240);  // opaque half
}

void TestPipeline::webpLosslessAndJpeg()
{
    JobSpec s = spec(OutputFormat::WebP, 2);
    s.encode.webpLossless = true;
    ItemResult r = runOne(m_alpha, s);
    QVERIFY2(r.ok, qPrintable(why(r)));
    QFile f(r.outputPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    QVERIFY(bytes.contains("VP8L"));
    QVERIFY(!bytes.contains("VP8 "));

    r = runOne(m_alpha, spec(OutputFormat::Jpeg, 2));
    QVERIFY2(r.ok, qPrintable(why(r)));
    QCOMPARE(QFileInfo(r.outputPath).suffix(), QStringLiteral("jpg"));
    const QImage j(r.outputPath);
    QCOMPARE(j.size(), QSize(160, 120));
    const QRgb px = j.pixel(5, 60);  // was fully transparent -> white
    QVERIFY(qRed(px) > 240 && qGreen(px) > 240 && qBlue(px) > 240);
}

void TestPipeline::sameNameGetsNumbered()
{
    const JobSpec s = spec(OutputFormat::Png, 2);
    const ItemResult a = runOne(m_opaque, s);  // png2xOpaque already made the first
    QVERIFY2(a.ok, qPrintable(why(a)));
    QCOMPARE(QFileInfo(a.outputPath).fileName(), QStringLiteral("線畫 テスト_2x (1).png"));
}

void TestPipeline::corruptInputFailsCleanly()
{
    const QString bad = QDir(m_srcDir).filePath(QStringLiteral("壊れた.png"));
    {
        QFile f(bad);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\x89PNG\r\n\x1a\n truncated");
    }
    const int before = QDir(m_outDir).entryList(QDir::Files | QDir::Hidden).size();
    const ItemResult r = runOne(bad, spec(OutputFormat::Png, 2));
    QVERIFY(!r.ok);
    QVERIFY(!r.error.isEmpty());
    QVERIFY2(r.diagnostics.startsWith(QStringLiteral("step: load")), qPrintable(r.diagnostics));
    qInfo() << "corrupt input error:" << r.error;
    QCOMPARE(QDir(m_outDir).entryList(QDir::Files | QDir::Hidden).size(), before);
}

void TestPipeline::missingModelIsReported()
{
    JobSpec s = spec(OutputFormat::Png, 2);
    s.waifu2x.modelDir = m_root.filePath(QStringLiteral("no-such/models-cunet"));
    const QString err = ProcessJob::checkPreconditions(s);
    QVERIFY(!err.isEmpty());
    qInfo() << "missing model error:" << err;

    // A model dir without the required noise file is also caught up front.
    const QString partial = m_root.filePath(QStringLiteral("partial/models-cunet"));
    QVERIFY(QDir().mkpath(partial));
    s.waifu2x.modelDir = partial;
    QVERIFY(!Waifu2xRunner::checkInstall(s.waifu2x).isEmpty());

    // The engine's model path buffer is 256 wide chars: reject longer install paths.
    JobSpec ok = spec(OutputFormat::Png, 2);
    QVERIFY(Waifu2xRunner::checkInstall(ok.waifu2x).isEmpty());
    const QString longDir = QStringLiteral("C:/") + QString(240, QLatin1Char('a')) + QStringLiteral("/models-cunet");
    ok.waifu2x.modelDir = longDir;
    // (checked before the folder itself, so it need not exist)
    const QString longErr = Waifu2xRunner::checkInstall(ok.waifu2x);
    QVERIFY2(longErr.contains(QStringLiteral("too long")), qPrintable(longErr));
}

void TestPipeline::cancelKillsProcess()
{
    // A bigger input so that inference is still running when we cancel.
    const QString big = QDir(m_srcDir).filePath(QStringLiteral("big.png"));
    QVERIFY(testimg::make(1600, 1200, false).save(big, "png"));
    std::atomic_bool cancel{false};
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        cancel.store(true);
    });
    QElapsedTimer timer;
    timer.start();
    const ItemResult r = ProcessJob::run(big, spec(OutputFormat::Png, 4), cancel);
    t.join();
    if (r.ok)
        QSKIP("inference finished before cancel; GPU too fast for this check");
    QVERIFY2(r.canceled, qPrintable(why(r)));
    QVERIFY(timer.elapsed() < 10000);
    QVERIFY(leftovers().isEmpty());
    QVERIFY(!QFileInfo::exists(QDir(m_outDir).filePath(QStringLiteral("big_4x.png"))));
}

void TestPipeline::batchMixedContinuesAfterFailure()
{
    const QString bad = QDir(m_srcDir).filePath(QStringLiteral("batch-bad.webp"));
    {
        QFile f(bad);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("RIFF....WEBPnope");
    }
    const QStringList files{m_opaque, bad, m_alpha};
    QList<int> started;
    QList<QPair<int, bool>> finished;
    QStringList reasons;
    BatchCallbacks cb;
    cb.itemStarted = [&](int i) { started << i; };
    cb.itemFinished = [&](int i, const ItemResult &r) {
        finished << qMakePair(i, r.ok);
        if (!r.ok)
            reasons << QStringLiteral("item %1: %2").arg(i).arg(why(r));
    };
    std::atomic_bool cancel{false};
    const BatchSummary s = runBatch(files, spec(OutputFormat::WebP, 2), cancel, cb);
    QVERIFY2(s.succeeded == 2, qPrintable(reasons.join(QLatin1Char('\n'))));
    QCOMPARE(s.failed, 1);
    QCOMPARE(s.notProcessed, 0);
    QVERIFY(!s.canceled);
    QVERIFY(s.fatalError.isEmpty());
    QCOMPARE(started, (QList<int>{0, 1, 2}));
    QCOMPARE(finished, (QList<QPair<int, bool>>{{0, true}, {1, false}, {2, true}}));
    QVERIFY(leftovers().isEmpty());
}

void TestPipeline::batchPreconditionStops()
{
    JobSpec s = spec(OutputFormat::Png, 2);
    s.outputDir = m_root.filePath(QStringLiteral("does-not-exist"));
    int calls = 0;
    BatchCallbacks cb;
    cb.itemStarted = [&](int) { ++calls; };
    std::atomic_bool cancel{false};
    const BatchSummary r = runBatch({m_opaque, m_alpha}, s, cancel, cb);
    QCOMPARE(r.succeeded, 0);
    QCOMPARE(r.failed, 0);
    QCOMPARE(r.notProcessed, 2);
    QVERIFY(!r.fatalError.isEmpty());
    QCOMPARE(calls, 0);
}

void TestPipeline::batchEngineFailureStops()
{
    // A file that exists but is not a valid executable: the engine cannot start.
    const QString fakeDir = m_root.filePath(QStringLiteral("fake-engine"));
    QVERIFY(QDir().mkpath(fakeDir));
    const QString fakeExe = fakeDir + QStringLiteral("/waifu2x-ncnn-vulkan.exe");
    {
        QFile f(fakeExe);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not an executable");
    }
    JobSpec s = spec(OutputFormat::Png, 2);
    s.waifu2x.exePath = fakeExe;
    std::atomic_bool cancel{false};
    const BatchSummary r = runBatch({m_opaque, m_alpha, m_opaque}, s, cancel, {});
    QCOMPARE(r.succeeded, 0);
    QCOMPARE(r.failed, 1);
    QCOMPARE(r.notProcessed, 2);
    QVERIFY(!r.fatalError.isEmpty());
    qInfo() << "engine failure:" << r.fatalError;
}

void TestPipeline::batchCanceledBeforeStart()
{
    std::atomic_bool cancel{true};
    const BatchSummary r = runBatch({m_opaque, m_alpha}, spec(OutputFormat::Png, 2), cancel, {});
    QVERIFY(r.canceled);
    QCOMPARE(r.notProcessed, 2);
    QCOMPARE(r.succeeded + r.failed, 0);
}

void TestPipeline::stagesAreReportedInOrder()
{
    // Every stage of the progress plan is reported, in order, with the pass number.
    const JobSpec s = spec(OutputFormat::Png, 4);
    const ItemPlan plan = ProcessJob::progressPlan(m_opaque, s);
    QCOMPARE(plan.weight, 96.0 * 64 * 16 / 1e6);

    QList<StageEvent> events;
    QStringList devices;
    BatchCallbacks cb;
    cb.deviceDetected = [&](const QString &d) { devices << d; };
    cb.stageStarted = [&](int index, const StageEvent &e) {
        QCOMPARE(index, 0);
        events << e;
    };
    QString reason;
    cb.itemFinished = [&](int, const ItemResult &res) { reason = why(res); };
    std::atomic_bool cancel{false};
    const BatchSummary r = runBatch({m_opaque}, s, cancel, cb);
    QVERIFY2(r.succeeded == 1, qPrintable(reason + QLatin1Char('\n') + r.fatalError));
    QCOMPARE(events.size(), plan.stages.size());
    for (int i = 0; i < events.size(); ++i) {
        QCOMPARE(events[i].stage, plan.stages[i].stage);
        QCOMPARE(events[i].pass, plan.stages[i].pass);
        QCOMPARE(events[i].passCount, 2);
    }
    // The engine names its GPU after each pass (measured speeds are kept per GPU).
    QCOMPARE(devices.size(), 2);
    QVERIFY(!devices.first().isEmpty());
    QCOMPARE(devices.first(), devices.last());
}

void TestPipeline::fourXIsTwoStage()
{
    // The pipeline's 4x PNG must equal running [-n 1 -s 2] then [-n -1 -s 2]
    // by hand on the same normalised input, and must differ from waifu2x -s 4.
    const ItemResult r = runOne(m_opaque, spec(OutputFormat::Png, 4));
    QVERIFY2(r.ok, qPrintable(why(r)));
    const QImage pipeline = QImage(r.outputPath).convertToFormat(QImage::Format_RGB888);
    QCOMPARE(pipeline.size(), QSize(384, 256));

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const LoadResult in = ImageLoader::load(m_opaque);
    QVERIFY(in.ok);
    const QString inPng = tmp.filePath(QStringLiteral("in.png"));
    QVERIFY(in.image.save(inPng, "png"));

    std::atomic_bool cancel{false};
    JobSpec s = spec(OutputFormat::Png, 2);
    Waifu2xParams first = s.waifu2x;   // -n 1 -s 2
    Waifu2xParams second = first;
    second.noise = -1;                 // -n -1 -s 2
    const RunResult r1 = Waifu2xRunner::run(first, inPng, tmp.filePath(QStringLiteral("p1.png")), cancel);
    QVERIFY2(r1.ok, qPrintable(QStringLiteral("manual pass 1: ") + why(r1)));
    const RunResult r2 = Waifu2xRunner::run(second, tmp.filePath(QStringLiteral("p1.png")),
                                            tmp.filePath(QStringLiteral("p2.png")), cancel);
    QVERIFY2(r2.ok, qPrintable(QStringLiteral("manual pass 2: ") + why(r2)));
    const QImage manual = QImage(tmp.filePath(QStringLiteral("p2.png"))).convertToFormat(QImage::Format_RGB888);
    QCOMPARE(manual, pipeline);

    Waifu2xParams direct = s.waifu2x;
    direct.scale = 4;                  // waifu2x's own -s 4 (denoise model twice)
    const RunResult r4 = Waifu2xRunner::run(direct, inPng, tmp.filePath(QStringLiteral("d4.png")), cancel);
    QVERIFY2(r4.ok, qPrintable(QStringLiteral("direct -s 4: ") + why(r4)));
    const QImage d4 = QImage(tmp.filePath(QStringLiteral("d4.png"))).convertToFormat(QImage::Format_RGB888);
    QCOMPARE(d4.size(), pipeline.size());
    QVERIFY2(d4 != pipeline, "4x must not be waifu2x's direct -s 4");

    // The intermediate 2x file is not left in the output folder.
    QVERIFY(leftovers().isEmpty());
}

void TestPipeline::fourXNeedsUpscaleOnlyModel()
{
    // A model folder with only the denoise+2x model is fine for 2x, but 4x
    // also needs scale2.0x_model for the second pass.
    const QString dir = m_root.filePath(QStringLiteral("only-noise1/models-cunet"));
    QVERIFY(QDir().mkpath(dir));
    for (const char *ext : {".param", ".bin"}) {
        const QString name = QStringLiteral("noise1_scale2.0x_model") + QLatin1String(ext);
        QVERIFY(QFile::copy(m_engineDir + QStringLiteral("/models-cunet/") + name, dir + QLatin1Char('/') + name));
    }
    JobSpec s = spec(OutputFormat::Png, 2);
    s.waifu2x.modelDir = dir;
    QVERIFY(Waifu2xRunner::checkInstall(s.waifu2x).isEmpty());
    s.waifu2x.scale = 4;
    const QString err = Waifu2xRunner::checkInstall(s.waifu2x);
    QVERIFY2(err.contains(QDir::toNativeSeparators(QStringLiteral("/scale2.0x_model."))), qPrintable(err));
}

void TestPipeline::oneXDenoiseOnly()
{
    // 1x keeps the size and runs the noiseN_model once.
    const ItemResult r = runOne(m_opaque, spec(OutputFormat::Png, 1));
    QVERIFY2(r.ok, qPrintable(why(r)));
    QCOMPARE(QFileInfo(r.outputPath).fileName(), QStringLiteral("線畫 テスト_1x.png"));
    const QImage out = QImage(r.outputPath).convertToFormat(QImage::Format_RGB888);
    QCOMPARE(out.size(), QSize(96, 64));
    const LoadResult in = ImageLoader::load(m_opaque);
    QVERIFY(in.ok);
    QVERIFY2(out != in.image.convertToFormat(QImage::Format_RGB888), "1x must actually denoise");
    QVERIFY(leftovers().isEmpty());

    // 1x needs the 1x model files: a folder with only 2x models is reported.
    const QString dir = m_root.filePath(QStringLiteral("only-2x/models-cunet"));
    QVERIFY(QDir().mkpath(dir));
    for (const char *ext : {".param", ".bin"}) {
        const QString name = QStringLiteral("noise1_scale2.0x_model") + QLatin1String(ext);
        QVERIFY(QFile::copy(m_engineDir + QStringLiteral("/models-cunet/") + name, dir + QLatin1Char('/') + name));
    }
    JobSpec s = spec(OutputFormat::Png, 1);
    s.waifu2x.modelDir = dir;
    const QString err = Waifu2xRunner::checkInstall(s.waifu2x);
    QVERIFY2(err.contains(QStringLiteral("noise1_model.")), qPrintable(err));

    // 1x without denoise is refused up front (nothing would be done).
    s = spec(OutputFormat::Png, 1);
    s.waifu2x.noise = -1;
    QVERIFY(!ProcessJob::checkPreconditions(s).isEmpty());
}

QTEST_GUILESS_MAIN(TestPipeline)
#include "tst_pipeline.moc"
