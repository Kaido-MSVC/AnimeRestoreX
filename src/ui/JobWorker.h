#pragma once

#include "core/BatchRunner.h"
#include "core/JobSpec.h"
#include "core/Progress.h"
#include "core/inference/Backend.h"

#include <QList>
#include <QObject>
#include <QStringList>

#include <atomic>
#include <memory>

// Lives in a worker QThread and processes items sequentially (one GPU task
// at a time). The JobSpec is copied, so UI changes do not affect a running job.
// Owns the inference backend: the runtime is initialised and sessions are created
// on this thread only; they are released when a job ends.
class JobWorker : public QObject {
    Q_OBJECT
public:
    // Backend on the app folder: onnxruntime.dll / DirectML.dll next to the exe,
    // models/manifest.json and the model files below <app>/models.
    explicit JobWorker(QObject *parent = nullptr);
    // Any backend (tests).
    explicit JobWorker(std::unique_ptr<arx::core::Backend> backend, QObject *parent = nullptr);
    ~JobWorker() override;

    // Thread-safe; may be called from the GUI thread. The worker stops before the
    // next tile (DML) or aborts the running tile (CPU); it never kills the thread.
    void requestCancel();

public slots:
    void process(const QStringList &files, const arx::JobSpec &spec);

signals:
    // Emitted first (after the engine was prepared): the expected stages of every item,
    // for progress estimation. Not emitted if the job cannot start.
    void planned(const QList<arx::ItemPlan> &plans);
    // Tile used for the job; `smallerForVram`: Auto chose 160 because GPU memory is short.
    void prepared(int tile, bool smallerForVram);
    void itemStarted(int index);
    // `stage` is an arx::Stage value.
    void stageStarted(int index, int stage, int pass, int passCount);
    void stageFraction(int index, double fraction);
    // Out of GPU memory: the item starts again with `tile`.
    void retrying(int index, int tile);
    void deviceDetected(const QString &device);
    void itemFinished(int index, const arx::ItemResult &result);
    void jobFinished(const arx::BatchSummary &summary);

private:
    std::atomic_bool m_cancel{false};
    std::unique_ptr<arx::core::Backend> m_backend;
};
