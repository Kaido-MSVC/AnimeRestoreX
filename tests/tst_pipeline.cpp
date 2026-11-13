// Pipeline tests (ProcessJob + BatchRunner) on a fake Backend: the real models/manifest.json,
// a nearest-neighbour "model" and scripted engine errors (OOM, device lost, cancel), so the
// error handling runs without a GPU. One extra test uses the real ONNX Runtime and
// models next to the test exe and is skipped when they are not deployed.
#include "TestImages.h"

#include "core/BatchRunner.h"
#include "core/ImageLoader.h"
#include "core/ProcessJob.h"
#include "core/inference/EngineHost.h"

#include <QCoreApplication>
#include <QDir>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest>

#include <map>
#include <memory>

using namespace arx;

namespace {

QString why(const ItemResult &r)
{
    return r.diagnostics.isEmpty() ? r.error : r.error + QLatin1Char('\n') + r.diagnostics;
}

// Shared script for all sessions of a FakeBackend (call numbers count every Run).
struct Script {
    int calls = 0;
    int failAtCall = -1;               // 1-based
    core::Status failWith;
    std::atomic_bool *cancel = nullptr;
    int cancelAtCall = -1;             // sets *cancel during that Run
    std::vector<int> tiles;            // tile of every Run
    int failAtCall2 = -1;              // second failure, same error
};

// Nearest-neighbour upscale of the tile, as a model with the manifest's scale / offset would.
struct FakeEngine : core::ITileEngine {
    core::TileParams p;
    Script &script;
    FakeEngine(core::TileParams pp, Script &s) : p(pp), script(s) {}
    core::Status run(const float *in, int T, int outSide, std::vector<float> &out) override
    {
        ++script.calls;
        script.tiles.push_back(T);
        if (script.calls == script.failAtCall || script.calls == script.failAtCall2) {
            out.clear();
            return script.failWith;
        }
        if (script.cancel && script.calls == script.cancelAtCall)
            script.cancel->store(true);
        out.assign(size_t(3) * outSide * outSide, 0.f);
        for (int c = 0; c < 3; ++c)
            for (int y = 0; y < outSide; ++y)
                for (int x = 0; x < outSide; ++x)
                    out[(size_t(c) * outSide + y) * outSide + x] =
                        in[(size_t(c) * T + (y + p.offset) / p.scale) * T + (x + p.offset) / p.scale];
        return core::Status::success();
    }
};

class FakeBackend : public core::Backend {
public:
    FakeBackend()
    {
        loadStatus = core::ModelContract::load(std::filesystem::path(ARX_TEST_MANIFEST), m_contract);
    }

    // Knobs.
    core::Status loadStatus;
    core::Status runtime;
    std::string badHashId;     // verify() fails for this model
    uint64_t vramMiB = 6000;
    Script script;
    core::Status rebuildFailure;   // acquire() after the first releaseAll() fails with this

    // Observations.
    int releases = 0;
    int terminates = 0;
    int beginJobs = 0;
    int maxSessions = 0;
    int sessionsCreated = 0;
    core::ExecutionDevice device = core::ExecutionDevice::Dml;
    std::vector<std::string> acquired;

    core::Status runtimeReady() override { return runtime; }
    core::Status ready() override { return runtime.ok() ? loadStatus : runtime; }
    const core::ModelContract &contract() const override { return m_contract; }
    core::Status verify(const core::ModelInfo &m) override
    {
        if (m.id == badHashId)
            return core::Status::fail(core::ErrorKind::HashMismatch, "sha256 mismatch: " + m.file);
        return core::Status::success();
    }
    core::Status beginJob(core::ExecutionDevice d, int n) override
    {
        ++beginJobs;
        device = d;
        maxSessions = n;
        return core::Status::success();
    }
    core::Status acquire(const core::ModelInfo &m, core::ITileEngine *&engine) override
    {
        acquired.push_back(m.id);
        engine = nullptr;
        if (releases > 0 && !rebuildFailure.ok())
            return rebuildFailure;
        auto &e = m_sessions[m.id];
        if (!e)
            ++sessionsCreated;
        if (!e)
            e = std::make_unique<FakeEngine>(core::ModelContract::tileParams(m), script);
        engine = e.get();
        return core::Status::success();
    }
    void releaseAll() override
    {
        ++releases;
        m_sessions.clear();
    }
    void requestTerminate() override { ++terminates; }
    std::string deviceName(core::ExecutionDevice d) override
    {
        return d == core::ExecutionDevice::Cpu ? "CPU" : "Fake GPU 6GB";
    }
    uint64_t availableVramMiB() override { return vramMiB; }

private:
    core::ModelContract m_contract;
    std::map<std::string, std::unique_ptr<FakeEngine>> m_sessions;
};

int countFiles(const QString &dir)
{
    return int(QDir(dir).entryList(QDir::Files | QDir::Hidden).size());
}

} // namespace

class TestPipeline : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void png2xOpaque();
    void ttaThroughTheJob();
    void webp4xLossyAlpha();
    void webpLosslessAndJpeg();
    void sameNameGetsNumbered();
    void sameFolderAsSource();
    void corruptInputFailsCleanly();
    void outputFolderGoneFailsCleanly();
    void hashMismatchStopsBeforeFirstImage();
    void runtimeMissingStops();
    void cancelDuringInference();
    void oomRetriesWithSmallerTile();
    void deviceLostRecoversWithSmallerTile();
    void recoveryFailureStopsBatch_data();
    void recoveryFailureStopsBatch();
    void failureAfterRecoveryStopsBatch();
    void outputFailureAfterRecoveryKeepsFallback();
    void oomManualTileFailsItemOnly();
    void deviceLostManualTileStopsBatch();
    void smallGpuBudgetUses160();
    void cpuDevice();
    void batchMixedContinuesAfterFailure();
    void batchPreconditionStops();
    void batchCanceledBeforeStart();
    void stagesAreReportedInOrder();
    void oneXDenoiseOnly();
    void realRuntime2x();

private:
    JobSpec spec(OutputFormat f, int scale) const;
    QStringList leftovers() const;
    ItemResult runOne(const QString &src, const JobSpec &s, FakeBackend &b);

    QTemporaryDir m_root;
    QString m_srcDir;
    QString m_outDir;
    QString m_opaque;
    QString m_alpha;
    QString m_big;       // several tiles at 256
};

void TestPipeline::initTestCase()
{
    QImageReader::setAllocationLimit(1024);
    QVERIFY(m_root.isValid());
    FakeBackend probe;
    QVERIFY2(probe.loadStatus.ok(), probe.loadStatus.message.c_str());

    // Sources and outputs live under CJK / Japanese / space paths.
    m_srcDir = m_root.filePath(QStringLiteral("來源 画像"));
    m_outDir = m_root.filePath(QStringLiteral("輸出 フォルダ"));
    QVERIFY(QDir().mkpath(m_srcDir));
    QVERIFY(QDir().mkpath(m_outDir));

    m_opaque = QDir(m_srcDir).filePath(QStringLiteral("線畫 テスト.jpg"));
    QVERIFY(testimg::make(96, 64, false).convertToFormat(QImage::Format_RGB32).save(m_opaque, "jpeg", 70));
    m_alpha = QDir(m_srcDir).filePath(QStringLiteral("透明 sample.png"));
    QVERIFY(testimg::make(80, 60, true).save(m_alpha, "png"));
    m_big = QDir(m_srcDir).filePath(QStringLiteral("big.png"));
    QVERIFY(testimg::make(640, 480, false).save(m_big, "png"));
}

void TestPipeline::init()
{
    QVERIFY(leftovers().isEmpty());
}

JobSpec TestPipeline::spec(OutputFormat f, int scale) const
{
    JobSpec s;
    s.outputDir = m_outDir;
    s.inference.noise = 1;
    s.inference.scale = scale;
    s.inference.tile = 0;
    s.encode.format = f;
    s.encode.quality = 90;
    return s;
}

QStringList TestPipeline::leftovers() const
{
    return QDir(m_outDir).entryList({QStringLiteral("*.tmp")}, QDir::Files | QDir::Hidden);
}

ItemResult TestPipeline::runOne(const QString &src, const JobSpec &s, FakeBackend &b)
{
    std::atomic_bool cancel{false};
    QString err = ProcessJob::checkPreconditions(s);
    PreparedJob job;
    if (err.isEmpty())
        err = ProcessJob::prepare(s, b, job);
    if (!err.isEmpty()) {
        ItemResult r;
        r.error = err;
        r.diagnostics = QStringLiteral("step: preconditions");
        return r;
    }
    return ProcessJob::run(src, s, b, job, cancel);
}

void TestPipeline::png2xOpaque()
{
    FakeBackend b;
    const QByteArray before = testimg::sha256(m_opaque);
    const ItemResult r = runOne(m_opaque, spec(OutputFormat::Png, 2), b);
    QVERIFY2(r.ok, qPrintable(why(r)));
    QCOMPARE(QFileInfo(r.outputPath).fileName(), QStringLiteral("線畫 テスト_2x.png"));
    QImageReader rd(r.outputPath);
    QCOMPARE(rd.format(), QByteArray("png"));
    QCOMPARE(rd.size(), QSize(192, 128));
    QCOMPARE(r.tileUsed, 256);
    QCOMPARE(r.tileChoice, TileChoice::Default);
    QCOMPARE(testimg::sha256(m_opaque), before);  // source untouched
    // Opaque input: only the RGB model is used.
    QCOMPARE(b.acquired, (std::vector<std::string>{"swin_unet_v3/art/noise1_scale2x"}));
    QCOMPARE(b.maxSessions, 2);  // plenty of GPU memory: RGB and alpha may stay open together

    // The nearest-neighbour model makes the 2x output a pixel-doubled input.
    const LoadResult in = ImageLoader::load(m_opaque);
    QVERIFY(in.ok);
    const QImage src = in.image.convertToFormat(QImage::Format_RGB888);
    const QImage out = QImage(r.outputPath).convertToFormat(QImage::Format_RGB888);
    QCOMPARE(out.pixel(11, 7), src.pixel(5, 3));
    QCOMPARE(out.pixel(191, 127), src.pixel(95, 63));
}

void TestPipeline::ttaThroughTheJob()
{
    // The job passes InferenceOptions::tta to the tiler and reports it; with the nearest fake
    // model the TTA mean of an opaque image equals the plain result.
    FakeBackend plainBackend, ttaBackend;
    JobSpec s = spec(OutputFormat::Png, 2);
    s.outputDir = QDir(m_outDir).filePath(QStringLiteral("tta"));  // keeps the shared names for later tests
    QVERIFY(QDir().mkpath(s.outputDir));
    const ItemResult plain = runOne(m_opaque, s, plainBackend);
    QVERIFY2(plain.ok, qPrintable(why(plain)));
    QVERIFY(!plain.tta);
    QVERIFY2(plain.diagnostics.contains(QStringLiteral("\ntta: off")), qPrintable(plain.diagnostics));
    s.inference.tta = true;
    const ItemResult tta = runOne(m_opaque, s, ttaBackend);
    QVERIFY2(tta.ok, qPrintable(why(tta)));
    QVERIFY(tta.tta);
    QVERIFY2(tta.diagnostics.contains(QStringLiteral("\ntta: on")), qPrintable(tta.diagnostics));
    QCOMPARE(tta.tileUsed, 256);
    QCOMPARE(ttaBackend.script.calls, 8 * plainBackend.script.calls);  // square tiles, 96x64 input
    QCOMPARE(QImage(tta.outputPath).convertToFormat(QImage::Format_RGB888),
             QImage(plain.outputPath).convertToFormat(QImage::Format_RGB888));
}

void TestPipeline::webp4xLossyAlpha()
{
    FakeBackend b;
    const ItemResult r = runOne(m_alpha, spec(OutputFormat::WebP, 4), b);
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
    // Non-blank alpha goes through the manifest's 4x alpha model after the RGB model.
    QCOMPARE(b.acquired.size(), size_t(2));
    QCOMPARE(b.acquired[0], std::string("swin_unet/art/noise1_scale4x"));
    QCOMPARE(b.acquired[1], b.contract().alphaModelFor(4));
}

void TestPipeline::webpLosslessAndJpeg()
{
    FakeBackend b;
    JobSpec s = spec(OutputFormat::WebP, 2);
    s.encode.webpLossless = true;
    ItemResult r = runOne(m_alpha, s, b);
    QVERIFY2(r.ok, qPrintable(why(r)));
    QFile f(r.outputPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    QVERIFY(bytes.contains("VP8L"));
    QVERIFY(!bytes.contains("VP8 "));

    r = runOne(m_alpha, spec(OutputFormat::Jpeg, 2), b);
    QVERIFY2(r.ok, qPrintable(why(r)));
    QCOMPARE(QFileInfo(r.outputPath).suffix(), QStringLiteral("jpg"));
    const QImage j(r.outputPath);
    QCOMPARE(j.size(), QSize(160, 120));
    const QRgb px = j.pixel(5, 60);  // was fully transparent -> white
    QVERIFY(qRed(px) > 240 && qGreen(px) > 240 && qBlue(px) > 240);
}

void TestPipeline::sameNameGetsNumbered()
{
    FakeBackend b;
    const JobSpec s = spec(OutputFormat::Png, 2);
    const QString first = QDir(m_outDir).filePath(QStringLiteral("線畫 テスト_2x.png"));
    QVERIFY(QFileInfo::exists(first));  // made by png2xOpaque
    const QByteArray before = testimg::sha256(first);
    const ItemResult a = runOne(m_opaque, s, b);
    QVERIFY2(a.ok, qPrintable(why(a)));
    QCOMPARE(QFileInfo(a.outputPath).fileName(), QStringLiteral("線畫 テスト_2x (1).png"));
    QCOMPARE(testimg::sha256(first), before);  // never overwritten
}

void TestPipeline::sameFolderAsSource()
{
    FakeBackend b;
    JobSpec s = spec(OutputFormat::Png, 2);
    s.outputDir = m_srcDir;
    const QByteArray before = testimg::sha256(m_alpha);
    const ItemResult r = runOne(m_alpha, s, b);
    QVERIFY2(r.ok, qPrintable(why(r)));
    QCOMPARE(QFileInfo(r.outputPath).absolutePath(), QFileInfo(m_alpha).absolutePath());
    QCOMPARE(QFileInfo(r.outputPath).fileName(), QStringLiteral("透明 sample_2x.png"));
    QCOMPARE(testimg::sha256(m_alpha), before);
    QVERIFY(QDir(m_srcDir).entryList({QStringLiteral("*.tmp")}, QDir::Files | QDir::Hidden).isEmpty());
    QVERIFY(QFile::remove(r.outputPath));  // keep the source folder as the other tests expect
}

void TestPipeline::corruptInputFailsCleanly()
{
    FakeBackend b;
    const QString bad = QDir(m_srcDir).filePath(QStringLiteral("壊れた.png"));
    {
        QFile f(bad);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("\x89PNG\r\n\x1a\n truncated");
    }
    const int before = countFiles(m_outDir);
    const ItemResult r = runOne(bad, spec(OutputFormat::Png, 2), b);
    QVERIFY(!r.ok);
    QVERIFY(!r.fatal);
    QVERIFY(!r.error.isEmpty());
    QVERIFY2(r.diagnostics.startsWith(QStringLiteral("step: load")), qPrintable(r.diagnostics));
    QCOMPARE(b.script.calls, 0);
    QCOMPARE(countFiles(m_outDir), before);
}

void TestPipeline::outputFolderGoneFailsCleanly()
{
    // The output folder disappears after the job started: the item fails at the encode
    // step, nothing is written anywhere else, and the source is untouched.
    FakeBackend b;
    const QString dir = m_root.filePath(QStringLiteral("消える"));
    QVERIFY(QDir().mkpath(dir));
    JobSpec s = spec(OutputFormat::Png, 2);
    s.outputDir = dir;
    PreparedJob job;
    QCOMPARE(ProcessJob::checkPreconditions(s), QString());
    QCOMPARE(ProcessJob::prepare(s, b, job), QString());
    QVERIFY(QDir(dir).removeRecursively());
    std::atomic_bool cancel{false};
    const QByteArray before = testimg::sha256(m_opaque);
    const ItemResult r = ProcessJob::run(m_opaque, s, b, job, cancel);
    QVERIFY(!r.ok);
    QVERIFY(!r.fatal);
    QVERIFY2(r.diagnostics.startsWith(QStringLiteral("step: encode")), qPrintable(r.diagnostics));
    QVERIFY(!QFileInfo::exists(dir));
    QCOMPARE(testimg::sha256(m_opaque), before);
}

void TestPipeline::hashMismatchStopsBeforeFirstImage()
{
    FakeBackend b;
    b.badHashId = b.contract().alphaModelFor(2);  // the alpha helper is checked up front too
    int started = 0;
    BatchCallbacks cb;
    cb.itemStarted = [&](int) { ++started; };
    std::atomic_bool cancel{false};
    const BatchSummary r = runBatch({m_opaque, m_alpha}, spec(OutputFormat::Png, 2), b, cancel, cb);
    QCOMPARE(r.succeeded, 0);
    QCOMPARE(r.failed, 0);
    QCOMPARE(r.notProcessed, 2);
    QVERIFY2(r.fatalError.contains(QStringLiteral("checksum")), qPrintable(r.fatalError));
    QCOMPARE(started, 0);
    QVERIFY(b.acquired.empty());
    QCOMPARE(b.beginJobs, 0);
    QVERIFY(b.releases >= 1);
}

void TestPipeline::runtimeMissingStops()
{
    FakeBackend b;
    b.runtime = core::Status::fail(core::ErrorKind::ModelNotFound, "missing onnxruntime.dll");
    std::atomic_bool cancel{false};
    const BatchSummary r = runBatch({m_opaque}, spec(OutputFormat::Png, 2), b, cancel, {});
    QCOMPARE(r.notProcessed, 1);
    QVERIFY2(r.fatalError.contains(QStringLiteral("onnxruntime.dll")), qPrintable(r.fatalError));

    b.runtime = core::Status::fail(core::ErrorKind::EngineFailure, "ORT version 1.22.0 != expected 1.28.3");
    const BatchSummary r2 = runBatch({m_opaque}, spec(OutputFormat::Png, 2), b, cancel, {});
    QVERIFY2(r2.fatalError.contains(QStringLiteral("1.22.0")), qPrintable(r2.fatalError));
    QCOMPARE(b.script.calls, 0);
}

void TestPipeline::cancelDuringInference()
{
    FakeBackend b;
    std::atomic_bool cancel{false};
    b.script.cancel = &cancel;
    b.script.cancelAtCall = 2;
    const JobSpec s = spec(OutputFormat::Png, 2);
    PreparedJob job;
    QCOMPARE(ProcessJob::prepare(s, b, job), QString());
    const int before = countFiles(m_outDir);
    const ItemResult r = ProcessJob::run(m_big, s, b, job, cancel);
    QVERIFY2(r.canceled, qPrintable(why(r)));
    QCOMPARE(b.script.calls, 2);  // stops between tiles
    QCOMPARE(countFiles(m_outDir), before);
    QVERIFY(!QFileInfo::exists(QDir(m_outDir).filePath(QStringLiteral("big_2x.png"))));

    // In a batch the interrupted item counts as not processed, and so do the rest.
    cancel.store(false);
    b.script.calls = 0;
    const BatchSummary sum = runBatch({m_big, m_opaque}, s, b, cancel, {});
    QVERIFY(sum.canceled);
    QCOMPARE(sum.notProcessed, 2);
    QCOMPARE(sum.succeeded, 0);
    QCOMPARE(countFiles(m_outDir), before);
}

void TestPipeline::oomRetriesWithSmallerTile()
{
    FakeBackend b;
    b.script.failAtCall = 1;
    b.script.failWith = core::Status::fail(core::ErrorKind::OutOfMemory, "E_OUTOFMEMORY", 0x8007000E);
    QList<QPair<int, int>> retries;
    QList<ItemResult> results;
    BatchCallbacks cb;
    cb.retrying = [&](int i, int tile) { retries << qMakePair(i, tile); };
    cb.itemFinished = [&](int, const ItemResult &r) { results << r; };
    std::atomic_bool cancel{false};
    const BatchSummary s = runBatch({m_big, m_opaque}, spec(OutputFormat::Png, 2), b, cancel, cb);
    QCOMPARE(s.succeeded, 2);
    QCOMPARE(s.smallerTile, 2);
    QCOMPARE(retries, (QList<QPair<int, int>>{{0, 160}}));
    QCOMPARE(results[0].tileUsed, 160);
    QCOMPARE(results[0].tileChoice, TileChoice::OomRetry);
    QCOMPARE(results[1].tileUsed, 160);   // the rest of the batch stays on the fallback tile
    QCOMPARE(results[1].tileChoice, TileChoice::AfterGpuError);
    QCOMPARE(b.script.tiles.front(), 256);
    for (size_t i = 1; i < b.script.tiles.size(); ++i)
        QCOMPARE(b.script.tiles[i], 160);
    QCOMPARE(b.sessionsCreated, 2);       // rebuilt once, then reused by the next image
}

void TestPipeline::deviceLostRecoversWithSmallerTile()
{
    FakeBackend b;
    b.script.failAtCall = 2;
    b.script.failWith = core::Status::fail(core::ErrorKind::DeviceLost, "DXGI_ERROR_DEVICE_HUNG", 0x887A0006);
    int releasesAtRetry = -1;
    QList<ItemResult> results;
    BatchCallbacks cb;
    cb.retrying = [&](int, int) { releasesAtRetry = b.releases; };
    cb.itemFinished = [&](int, const ItemResult &r) { results << r; };
    std::atomic_bool cancel{false};
    const BatchSummary s = runBatch({m_big, m_opaque}, spec(OutputFormat::Png, 2), b, cancel, cb);
    QCOMPARE(s.succeeded, 2);
    QVERIFY(s.fatalError.isEmpty());
    QCOMPARE(releasesAtRetry, 0);
    QCOMPARE(b.sessionsCreated, 2);
    QCOMPARE(results[0].tileChoice, TileChoice::OomRetry);
    QCOMPARE(results[1].tileUsed, 160);
}

void TestPipeline::recoveryFailureStopsBatch_data()
{
    QTest::addColumn<bool>("rebuildFails");
    QTest::addColumn<int>("kind");
    QTest::addColumn<uint>("hresult");
    QTest::newRow("rebuild: device lost") << true << int(core::ErrorKind::DeviceLost) << 0x887A0006u;
    QTest::newRow("rebuild: out of memory") << true << int(core::ErrorKind::OutOfMemory) << 0x8007000Eu;
    QTest::newRow("redo: out of memory") << false << int(core::ErrorKind::OutOfMemory) << 0x8007000Eu;
    QTest::newRow("redo: device lost") << false << int(core::ErrorKind::DeviceLost) << 0x887A0005u;
    QTest::newRow("redo: other engine error") << false << int(core::ErrorKind::EngineFailure) << 0x80004005u;
}

void TestPipeline::recoveryFailureStopsBatch()
{
    QFETCH(bool, rebuildFails);
    QFETCH(int, kind);
    QFETCH(uint, hresult);
    const core::Status failure = core::Status::fail(core::ErrorKind(kind), "second failure", hresult);
    FakeBackend b;
    b.script.failAtCall = 1;
    b.script.failWith = core::Status::fail(core::ErrorKind::OutOfMemory, "E_OUTOFMEMORY", 0x8007000E);
    QList<ItemResult> results;
    BatchCallbacks cb;
    cb.itemFinished = [&](int, const ItemResult &r) { results << r; };
    if (rebuildFails) {
        b.rebuildFailure = failure;
    } else {
        // The first failure is the OOM; the first Run after the rebuild fails with `failure`.
        b.script.failAtCall2 = 2;
        cb.retrying = [&](int, int) { b.script.failWith = failure; };
    }
    std::atomic_bool cancel{false};
    const int before = countFiles(m_outDir);
    const BatchSummary s = runBatch({m_big, m_opaque, m_alpha}, spec(OutputFormat::Png, 2), b, cancel, cb);
    QCOMPARE(s.failed, 1);
    QCOMPARE(s.succeeded, 0);
    QCOMPARE(s.notProcessed, 2);
    QCOMPARE(results.size(), 1);
    QVERIFY(results[0].fatal);
    QVERIFY(results[0].resetEngine);
    QCOMPARE(countFiles(m_outDir), before);
    QCOMPARE(b.script.calls, rebuildFails ? 1 : 2);  // one retry, no loop
    const QString code = QString::number(hresult, 16).toUpper();
    QVERIFY2(s.fatalError.contains(code), qPrintable(s.fatalError));
    QVERIFY2(s.fatalError.contains(QStringLiteral("CPU")), qPrintable(s.fatalError));
    QVERIFY2(s.fatalError.contains(QStringLiteral("other programs")), qPrintable(s.fatalError));
    QVERIFY2(!s.fatalError.contains(QStringLiteral("smaller tile")), qPrintable(s.fatalError));
}

void TestPipeline::failureAfterRecoveryStopsBatch()
{
    FakeBackend b;
    b.script.failAtCall = 1;  // OOM on the first image
    b.script.failWith = core::Status::fail(core::ErrorKind::OutOfMemory, "E_OUTOFMEMORY", 0x8007000E);
    int retries = 0;
    QList<ItemResult> results;
    BatchCallbacks cb;
    cb.retrying = [&](int, int) { ++retries; };
    cb.itemStarted = [&](int i) {
        if (i == 1)  // next Run: the second image fails too
            b.script.failAtCall2 = b.script.calls + 1;
    };
    cb.itemFinished = [&](int, const ItemResult &r) { results << r; };
    std::atomic_bool cancel{false};
    const BatchSummary s = runBatch({m_big, m_opaque, m_alpha}, spec(OutputFormat::Png, 2), b, cancel, cb);
    QCOMPARE(s.succeeded, 1);
    QCOMPARE(s.failed, 1);
    QCOMPARE(s.notProcessed, 1);
    QCOMPARE(retries, 1);
    QVERIFY(results[1].fatal);
    QVERIFY2(s.fatalError.contains(QStringLiteral("0x8007000E")), qPrintable(s.fatalError));
}

void TestPipeline::outputFailureAfterRecoveryKeepsFallback()
{
    // The GPU recovery succeeds on the first image, but its output file cannot be created
    // (the output folder disappears during encoding). The batch must still keep the rebuilt
    // sessions and tile 160, and a later GPU failure must stop it instead of recovering again.
    FakeBackend b;
    b.script.failAtCall = 1;
    b.script.failWith = core::Status::fail(core::ErrorKind::OutOfMemory, "E_OUTOFMEMORY", 0x8007000E);
    const QString dir = m_root.filePath(QStringLiteral("復原後 出力"));
    QVERIFY(QDir().mkpath(dir));
    JobSpec s = spec(OutputFormat::Png, 2);
    s.outputDir = dir;
    int retries = 0;
    QList<ItemResult> results;
    BatchCallbacks cb;
    cb.retrying = [&](int, int) { ++retries; };
    cb.stageStarted = [&](int i, const StageEvent &e) {
        if (i == 0 && e.stage == Stage::Encode)
            QVERIFY(QDir(dir).removeRecursively());
    };
    cb.itemStarted = [&](int i) {
        if (i == 1)
            QVERIFY(QDir().mkpath(dir));
        if (i == 2)  // the third image fails on the GPU again
            b.script.failAtCall2 = b.script.calls + 1;
    };
    cb.itemFinished = [&](int, const ItemResult &r) { results << r; };
    std::atomic_bool cancel{false};
    const BatchSummary sum = runBatch({m_big, m_opaque, m_alpha}, s, b, cancel, cb);

    QCOMPARE(results.size(), 3);
    QVERIFY(!results[0].ok);
    QVERIFY(!results[0].fatal);
    QVERIFY(results[0].gpuRecovered);  // reported although the item failed
    QVERIFY2(results[0].diagnostics.startsWith(QStringLiteral("step: encode")), qPrintable(why(results[0])));
    QVERIFY2(results[1].ok, qPrintable(why(results[1])));
    QCOMPARE(results[1].tileUsed, 160);
    QCOMPARE(results[1].tileChoice, TileChoice::AfterGpuError);
    QVERIFY(!results[1].gpuRecovered);
    QVERIFY(results[2].fatal);           // no second recovery in this batch
    QVERIFY(!results[2].gpuRecovered);
    QCOMPARE(retries, 1);
    QCOMPARE(b.sessionsCreated, 2);      // rebuilt once, then reused
    QCOMPARE(b.script.tiles.front(), 256);
    for (size_t i = 1; i < b.script.tiles.size(); ++i)
        QCOMPARE(b.script.tiles[i], 160);
    QCOMPARE(sum.succeeded, 1);
    QCOMPARE(sum.failed, 2);
    QVERIFY2(sum.fatalError.contains(QStringLiteral("0x8007000E")), qPrintable(sum.fatalError));
    // Only the second image was written, through the temp file and commit.
    QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden),
             QStringList{QStringLiteral("線畫 テスト_2x.png")});
}

void TestPipeline::oomManualTileFailsItemOnly()
{
    FakeBackend b;
    b.script.failAtCall = 1;
    b.script.failWith = core::Status::fail(core::ErrorKind::OutOfMemory, "E_OUTOFMEMORY", 0x8007000E);
    JobSpec s = spec(OutputFormat::Png, 2);
    s.inference.tile = 256;  // manual: never retried with another size
    QList<ItemResult> results;
    BatchCallbacks cb;
    int releasesAtSecond = -1;
    cb.itemStarted = [&](int i) {
        if (i == 1)
            releasesAtSecond = b.releases;
    };
    cb.itemFinished = [&](int, const ItemResult &r) { results << r; };
    std::atomic_bool cancel{false};
    const int before = countFiles(m_outDir);
    const BatchSummary sum = runBatch({m_big, m_opaque}, s, b, cancel, cb);
    QCOMPARE(sum.failed, 1);
    QCOMPARE(sum.succeeded, 1);
    QVERIFY(sum.fatalError.isEmpty());
    QVERIFY(!results[0].fatal);
    QVERIFY(results[0].resetEngine);
    QVERIFY2(results[0].error.contains(QStringLiteral("256")), qPrintable(results[0].error));
    QCOMPARE(releasesAtSecond, 1);  // sessions released before the next image
    QCOMPARE(results[1].tileUsed, 256);
    QCOMPARE(countFiles(m_outDir), before + 1);
}

void TestPipeline::deviceLostManualTileStopsBatch()
{
    FakeBackend b;
    b.script.failAtCall = 2;
    b.script.failWith = core::Status::fail(core::ErrorKind::DeviceLost, "DXGI_ERROR_DEVICE_REMOVED", 0x887A0005);
    QList<ItemResult> results;
    BatchCallbacks cb;
    cb.itemFinished = [&](int, const ItemResult &r) { results << r; };
    std::atomic_bool cancel{false};
    const int before = countFiles(m_outDir);
    JobSpec manual = spec(OutputFormat::Png, 2);
    manual.inference.tile = 256;  // manual: never retried with another size
    const BatchSummary s = runBatch({m_big, m_opaque, m_alpha}, manual, b, cancel, cb);
    QCOMPARE(s.failed, 1);
    QCOMPARE(s.succeeded, 0);
    QCOMPARE(s.notProcessed, 2);
    QVERIFY(!s.fatalError.isEmpty());
    QCOMPARE(results.size(), 1);
    QVERIFY(results[0].fatal);
    QVERIFY2(results[0].diagnostics.contains(QStringLiteral("887A0005"), Qt::CaseInsensitive),
             qPrintable(results[0].diagnostics));
    QCOMPARE(b.script.calls, 2);  // never retried
    QVERIFY(b.releases >= 1);
    QCOMPARE(countFiles(m_outDir), before);
}

void TestPipeline::smallGpuBudgetUses160()
{
    FakeBackend b;
    b.vramMiB = 900;  // < 920 + 256 needed for 4x at 256
    QList<ItemResult> results;
    BatchCallbacks cb;
    cb.itemFinished = [&](int, const ItemResult &r) { results << r; };
    std::atomic_bool cancel{false};
    const BatchSummary s = runBatch({m_alpha}, spec(OutputFormat::Png, 4), b, cancel, cb);
    QCOMPARE(s.succeeded, 1);
    QCOMPARE(s.smallerTile, 1);
    QCOMPARE(results[0].tileUsed, 160);
    QCOMPARE(results[0].tileChoice, TileChoice::SmallerForVram);
    QCOMPARE(b.maxSessions, 1);  // alpha session only after the RGB one was released
    for (int t : b.script.tiles)
        QCOMPARE(t, 160);
}

void TestPipeline::cpuDevice()
{
    FakeBackend b;
    b.vramMiB = 100;  // ignored on the CPU
    JobSpec s = spec(OutputFormat::Png, 2);
    s.inference.device = ComputeDevice::Cpu;
    QString device;
    BatchCallbacks cb;
    cb.deviceDetected = [&](const QString &d) { device = d; };
    std::atomic_bool cancel{false};
    const BatchSummary sum = runBatch({m_opaque}, s, b, cancel, cb);
    QCOMPARE(sum.succeeded, 1);
    QCOMPARE(b.device, core::ExecutionDevice::Cpu);
    QCOMPARE(device, QStringLiteral("CPU"));
    QCOMPARE(sum.smallerTile, 0);
}

void TestPipeline::batchMixedContinuesAfterFailure()
{
    FakeBackend b;
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
    const BatchSummary s = runBatch(files, spec(OutputFormat::WebP, 2), b, cancel, cb);
    QVERIFY2(s.succeeded == 2, qPrintable(reasons.join(QLatin1Char('\n'))));
    QCOMPARE(s.failed, 1);
    QCOMPARE(s.notProcessed, 0);
    QVERIFY(!s.canceled);
    QVERIFY(s.fatalError.isEmpty());
    QCOMPARE(started, (QList<int>{0, 1, 2}));
    QCOMPARE(finished, (QList<QPair<int, bool>>{{0, true}, {1, false}, {2, true}}));
    QCOMPARE(b.beginJobs, 1);
    QCOMPARE(b.releases, 1);  // once, when the batch ends
}

void TestPipeline::batchPreconditionStops()
{
    FakeBackend b;
    JobSpec s = spec(OutputFormat::Png, 2);
    s.outputDir = m_root.filePath(QStringLiteral("does-not-exist"));
    int calls = 0;
    BatchCallbacks cb;
    cb.itemStarted = [&](int) { ++calls; };
    std::atomic_bool cancel{false};
    const BatchSummary r = runBatch({m_opaque, m_alpha}, s, b, cancel, cb);
    QCOMPARE(r.succeeded, 0);
    QCOMPARE(r.failed, 0);
    QCOMPARE(r.notProcessed, 2);
    QVERIFY(!r.fatalError.isEmpty());
    QCOMPARE(calls, 0);
    QCOMPARE(b.beginJobs, 0);

    // A hand-edited tile (legal for the model, but not offered) never reaches the engine.
    for (int tile : {640, 112, 200}) {
        s = spec(OutputFormat::Png, 2);
        s.inference.tile = tile;
        const BatchSummary t = runBatch({m_opaque}, s, b, cancel, cb);
        QCOMPARE(t.notProcessed, 1);
        QVERIFY2(t.fatalError.contains(QString::number(tile)), qPrintable(t.fatalError));
    }
    QCOMPARE(b.beginJobs, 0);
    QCOMPARE(b.script.calls, 0);
}

void TestPipeline::batchCanceledBeforeStart()
{
    FakeBackend b;
    std::atomic_bool cancel{true};
    const BatchSummary r = runBatch({m_opaque, m_alpha}, spec(OutputFormat::Png, 2), b, cancel, {});
    QVERIFY(r.canceled);
    QCOMPARE(r.notProcessed, 2);
    QCOMPARE(r.succeeded + r.failed, 0);
    QCOMPARE(b.script.calls, 0);
}

void TestPipeline::stagesAreReportedInOrder()
{
    FakeBackend b;
    const JobSpec s = spec(OutputFormat::Png, 4);
    const ItemPlan plan = ProcessJob::progressPlan(m_opaque, s);
    QCOMPARE(plan.weight, 96.0 * 64 * 16 / 1e6);

    QList<StageEvent> events;
    QStringList devices;
    QList<double> fractions;
    BatchCallbacks cb;
    cb.deviceDetected = [&](const QString &d) { devices << d; };
    cb.stageStarted = [&](int index, const StageEvent &e) {
        QCOMPARE(index, 0);
        events << e;
    };
    cb.stageFraction = [&](int, double f) { fractions << f; };
    QString reason;
    cb.itemFinished = [&](int, const ItemResult &res) { reason = why(res); };
    std::atomic_bool cancel{false};
    const BatchSummary r = runBatch({m_opaque}, s, b, cancel, cb);
    QVERIFY2(r.succeeded == 1, qPrintable(reason + QLatin1Char('\n') + r.fatalError));
    QCOMPARE(events.size(), plan.stages.size());
    for (int i = 0; i < events.size(); ++i) {
        QCOMPARE(events[i].stage, plan.stages[i].stage);
        QCOMPARE(events[i].pass, plan.stages[i].pass);
        QCOMPARE(events[i].passCount, 1);
    }
    QCOMPARE(devices, QStringList{QStringLiteral("Fake GPU 6GB")});
    QVERIFY(!fractions.isEmpty());
    QCOMPARE(fractions.last(), 1.0);
}

void TestPipeline::oneXDenoiseOnly()
{
    FakeBackend b;
    const ItemResult r = runOne(m_alpha, spec(OutputFormat::Png, 1), b);
    QVERIFY2(r.ok, qPrintable(why(r)));
    QCOMPARE(QFileInfo(r.outputPath).fileName(), QStringLiteral("透明 sample_1x.png"));
    const QImage out(r.outputPath);
    QCOMPARE(out.size(), QSize(80, 60));
    QVERIFY(out.hasAlphaChannel());
    QCOMPARE(qAlpha(out.pixel(5, 30)), 0);     // 1x keeps the alpha as is
    QCOMPARE(qAlpha(out.pixel(30, 30)), 128);
    QCOMPARE(b.acquired, (std::vector<std::string>{"swin_unet_v3/art/noise1"}));

    // 1x without denoise is refused up front (nothing would be done).
    JobSpec s = spec(OutputFormat::Png, 1);
    s.inference.noise = -1;
    QVERIFY(!ProcessJob::checkPreconditions(s).isEmpty());
}

void TestPipeline::realRuntime2x()
{
    // Real ONNX Runtime + models deployed next to the test exe by the app build.
    const QDir app(QCoreApplication::applicationDirPath());
    if (!QFileInfo::exists(app.filePath(QStringLiteral("onnxruntime.dll"))) ||
        !QFileInfo::exists(app.filePath(QStringLiteral("models/manifest.json"))))
        QSKIP("onnxruntime.dll / models not deployed next to the test exe");
    core::EngineHost host(QDir::toNativeSeparators(app.path()).toStdWString(), ARX_ORT_VERSION,
                          QDir::toNativeSeparators(app.filePath(QStringLiteral("models"))).toStdWString());
    const core::Status ready = host.ready();
    if (!ready.ok() && ready.kind == core::ErrorKind::ModelNotFound)
        QSKIP(qPrintable(QStringLiteral("runtime or models missing: ") + QString::fromStdString(ready.message)));
    QVERIFY2(ready.ok(), ready.message.c_str());

    QList<ItemResult> results;
    QString device;
    BatchCallbacks cb;
    cb.itemFinished = [&](int, const ItemResult &r) { results << r; };
    cb.deviceDetected = [&](const QString &d) { device = d; };
    std::atomic_bool cancel{false};
    const BatchSummary s = runBatch({m_alpha, m_opaque}, spec(OutputFormat::Png, 2), host, cancel, cb);
    QVERIFY2(s.succeeded == 2,
             qPrintable(s.fatalError + QLatin1Char('\n') + (results.isEmpty() ? QString() : why(results.last()))));
    qInfo() << "real runtime device:" << device << "|" << results[0].diagnostics;
    QCOMPARE(QImageReader(results[0].outputPath).size(), QSize(160, 120));
    QCOMPARE(QImageReader(results[1].outputPath).size(), QSize(192, 128));
    QVERIFY(!device.isEmpty());
}

QTEST_GUILESS_MAIN(TestPipeline)
#include "tst_pipeline.moc"
