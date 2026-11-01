#pragma once

#include "core/JobSpec.h"

#include <QCoreApplication>
#include <QString>

#include <atomic>

namespace arx {

class ProcessJob {
    Q_DECLARE_TR_FUNCTIONS(ProcessJob)
public:
    // Errors that stop the whole job (shared by all items). Empty = OK.
    static QString checkOutputDir(const QString &dir);
    static QString checkPreconditions(const JobSpec &spec);

    // Processes one source image end to end:
    // load -> temp PNG -> waifu2x -> read back -> encode to temp -> rename.
    // The source file is never modified; only files created here are removed.
    static ItemResult run(const QString &sourcePath, const JobSpec &spec,
                          const std::atomic_bool &cancel);
};

} // namespace arx
