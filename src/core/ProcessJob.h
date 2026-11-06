#pragma once

#include "core/JobSpec.h"
#include "core/Progress.h"
#include "core/inference/Backend.h"
#include "core/inference/TilePolicy.h"

#include <QCoreApplication>
#include <QString>

#include <atomic>

namespace arx {

// What every item of a job shares, decided once by ProcessJob::prepare().
struct PreparedJob {
    const core::ModelInfo *rgbModel = nullptr;    // from the backend's verified contract
    const core::ModelInfo *alphaModel = nullptr;  // 2x/4x helper for non-blank alpha; null at 1x
    core::ExecutionDevice device = core::ExecutionDevice::Dml;
    core::TileDecision tile;
    QString deviceName;      // as the backend names it (GPU description or "CPU")
    quint64 vramMiB = 0;     // budget seen when the job started (0 = unknown / CPU)
    bool recovered = false;  // the batch already rebuilt its sessions: the next GPU failure stops it
};

// What progressPlan() needs to know about the prepared job to group measured speeds:
// the model file (hash), the tile actually used and whether the item starts cold
// (its inference creates the AI sessions).
struct PlanContext {
    QString modelHash;   // manifest SHA-256 of the RGB model (first 8 hex digits are used)
    int tile = 0;        // 0 = not decided yet (Auto)
    bool cold = false;
};

class ProcessJob {
    Q_DECLARE_TR_FUNCTIONS(ProcessJob)
public:
    // Errors that stop the whole job (shared by all items). Empty = OK.
    static QString checkOutputDir(const QString &dir);
    // Output folder, encoder plugin and the scale / denoise combination.
    static QString checkPreconditions(const JobSpec &spec);
    // Scale / denoise combination only (1x needs denoise >= 0). Empty = OK.
    static QString checkOptions(const InferenceOptions &o);
    // Manifest id of the model for a scale / denoise combination ("" if none).
    static QString modelId(const InferenceOptions &o);

    // Engine side of the preconditions: runtime ready, models present with the
    // manifest hash, tile decision from the GPU memory budget, sessions configured.
    // Empty = OK, otherwise a user-facing reason (the batch must not start).
    static QString prepare(const JobSpec &spec, core::Backend &backend, PreparedJob &job,
                           QString *diagnostics = nullptr);

    // Processes one source image end to end:
    // load -> tiled inference (RGB, then alpha if any) -> encode to temp -> rename.
    // The source file is never modified; only files created here are removed.
    // `progress` is told when each stage of progressPlan() starts and how many
    // output pixels of the inference stage are finished.
    static ItemResult run(const QString &sourcePath, const JobSpec &spec, core::Backend &backend,
                          const PreparedJob &job, const std::atomic_bool &cancel,
                          const ProgressSink &progress = {});

    // The stages run() goes through for this file, with the expected cost of
    // each (engine adapter for ProgressEstimator). Reads only the image header.
    // Inference speeds are grouped by model hash, EP, tile and scale (the device is
    // the group the caller stores them under); a cold item learns its start-up cost
    // separately. Without a context the model hash and the Auto tile are unknown.
    static ItemPlan progressPlan(const QString &sourcePath, const JobSpec &spec,
                                 const PlanContext *context = nullptr);
    // Context of item `index` of a prepared job (the first item is cold).
    static PlanContext planContext(const PreparedJob &job, int index);

    // User-facing text for an engine error (no English runtime message is shown alone).
    static QString engineErrorMessage(const core::Status &s);
    // User-facing text when the runtime (onnxruntime.dll / DirectML.dll) cannot be used.
    static QString runtimeErrorMessage(const core::Status &s);
};

} // namespace arx
