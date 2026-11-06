// arx_verify: developer tool for the v2.0 P3 checks (not a ctest test, not shipped).
// Runs ProcessJob / BatchRunner on the real EngineHost next to this exe (onnxruntime.dll,
// DirectML.dll, models/) and writes one JSON line per item and per job: result, tile,
// time, private bytes after the item, and cancel latency.
//
//   arx_verify --out DIR --json LOG [--job SCALE:NOISE:FORMAT[:TILE[:cpu]]]... FILE...
//              [--cancel-at load|inference|encode] [--cancel-item N] [--cancel-delay-ms D]
//              [--force-oom-tile T [--force-device-lost] [--fail-recovery rebuild|redo]]
//   FORMAT: png | jpeg | webp | webpl (lossless). Jobs run in order over all files,
//   in one process with one EngineHost (sessions are released after every job).
//
//   --force-oom-tile T: the first Run of each job really runs tile T on the real session
//   (T is not an offered size, so this cannot go through the job options) and its real error
//   goes back to the unchanged job code. --force-device-lost then Runs the damaged session once
//   more and returns that error instead (887A0006). --fail-recovery simulates a failure of the
//   session rebuild (887A0006) or of the first Run after it (8007000E).
#include "core/BatchRunner.h"
#include "core/ProcessJob.h"
#include "core/inference/EngineHost.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace arx;

namespace {

double privateMiB()
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof pmc);
    return double(pmc.PrivateUsage) / (1024.0 * 1024.0);
}

const char *choiceName(TileChoice c)
{
    switch (c) {
    case TileChoice::Default: return "default";
    case TileChoice::Manual: return "manual";
    case TileChoice::SmallerForVram: return "smaller_for_vram";
    case TileChoice::OomRetry: return "oom_retry";
    case TileChoice::AfterGpuError: return "after_gpu_error";
    }
    return "?";
}

bool parseJob(const QString &text, const QString &outDir, JobSpec &s)
{
    const QStringList p = text.split(QLatin1Char(':'));
    if (p.size() < 3)
        return false;
    s.outputDir = outDir;
    s.inference.scale = p[0].toInt();
    s.inference.noise = p[1].toInt();
    const QString f = p[2];
    s.encode.webpLossless = f == QLatin1String("webpl");
    s.encode.format = f == QLatin1String("png")    ? OutputFormat::Png
                      : f == QLatin1String("jpeg") ? OutputFormat::Jpeg
                                                   : OutputFormat::WebP;
    if (f != QLatin1String("png") && f != QLatin1String("jpeg") && f != QLatin1String("webp") &&
        f != QLatin1String("webpl"))
        return false;
    s.encode.quality = 90;
    s.inference.tile = p.size() > 3 ? p[3].toInt() : 0;
    s.inference.device = p.size() > 4 && p[4] == QLatin1String("cpu") ? ComputeDevice::Cpu : ComputeDevice::Gpu;
    return true;
}

// Fault injection around the real backend; see --force-oom-tile above.
class FaultBackend final : public core::Backend {
public:
    FaultBackend(core::EngineHost &host, int oomTile, bool deviceLost, QString failRecovery)
        : m_host(host), m_engine(*this), m_oomTile(oomTile), m_deviceLost(deviceLost),
          m_failRecovery(std::move(failRecovery)) {}

    int sessionsAtRebuild = -1;  // EngineHost::sessionCount() when the first new session was requested
    QString forcedErrors;        // what the real session returned

    core::Status runtimeReady() override { return m_host.runtimeReady(); }
    core::Status ready() override { return m_host.ready(); }
    const core::ModelContract &contract() const override { return m_host.contract(); }
    core::Status verify(const core::ModelInfo &m) override { return m_host.verify(m); }
    core::Status beginJob(core::ExecutionDevice d, int n) override
    {
        m_armed = true;
        m_tripped = m_redoFailed = false;
        sessionsAtRebuild = -1;
        return m_host.beginJob(d, n);
    }
    core::Status acquire(const core::ModelInfo &m, core::ITileEngine *&engine) override
    {
        if (m_tripped && sessionsAtRebuild < 0)
            sessionsAtRebuild = m_host.sessionCount();
        if (m_tripped && m_failRecovery == QLatin1String("rebuild")) {
            engine = nullptr;
            return core::Status::fail(core::ErrorKind::DeviceLost, "simulated: session rebuild failed", 0x887A0006);
        }
        core::Status s = m_host.acquire(m, m_real);
        m_model = &m;
        engine = &m_engine;
        return s;
    }
    void releaseAll() override { m_host.releaseAll(); }
    void requestTerminate() override { m_host.requestTerminate(); }
    std::string deviceName(core::ExecutionDevice d) override { return m_host.deviceName(d); }
    uint64_t availableVramMiB() override { return m_host.availableVramMiB(); }

private:
    struct Engine final : core::ITileEngine {
        explicit Engine(FaultBackend &b) : backend(b) {}
        FaultBackend &backend;
        core::Status run(const float *in, int T, int outSide, std::vector<float> &out) override
        {
            return backend.run(in, T, outSide, out);
        }
    };

    core::Status run(const float *in, int T, int outSide, std::vector<float> &out)
    {
        if (m_tripped && m_failRecovery == QLatin1String("redo") && !m_redoFailed) {
            m_redoFailed = true;
            return core::Status::fail(core::ErrorKind::OutOfMemory, "simulated: Run after the rebuild failed",
                                      0x8007000E);
        }
        if (!m_armed || !m_oomTile)
            return m_real->run(in, T, outSide, out);
        m_armed = false;
        m_tripped = true;
        const core::TileParams p = core::ModelContract::tileParams(*m_model);
        const std::vector<float> big(size_t(3) * m_oomTile * m_oomTile, 0.5f);
        std::vector<float> ignored;
        core::Status s = m_real->run(big.data(), m_oomTile, m_oomTile * p.scale - 2 * p.offset, ignored);
        if (s.ok())
            return core::Status::fail(core::ErrorKind::EngineFailure, "forced tile did not fail");
        forcedErrors = QString::fromLatin1(core::errorKindName(s.kind)) + QLatin1Char(' ') +
                       QString::number(s.hresult, 16);
        if (m_deviceLost) {
            // Tile 160 as in the P2 redo report §4.4: 256 on this session repeats 8007000E.
            const std::vector<float> tile160(size_t(3) * 160 * 160, 0.5f);
            core::Status second = m_real->run(tile160.data(), 160, 160 * p.scale - 2 * p.offset, ignored);
            forcedErrors += QStringLiteral(", then ") + QString::fromLatin1(core::errorKindName(second.kind)) +
                            QLatin1Char(' ') + QString::number(second.hresult, 16);
            if (!second.ok())
                return second;
        }
        return s;
    }

    core::EngineHost &m_host;
    Engine m_engine;
    core::ITileEngine *m_real = nullptr;
    const core::ModelInfo *m_model = nullptr;
    int m_oomTile;
    bool m_deviceLost;
    QString m_failRecovery;
    bool m_armed = false, m_tripped = false, m_redoFailed = false;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString outDir, jsonPath, cancelAt;
    int cancelItem = 0, cancelDelayMs = 0, forceOomTile = 0;
    bool forceDeviceLost = false;
    QString failRecovery;
    QStringList jobs, files;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args[i];
        auto next = [&] { return i + 1 < args.size() ? args[++i] : QString(); };
        if (a == QLatin1String("--out")) outDir = next();
        else if (a == QLatin1String("--json")) jsonPath = next();
        else if (a == QLatin1String("--job")) jobs << next();
        else if (a == QLatin1String("--cancel-at")) cancelAt = next();
        else if (a == QLatin1String("--cancel-item")) cancelItem = next().toInt();
        else if (a == QLatin1String("--cancel-delay-ms")) cancelDelayMs = next().toInt();
        else if (a == QLatin1String("--force-oom-tile")) forceOomTile = next().toInt();
        else if (a == QLatin1String("--force-device-lost")) forceDeviceLost = true;
        else if (a == QLatin1String("--fail-recovery")) failRecovery = next();
        else files << a;
    }
    if (outDir.isEmpty() || jsonPath.isEmpty() || files.isEmpty()) {
        std::fputs("usage: arx_verify --out DIR --json LOG [--job S:N:FMT[:TILE[:cpu]]]... FILE...\n"
                   "       [--cancel-at load|inference|encode] [--cancel-item N] [--cancel-delay-ms D]\n"
                   "       [--force-oom-tile T [--force-device-lost] [--fail-recovery rebuild|redo]]\n", stderr);
        return 64;
    }
    if (jobs.isEmpty())
        jobs << QStringLiteral("2:1:png");
    QFile log(jsonPath);
    if (!log.open(QIODevice::WriteOnly | QIODevice::Append))
        return 2;
    auto writeLine = [&](QJsonObject o) {
        log.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
        log.flush();
    };

    const QDir appDir(QCoreApplication::applicationDirPath());
    const double privateStart = privateMiB();
    const core::RuntimeInfo &rt = core::EngineHost::initRuntime(
        QDir::toNativeSeparators(appDir.path()).toStdWString(), ARX_ORT_VERSION);
    writeLine({{QStringLiteral("event"), QStringLiteral("runtime")},
          {QStringLiteral("ok"), rt.status.ok()},
          {QStringLiteral("message"), QString::fromStdString(rt.status.message)},
          {QStringLiteral("details"), QString::fromStdString(rt.details)},
          {QStringLiteral("private_mib"), privateStart}});
    core::EngineHost host(QDir::toNativeSeparators(appDir.path()).toStdWString(), ARX_ORT_VERSION,
                          QDir::toNativeSeparators(appDir.filePath(QStringLiteral("models"))).toStdWString());
    FaultBackend backend(host, forceOomTile, forceDeviceLost, failRecovery);

    using Clock = std::chrono::steady_clock;
    int exitCode = 0;
    for (int j = 0; j < jobs.size(); ++j) {
        JobSpec spec;
        if (!parseJob(jobs[j], outDir, spec)) {
            std::fprintf(stderr, "bad --job %s\n", qPrintable(jobs[j]));
            return 64;
        }
        std::atomic_bool cancel{false};
        std::thread canceller;
        Clock::time_point cancelTime{};
        QElapsedTimer itemTimer;
        QList<QJsonObject> items;
        QString device;
        QList<int> retries;

        BatchCallbacks cb;
        cb.deviceDetected = [&](const QString &d) { device = d; };
        cb.itemStarted = [&](int) { itemTimer.start(); };
        cb.retrying = [&](int, int tile) { retries << tile; };
        cb.stageStarted = [&](int i, const StageEvent &e) {
            if (j != 0 || i != cancelItem || cancelAt.isEmpty() || canceller.joinable())
                return;
            const Stage want = cancelAt == QLatin1String("load")        ? Stage::Load
                               : cancelAt == QLatin1String("inference") ? Stage::Inference
                                                                        : Stage::Encode;
            if (e.stage != want)
                return;
            auto fire = [&] {
                cancelTime = Clock::now();
                cancel.store(true);
                host.requestTerminate();
            };
            if (cancelDelayMs <= 0)
                fire();  // synchronously, inside the stage
            else
                canceller = std::thread([&, fire] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(cancelDelayMs));
                    fire();
                });
        };
        cb.itemFinished = [&](int i, const ItemResult &r) {
            const auto now = Clock::now();
            QJsonObject o{{QStringLiteral("event"), QStringLiteral("item")},
                          {QStringLiteral("job"), jobs[j]},
                          {QStringLiteral("index"), i},
                          {QStringLiteral("file"), QFileInfo(files[i]).fileName()},
                          {QStringLiteral("ok"), r.ok},
                          {QStringLiteral("canceled"), r.canceled},
                          {QStringLiteral("fatal"), r.fatal},
                          {QStringLiteral("error"), r.error},
                          {QStringLiteral("output"), r.outputPath},
                          {QStringLiteral("tile_used"), r.tileUsed},
                          {QStringLiteral("tile_choice"), QString::fromLatin1(choiceName(r.tileChoice))},
                          {QStringLiteral("reset_engine"), r.resetEngine},
                          {QStringLiteral("ms"), double(itemTimer.nsecsElapsed()) / 1e6},
                          {QStringLiteral("private_mib"), privateMiB()},
                          {QStringLiteral("diagnostics"), r.diagnostics}};
            if (cancelTime != Clock::time_point{})
                o.insert(QStringLiteral("cancel_to_return_ms"),
                         std::chrono::duration<double, std::milli>(now - cancelTime).count());
            items << o;
            writeLine(o);
        };

        QElapsedTimer jobTimer;
        jobTimer.start();
        const BatchSummary s = runBatch(files, spec, backend, cancel, cb);
        const auto returned = Clock::now();
        if (canceller.joinable())
            canceller.join();
        QJsonObject o{{QStringLiteral("event"), QStringLiteral("job")},
                      {QStringLiteral("job"), jobs[j]},
                      {QStringLiteral("device"), device},
                      {QStringLiteral("succeeded"), s.succeeded},
                      {QStringLiteral("failed"), s.failed},
                      {QStringLiteral("not_processed"), s.notProcessed},
                      {QStringLiteral("canceled"), s.canceled},
                      {QStringLiteral("fatal_error"), s.fatalError},
                      {QStringLiteral("smaller_tile"), s.smallerTile},
                      {QStringLiteral("retries"), int(retries.size())},
                      {QStringLiteral("sessions_at_rebuild"), backend.sessionsAtRebuild},
                      {QStringLiteral("forced_errors"), backend.forcedErrors},
                      {QStringLiteral("ms"), double(jobTimer.nsecsElapsed()) / 1e6},
                      {QStringLiteral("vram_mib"), double(host.availableVramMiB())},
                      // runBatch released every session before returning.
                      {QStringLiteral("private_after_release_mib"), privateMiB()}};
        if (cancelTime != Clock::time_point{})
            o.insert(QStringLiteral("cancel_to_batch_return_ms"),
                     std::chrono::duration<double, std::milli>(returned - cancelTime).count());
        writeLine(o);
        if (!s.fatalError.isEmpty())
            exitCode = 3;
    }
    writeLine({{QStringLiteral("event"), QStringLiteral("end")}, {QStringLiteral("private_mib"), privateMiB()}});
    return exitCode;
}
