// End-to-end tests that run the real waifu2x-ncnn-vulkan on the local GPU.
// They only check that processing completes with correct sizes and formats;
// timing and memory measurements belong to the manual A9 checklist.
#include "TestImages.h"

#include "core/BatchRunner.h"
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
        return r;
    }
    return ProcessJob::run(src, s, cancel);
}

void TestPipeline::png2xOpaque()
{
    const QByteArray before = testimg::sha256(m_opaque);
    const ItemResult r = runOne(m_opaque, spec(OutputFormat::Png, 2));
    QVERIFY2(r.ok, qPrintable(r.error));
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
    QVERIFY2(r.ok, qPrintable(r.error));
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
    QVERIFY2(r.ok, qPrintable(r.error));
    QFile f(r.outputPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    QVERIFY(bytes.contains("VP8L"));
    QVERIFY(!bytes.contains("VP8 "));

    r = runOne(m_alpha, spec(OutputFormat::Jpeg, 2));
    QVERIFY2(r.ok, qPrintable(r.error));
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
    QVERIFY2(a.ok, qPrintable(a.error));
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
    QVERIFY(r.canceled);
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
    BatchCallbacks cb;
    cb.itemStarted = [&](int i) { started << i; };
    cb.itemFinished = [&](int i, const ItemResult &r) { finished << qMakePair(i, r.ok); };
    std::atomic_bool cancel{false};
    const BatchSummary s = runBatch(files, spec(OutputFormat::WebP, 2), cancel, cb);
    QCOMPARE(s.succeeded, 2);
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

QTEST_GUILESS_MAIN(TestPipeline)
#include "tst_pipeline.moc"
