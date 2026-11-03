// Inference backend seen by ProcessJob / BatchRunner: the verified model contract, the
// sessions (created on first use, limited cache) and the device. EngineHost implements it
// on ONNX Runtime; tests use a fake. Plain C++, no Qt, no ORT headers.
#pragma once

#include <cstdint>
#include <string>

#include "ModelContract.h"
#include "Status.h"
#include "TileProcessor.h"

namespace arx::core {

enum class ExecutionDevice { Dml, Cpu };

class Backend {
public:
    virtual ~Backend() = default;

    // Runtime DLLs loaded from the app folder and the version is the expected one.
    virtual Status runtimeReady() = 0;
    // runtimeReady() and the manifest read. Not ok -> nothing else may be used.
    virtual Status ready() = 0;
    virtual const ModelContract& contract() const = 0;

    // File present below the models root and SHA-256 equal to the manifest.
    virtual Status verify(const ModelInfo& m) = 0;

    // Starts a job: device to use and how many sessions may stay open at once
    // (2 = RGB and alpha model together, 1 = one after the other). Releases sessions
    // of another device. Clears a previous terminate request.
    virtual Status beginJob(ExecutionDevice device, int maxSessions) = 0;

    // Session for a model (created and hash-checked on first use; least recently used
    // sessions beyond maxSessions are released first). Valid until the next acquire()
    // or releaseAll().
    virtual Status acquire(const ModelInfo& m, ITileEngine*& engine) = 0;

    // Releases every session (and with it the GPU device of the runtime).
    virtual void releaseAll() = 0;

    // Any thread: aborts a CPU Run in progress; DML stops between tiles only.
    virtual void requestTerminate() = 0;

    // Name of the device sessions run on ("CPU" for the CPU EP).
    virtual std::string deviceName(ExecutionDevice device) = 0;

    // GPU memory this process may still use (DXGI budget minus current usage), in MiB.
    // 0 = unknown.
    virtual uint64_t availableVramMiB() = 0;
};

}  // namespace arx::core
