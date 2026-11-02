#include "ui/JobWorker.h"

#include "core/BatchRunner.h"
#include "core/ProcessJob.h"

void JobWorker::process(const QStringList &files, const arx::JobSpec &spec)
{
    m_cancel.store(false);

    // Image headers only; read here so a slow drive does not block the GUI.
    QList<arx::ItemPlan> plans;
    plans.reserve(files.size());
    for (const QString &f : files)
        plans << arx::ProcessJob::progressPlan(f, spec);
    emit planned(plans);

    arx::BatchCallbacks cb;
    cb.itemStarted = [this](int i) { emit itemStarted(i); };
    cb.itemFinished = [this](int i, const arx::ItemResult &r) { emit itemFinished(i, r); };
    cb.stageStarted = [this](int i, const arx::StageEvent &e) {
        emit stageStarted(i, int(e.stage), e.pass, e.passCount);
    };
    cb.stageFraction = [this](int i, double f) { emit stageFraction(i, f); };
    cb.deviceDetected = [this](const QString &device) { emit deviceDetected(device); };

    const arx::BatchSummary summary = arx::runBatch(files, spec, m_cancel, cb);
    emit jobFinished(summary);
}
