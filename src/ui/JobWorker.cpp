#include "ui/JobWorker.h"

#include "core/BatchRunner.h"

void JobWorker::process(const QStringList &files, const arx::JobSpec &spec)
{
    m_cancel.store(false);

    arx::BatchCallbacks cb;
    cb.itemStarted = [this](int i) { emit itemStarted(i); };
    cb.itemFinished = [this](int i, const arx::ItemResult &r) { emit itemFinished(i, r); };

    const arx::BatchSummary summary = arx::runBatch(files, spec, m_cancel, cb);
    emit jobFinished(summary);
}
