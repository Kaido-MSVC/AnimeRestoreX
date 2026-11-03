#include "core/BatchRunner.h"

#include "core/ProcessJob.h"

namespace arx {

namespace {

// Sessions (and the runtime's GPU device) are released whenever the batch ends.
struct ReleaseOnExit {
    core::Backend &backend;
    ~ReleaseOnExit() { backend.releaseAll(); }
};

} // namespace

BatchSummary runBatch(const QStringList &files, const JobSpec &spec, core::Backend &backend,
                      const std::atomic_bool &cancel, const BatchCallbacks &cb)
{
    BatchSummary s;
    const int total = int(files.size());

    s.fatalError = ProcessJob::checkPreconditions(spec);
    if (s.fatalError.isEmpty() && cancel.load()) {
        s.canceled = true;
        s.notProcessed = total;
        return s;
    }
    ReleaseOnExit release{backend};
    PreparedJob job;
    if (s.fatalError.isEmpty())
        s.fatalError = ProcessJob::prepare(spec, backend, job);
    if (!s.fatalError.isEmpty()) {
        s.notProcessed = total;
        return s;
    }
    if (cb.prepared)
        cb.prepared(job);
    if (cb.deviceDetected)
        cb.deviceDetected(job.deviceName);

    for (int i = 0; i < total; ++i) {
        if (cancel.load()) {
            s.canceled = true;
            s.notProcessed = total - i;
            return s;
        }
        if (cb.itemStarted)
            cb.itemStarted(i);
        ProgressSink sink;
        if (cb.stageStarted)
            sink.stageStarted = [&cb, i](const StageEvent &e) { cb.stageStarted(i, e); };
        if (cb.stageFraction)
            sink.stageFraction = [&cb, i](double f) { cb.stageFraction(i, f); };
        if (cb.retrying)
            sink.retrying = [&cb, i](int tile) { cb.retrying(i, tile); };
        const ItemResult r = ProcessJob::run(files.at(i), spec, backend, job, cancel, sink);
        if (r.resetEngine)
            backend.releaseAll();  // the next item starts with fresh sessions
        if (cb.itemFinished)
            cb.itemFinished(i, r);

        if (r.canceled) {
            s.canceled = true;
            s.notProcessed = total - i;  // the interrupted item counts as not processed
            return s;
        }
        if (r.ok) {
            ++s.succeeded;
            if (r.tileChoice == TileChoice::SmallerForVram || r.tileChoice == TileChoice::OomRetry)
                ++s.smallerTile;
            continue;
        }
        ++s.failed;
        if (r.fatal) {
            s.fatalError = r.error;
            s.notProcessed = total - i - 1;
            return s;
        }
    }
    return s;
}

} // namespace arx
