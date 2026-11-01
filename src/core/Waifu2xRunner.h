#pragma once

#include "core/JobSpec.h"

#include <QCoreApplication>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>

namespace arx {

struct RunResult {
    bool ok = false;
    bool canceled = false;
    bool fatal = false;  // engine cannot start or no usable GPU: affects every item
    QString error;
};

class Waifu2xRunner {
    Q_DECLARE_TR_FUNCTIONS(Waifu2xRunner)
public:
    // Inference passes for a requested scale:
    //   1x: [-n N -s 1]                    (denoise only, N >= 0; added in v1.1)
    //   2x: [-n N -s 2]
    //   4x: [-n N -s 2] then [-n -1 -s 2]  (denoise only once; decided after A9)
    // waifu2x's own "-s 4" would run the denoise+2x model twice.
    static QList<Waifu2xParams> passes(const Waifu2xParams &p);

    // cunet model file name (without extension) used by one pass.
    static QString modelBaseName(const Waifu2xParams &pass);

    // Checks that the executable and the model files needed for `p` exist.
    // Returns an empty string when OK, otherwise a user-facing reason.
    static QString checkInstall(const Waifu2xParams &p);

    static QStringList arguments(const Waifu2xParams &p, const QString &inPng,
                                 const QString &outPng);

    // Runs waifu2x synchronously (call from a worker thread). Polls `cancel`
    // and kills the child when it becomes true. `ok` only means the process
    // exited cleanly; the caller must still verify the output file because
    // waifu2x exits with 0 even when decoding fails.
    static RunResult run(const Waifu2xParams &p, const QString &inPng, const QString &outPng,
                         const std::atomic_bool &cancel);

    // Extracts the meaningful part of waifu2x stderr (drops GPU info lines).
    static QString summarizeStderr(const QByteArray &stderrData);
};

} // namespace arx
