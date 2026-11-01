#pragma once

#include <QtGlobal>

namespace arx {

// Windows Job Object with KILL_ON_JOB_CLOSE: every child assigned to it is
// terminated by the OS when AnimeRestoreX exits, even after a crash.
class ChildProcessGuard {
public:
    static ChildProcessGuard &instance();
    bool assign(qint64 pid);

    ChildProcessGuard(const ChildProcessGuard &) = delete;
    ChildProcessGuard &operator=(const ChildProcessGuard &) = delete;

private:
    ChildProcessGuard();
    ~ChildProcessGuard();
    void *m_job = nullptr;
};

} // namespace arx
