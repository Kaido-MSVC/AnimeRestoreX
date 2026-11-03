#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <functional>

namespace arx {

// Progress reporting and estimation. Nothing here knows about a particular
// inference engine: the engine adapter (ProcessJob::progressPlan) describes
// the stages of an item, the job reports when each stage starts, and an
// engine that can report real progress passes it through stageFraction.
// The tiled ONNX engine reports the share of output pixels whose tiles are
// finished and blended (RGB and alpha passes together); that is completed
// work, not a time fraction.

enum class Stage { Load = 0, Inference = 1, Encode = 2 };

struct StageEvent {
    Stage stage = Stage::Load;
    int pass = 0;       // inference pass, 0-based (0 for the other stages)
    int passCount = 1;  // inference passes of this item
};

struct ProgressSink {
    std::function<void(const StageEvent &)> stageStarted;
    // Optional: real progress (0..1) of the current stage, from an engine
    // that reports it. Without it the stage progress is estimated.
    std::function<void(double fraction)> stageFraction;
    // The processing device as the engine names it (e.g. the GPU). Measured
    // speeds are only valid for the device they were measured on.
    std::function<void(const QString &device)> deviceDetected;
    // Out of GPU memory: the current image starts again with a smaller tile,
    // so its inference progress goes back to 0.
    std::function<void(int tile)> retrying;
};

// One stage of an item as the engine adapter predicts it:
// expected seconds = overheadSec + rate * megapixels.
struct StagePlan {
    Stage stage = Stage::Load;
    int pass = 0;
    QString rateKey;          // stages sharing a key share a measured speed
    double megapixels = 0;    // pixels this stage produces (load: input pixels)
    double defaultRate = 0;   // seconds per megapixel before anything was measured
    double overheadSec = 0;   // fixed cost (model load, ...) before anything was measured
    // Cold start (the first image of a job creates the AI sessions): the fixed cost is
    // learned under this key, in seconds, separately from the warm speed of rateKey.
    // Empty = warm stage without a fixed cost.
    QString overheadKey;
};

struct ItemPlan {
    double weight = 0;        // output megapixels; 0 = size unknown
    QList<StagePlan> stages;  // in execution order
};

struct ProgressSnapshot {
    int index = -1;               // running item, -1 = none
    Stage stage = Stage::Load;
    int pass = 0;
    int passCount = 1;
    double itemFraction = 0;      // 0..1, estimated
    double batchFraction = 0;     // 0..1, weighted by output pixels
    double remainingSec = -1;     // -1 = not known yet (no measured inference speed)
};

// Estimates item and batch progress from the stage plans and the measured
// speed. Times are passed in (milliseconds from any monotonic clock), so the
// class is deterministic and testable. Measured speeds are refined after
// every completed stage and can be persisted between sessions.
class ProgressEstimator {
public:
    // Speeds measured earlier (rateKey -> seconds per megapixel).
    void setLearnedRates(const QHash<QString, double> &rates);
    QHash<QString, double> learnedRates() const { return m_learned; }
    // Until the device is confirmed, the learned speeds may belong to another
    // GPU, so no remaining time is given (progress bars still work).
    void setDeviceConfirmed(bool confirmed) { m_deviceConfirmed = confirmed; }
    bool deviceConfirmed() const { return m_deviceConfirmed; }

    void startBatch(const QList<ItemPlan> &items, qint64 nowMs);
    void itemStarted(int index, qint64 nowMs);
    void stageStarted(int index, const StageEvent &e, qint64 nowMs);
    void stageFraction(int index, double fraction, qint64 nowMs);
    // The current stage starts over (OOM retry with a smaller tile): its
    // reported fraction may go back.
    void stageRestarted(int index, qint64 nowMs);
    // `ok`: the item completed, so its last stage is a valid measurement.
    void itemFinished(int index, bool ok, qint64 nowMs);

    ProgressSnapshot snapshot(qint64 nowMs) const;

    // Expected seconds of a stage with the current speeds.
    double stageSeconds(const StagePlan &s) const;
    // True once an inference speed was measured on this machine.
    bool hasMeasuredInference() const;

    static bool isValidRate(double r);

private:
    double rateFor(const StagePlan &s) const;
    double overheadFor(const StagePlan &s) const;
    double machineFactor() const;  // measured / default inference speed
    void closeStage(qint64 nowMs, bool calibrate);
    double currentStageFraction(qint64 nowMs) const;

    QHash<QString, double> m_learned;
    bool m_deviceConfirmed = true;
    QHash<QString, double> m_defaults;  // default rates of learned inference keys
    QList<ItemPlan> m_items;
    QList<double> m_weights;            // unknown sizes replaced by the average
    double m_totalWeight = 0;
    double m_doneWeight = 0;
    int m_index = -1;
    int m_next = 0;                     // first item not started yet
    int m_stage = -1;                   // index into the item's stages
    qint64 m_stageStart = 0;
    double m_engineFraction = -1;
    QList<double> m_itemStageSec;       // stage durations frozen at item start
    double m_itemTotalSec = 0;
};

} // namespace arx
