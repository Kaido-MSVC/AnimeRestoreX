// Backend on ONNX Runtime (DirectML EP or CPU EP).
// One process-wide runtime (DLLs pinned to the app folder, version checked, one Ort::Env);
// sessions are created lazily, hash-checked first, and kept in a small LRU cache.
// No ORT headers here; plain C++.
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Backend.h"

namespace arx::core {

struct RuntimeInfo {
    Status status;             // not ok: the runtime must not be used
    std::string details;       // resolved DLL paths and ORT version (diagnostics)
};

class EngineHost final : public Backend {
public:
    // Loads onnxruntime.dll / DirectML.dll from `dllDir` only and checks the ORT version.
    // Runs once per process; later calls return the first result.
    static const RuntimeInfo& initRuntime(const std::filesystem::path& dllDir, const std::string& expectedOrtVersion);
    // nullptr until initRuntime() was called.
    static const RuntimeInfo* runtime();

    // `modelsRoot` holds manifest.json and the model files; `dllDir` / version are used
    // if the runtime was not initialised yet. `dmlDevice` = DXGI adapter index (0 = default).
    EngineHost(std::filesystem::path dllDir, std::string expectedOrtVersion, std::filesystem::path modelsRoot,
               int dmlDevice = 0);
    ~EngineHost() override;

    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;

    Status runtimeReady() override;
    Status ready() override;
    const ModelContract& contract() const override;
    Status verify(const ModelInfo& m) override;
    Status beginJob(ExecutionDevice device, int maxSessions) override;
    Status acquire(const ModelInfo& m, ITileEngine*& engine) override;
    void releaseAll() override;
    void requestTerminate() override;
    std::string deviceName(ExecutionDevice device) override;
    uint64_t availableVramMiB() override;

    int sessionCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace arx::core
