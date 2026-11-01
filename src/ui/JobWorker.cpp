#include "ui/JobWorker.h"

#include "core/ProcessJob.h"

void JobWorker::process(const QStringList &files, const arx::JobSpec &spec)
{
    m_cancel.store(false);

    const QString fatal = arx::ProcessJob::checkPreconditions(spec);
    if (!fatal.isEmpty()) {
        emit jobFinished(0, 0, false, fatal);
        return;
    }

    int ok = 0;
    int failed = 0;
    for (int i = 0; i < files.size(); ++i) {
        if (m_cancel.load()) {
            emit jobFinished(ok, failed, true, {});
            return;
        }
        emit itemStarted(i);
        const arx::ItemResult r = arx::ProcessJob::run(files.at(i), spec, m_cancel);
        if (r.canceled) {
            emit itemFinished(i, r);
            emit jobFinished(ok, failed, true, {});
            return;
        }
        r.ok ? ++ok : ++failed;
        emit itemFinished(i, r);
    }
    emit jobFinished(ok, failed, false, {});
}
