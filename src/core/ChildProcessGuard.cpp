#include "core/ChildProcessGuard.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace arx {

ChildProcessGuard &ChildProcessGuard::instance()
{
    static ChildProcessGuard guard;
    return guard;
}

ChildProcessGuard::ChildProcessGuard()
{
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        CloseHandle(job);
        return;
    }
    m_job = job;
}

ChildProcessGuard::~ChildProcessGuard()
{
    if (m_job)
        CloseHandle(static_cast<HANDLE>(m_job));
}

bool ChildProcessGuard::assign(qint64 pid)
{
    if (!m_job || pid <= 0)
        return false;
    HANDLE proc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid));
    if (!proc)
        return false;
    const bool ok = AssignProcessToJobObject(static_cast<HANDLE>(m_job), proc) != FALSE;
    CloseHandle(proc);
    return ok;
}

} // namespace arx
