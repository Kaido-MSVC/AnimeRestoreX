#pragma once

#include "core/JobSpec.h"

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
};

struct BatchCallbacks {
    std::function<void(int index)> itemStarted;
    std::function<void(int index, const ItemResult &result)> itemFinished;
};

// Processes files one at a time with the same JobSpec snapshot. A failing
// item is recorded and the batch continues; shared errors (preconditions,
// engine cannot start, no usable GPU) stop the batch.
BatchSummary runBatch(const QStringList &files, const JobSpec &spec,
                      const std::atomic_bool &cancel, const BatchCallbacks &cb);

} // namespace arx
