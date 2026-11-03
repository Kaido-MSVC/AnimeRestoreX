// arx_verify: developer tool for the v2.0 P3 checks (not a ctest test, not shipped).
// Runs ProcessJob / BatchRunner on the real EngineHost next to this exe (onnxruntime.dll,
// DirectML.dll, models/) and writes one JSON line per item and per job: result, tile,
// time, private bytes after the item, and cancel latency.
//
//   arx_verify --out DIR --json LOG [--job SCALE:NOISE:FORMAT[:TILE[:cpu]]]... FILE...
//              [--cancel-at load|inference|encode] [--cancel-item N] [--cancel-delay-ms D]
//   FORMAT: png | jpeg | webp | webpl (lossless). Jobs run in order over all files,
//   in one process with one EngineHost (sessions are released after every job).
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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString outDir, jsonPath, cancelAt;
    int cancelItem = 0, cancelDelayMs = 0;
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
        else files << a;
    }
    if (outDir.isEmpty() || jsonPath.isEmpty() || files.isEmpty()) {
        std::fputs("usage: arx_verify --out DIR --json LOG [--job S:N:FMT[:TILE[:cpu]]]... FILE...\n"
                   "       [--cancel-at load|inference|encode] [--cancel-item N] [--cancel-delay-ms D]\n", stderr);
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
        const BatchSummary s = runBatch(files, spec, host, cancel, cb);
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
