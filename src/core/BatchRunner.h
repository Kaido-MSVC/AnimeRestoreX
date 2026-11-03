#pragma once

#include "core/JobSpec.h"
#include "core/Progress.h"
#include "core/inference/Backend.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace arx {

struct BatchSummary {
    int succeeded = 0;
    int failed = 0;
    int notProcessed = 0;   // skipped because of cancel or a fatal error
    bool canceled = false;
    QString fatalError;     // shared error that stopped the batch
    // Succeeded items made with tile 160 instead of 256 (Auto: GPU memory budget, or a
    // retry after running out of GPU memory). The tile changes the result slightly.
    int smallerTile = 0;
};

struct BatchCallbacks {
    std::function<void(int index)> itemStarted;
    std::function<void(int index, const ItemResult &result)> itemFinished;
    std::function<void(int index, const StageEvent &e)> stageStarted;
    std::function<void(int index, double fraction)> stageFraction;
    std::function<void(int index, int tile)> retrying;
    std::function<void(const QString &device)> deviceDetected;
};

// Processes files one at a time with the same JobSpec snapshot and one backend
// (sessions are shared by the items and released at the end). A failing item is
// recorded and the batch continues; shared errors (preconditions, runtime or model
// problems, device errors) stop the batch. After an out-of-memory failure or a
// device error the sessions are released before anything else runs.
BatchSummary runBatch(const QStringList &files, const JobSpec &spec, core::Backend &backend,
                      const std::atomic_bool &cancel, const BatchCallbacks &cb);

} // namespace arx
