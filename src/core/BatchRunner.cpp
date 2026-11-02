#include "core/BatchRunner.h"

#include "core/ProcessJob.h"

namespace arx {

BatchSummary runBatch(const QStringList &files, const JobSpec &spec,
                      const std::atomic_bool &cancel, const BatchCallbacks &cb)
{
    BatchSummary s;
    const int total = int(files.size());

    s.fatalError = ProcessJob::checkPreconditions(spec);
    if (!s.fatalError.isEmpty()) {
        s.notProcessed = total;
        return s;
    }

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
        sink.deviceDetected = cb.deviceDetected;
        const ItemResult r = ProcessJob::run(files.at(i), spec, cancel, sink);
        if (cb.itemFinished)
            cb.itemFinished(i, r);

        if (r.canceled) {
            s.canceled = true;
            s.notProcessed = total - i;  // the interrupted item counts as not processed
            return s;
        }
        if (r.ok) {
            ++s.succeeded;
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
