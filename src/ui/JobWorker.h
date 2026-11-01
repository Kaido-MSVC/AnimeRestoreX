#pragma once

#include "core/JobSpec.h"

#include <QObject>
#include <QStringList>

#include <atomic>

// Lives in a worker QThread and processes items sequentially (one GPU task
// at a time). The JobSpec is copied, so UI changes do not affect a running job.
class JobWorker : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // Thread-safe; may be called from the GUI thread.
    void requestCancel() { m_cancel.store(true); }

public slots:
    void process(const QStringList &files, const arx::JobSpec &spec);

signals:
    void itemStarted(int index);
    void itemFinished(int index, const arx::ItemResult &result);
    // fatalError is non-empty when a shared precondition stopped the job.
    void jobFinished(int succeeded, int failed, bool canceled, const QString &fatalError);

private:
    std::atomic_bool m_cancel{false};
};
