#pragma once

#include "core/JobSpec.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <atomic>

namespace arx {

struct RunResult {
    bool ok = false;
    bool canceled = false;
    QString error;
};

class Waifu2xRunner {
    Q_DECLARE_TR_FUNCTIONS(Waifu2xRunner)
public:
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
