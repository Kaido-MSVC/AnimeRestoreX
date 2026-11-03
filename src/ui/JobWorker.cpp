#include "ui/JobWorker.h"

#include "core/BatchRunner.h"
#include "core/ProcessJob.h"
#include "core/inference/EngineHost.h"

#include <QCoreApplication>
#include <QDir>

namespace {

std::unique_ptr<arx::core::Backend> appBackend()
{
    const QString dir = QCoreApplication::applicationDirPath();
    return std::make_unique<arx::core::EngineHost>(QDir::toNativeSeparators(dir).toStdWString(),
                                                   std::string(ARX_ORT_VERSION),
                                                   QDir::toNativeSeparators(QDir(dir).filePath(
                                                       QStringLiteral("models"))).toStdWString());
}

} // namespace

JobWorker::JobWorker(QObject *parent) : JobWorker(appBackend(), parent) {}

JobWorker::JobWorker(std::unique_ptr<arx::core::Backend> backend, QObject *parent)
    : QObject(parent), m_backend(std::move(backend))
{
}

JobWorker::~JobWorker() = default;

void JobWorker::requestCancel()
{
    m_cancel.store(true);
    m_backend->requestTerminate();
}

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
    cb.retrying = [this](int i, int tile) { emit retrying(i, tile); };
    cb.deviceDetected = [this](const QString &device) { emit deviceDetected(device); };

    const arx::BatchSummary summary = arx::runBatch(files, spec, *m_backend, m_cancel, cb);
    emit jobFinished(summary);
}
