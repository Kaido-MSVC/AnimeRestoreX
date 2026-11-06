// ITileEngine on ONNX Runtime (DirectML EP or CPU EP).
// Compile with ORT_API_MANUAL_INIT (the target defines it); call initOrtRuntime() first.
#pragma once

#include <onnxruntime_cxx_api.h>

#include <filesystem>
#include <memory>
#include <string>

#include "TileProcessor.h"

namespace arx::core {

// Loads onnxruntime.dll / DirectML.dll from `dllDir` only (absolute LoadLibraryExW after
// SetDefaultDllDirectories), checks that exactly that file is loaded, calls Ort::InitApi()
// and checks GetVersionString() == expectedVersion. `info` receives the resolved paths.
Status initOrtRuntime(const std::filesystem::path& dllDir, const std::string& expectedVersion, std::string& info);

// Maps a runtime error to an ErrorKind using the HRESULT in the message:
// 887A0005/0006/0007/0020 -> DeviceLost, 8007000E -> OutOfMemory, else EngineFailure.
Status classifyOrtError(int ortCode, const std::string& message);

class OrtTileEngine final : public ITileEngine {
public:
    enum class Ep { Dml, Cpu };

    // `sharedRunOptions` (optional, must outlive the engine): every Run uses it and never
    // clears its terminate flag, so one SetTerminate() reaches all sessions of a host.
    static Status create(Ort::Env& env, const std::filesystem::path& model, Ep ep, int device,
                         const std::string& inputName, const std::string& outputName,
                         std::unique_ptr<OrtTileEngine>& out, Ort::RunOptions* sharedRunOptions = nullptr);

    Status run(const float* input, int tile, int outSide, std::vector<float>& output) override;

    // CPU path: aborts the Run in progress (RunOptions::SetTerminate). The DML EP does not
    // honour it (P1 §6.6), so for DML cancel takes effect between tiles only.
    void requestTerminate() { (shared_ ? *shared_ : runOptions_).SetTerminate(); }
    Ep ep() const { return ep_; }

private:
    OrtTileEngine(Ort::Session&& s, Ep ep, std::string in, std::string out)
        : session_(std::move(s)), ep_(ep), in_(std::move(in)), out_(std::move(out)) {}
    Ort::Session session_;
    Ort::RunOptions runOptions_;
    Ort::RunOptions* shared_ = nullptr;
    Ep ep_;
    std::string in_, out_;
};

}  // namespace arx::core
