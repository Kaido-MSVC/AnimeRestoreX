#include "OrtTileEngine.h"

#include <winml/dml_provider_factory.h>

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace arx::core {

namespace fs = std::filesystem;

namespace {

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring modulePath(HMODULE h) {
    std::wstring buf(32768, L'\0');
    buf.resize(GetModuleFileNameW(h, buf.data(), (DWORD)buf.size()));
    return buf;
}

int countLoaded(const wchar_t* name) {
    std::vector<HMODULE> mods(1024);
    DWORD needed = 0;
    EnumProcessModules(GetCurrentProcess(), mods.data(), (DWORD)(mods.size() * sizeof(HMODULE)), &needed);
    mods.resize(std::min<size_t>(mods.size(), needed / sizeof(HMODULE)));
    int n = 0;
    for (HMODULE m : mods)
        if (_wcsicmp(fs::path(modulePath(m)).filename().c_str(), name) == 0) ++n;
    return n;
}

// First "887Axxxx"/"8007xxxx"-style HRESULT (8 hex digits starting with 8) in the message.
uint32_t findHresult(const std::string& msg) {
    for (size_t i = 0; i + 8 <= msg.size(); ++i) {
        if (i > 0 && std::isxdigit((unsigned char)msg[i - 1])) continue;
        if (msg[i] != '8') continue;
        size_t k = i;
        while (k < msg.size() && std::isxdigit((unsigned char)msg[k])) ++k;
        if (k - i != 8) continue;
        return (uint32_t)std::strtoul(msg.substr(i, 8).c_str(), nullptr, 16);
    }
    return 0;
}

}  // namespace

Status initOrtRuntime(const fs::path& dllDir, const std::string& expectedVersion, std::string& info) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    for (const wchar_t* n : {L"DirectML.dll", L"onnxruntime.dll"}) {
        const fs::path want = dllDir / n;
        if (!fs::exists(want)) return Status::fail(ErrorKind::ModelNotFound, "missing " + want.string());
        HMODULE h = LoadLibraryExW(want.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!h) return Status::fail(ErrorKind::EngineFailure, "LoadLibraryExW failed for " + want.string());
        std::error_code ec;
        if (!fs::equivalent(modulePath(GetModuleHandleW(n)), want, ec) || countLoaded(n) != 1)
            return Status::fail(ErrorKind::EngineFailure, narrow(n) + " did not resolve to " + want.string());
        info += narrow(n) + "=" + want.string() + "; ";
    }
    // Version first, through OrtApiBase only: an older onnxruntime.dll may not provide
    // ORT_API_VERSION, and GetApi() would then return nullptr.
    const std::string ver = Ort::GetVersionString();
    info += "ORT " + ver;
    if (ver != expectedVersion)
        return Status::fail(ErrorKind::EngineFailure, "ORT version " + ver + " != expected " + expectedVersion);
    const OrtApi* api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!api)
        return Status::fail(ErrorKind::EngineFailure, "ORT " + ver + " does not provide API version " +
                                                          std::to_string(ORT_API_VERSION));
    Ort::InitApi(api);
    return Status::success();
}

Status classifyOrtError(int ortCode, const std::string& message) {
    const uint32_t hr = findHresult(message);
    const std::string m = "ORT code " + std::to_string(ortCode) + ": " + message;
    switch (hr) {
    case 0x887A0005:  // DXGI_ERROR_DEVICE_REMOVED
    case 0x887A0006:  // DXGI_ERROR_DEVICE_HUNG
    case 0x887A0007:  // DXGI_ERROR_DEVICE_RESET
    case 0x887A0020:  // DXGI_ERROR_DRIVER_INTERNAL_ERROR
        return Status::fail(ErrorKind::DeviceLost, m, hr);
    case 0x8007000E:  // E_OUTOFMEMORY
        return Status::fail(ErrorKind::OutOfMemory, m, hr);
    default:
        return Status::fail(ErrorKind::EngineFailure, m, hr);
    }
}

Status OrtTileEngine::create(Ort::Env& env, const fs::path& model, Ep ep, int device, const std::string& inputName,
                             const std::string& outputName, std::unique_ptr<OrtTileEngine>& out,
                             Ort::RunOptions* sharedRunOptions) {
    try {
        Ort::SessionOptions so;
        // DirectML EP requirements: no memory pattern, sequential execution (ORT DirectML EP docs).
        so.DisableMemPattern();
        so.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        if (ep == Ep::Dml) {
            const OrtDmlApi* dml = nullptr;
            Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
                                                                    reinterpret_cast<const void**>(&dml)));
            Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML(so, device));
        }
        Ort::Session s(env, model.c_str(), so);
        // I/O contract: exactly one input / one output with the manifest names.
        Ort::AllocatorWithDefaultOptions alloc;
        if (s.GetInputCount() != 1 || s.GetOutputCount() != 1 ||
            s.GetInputNameAllocated(0, alloc).get() != inputName || s.GetOutputNameAllocated(0, alloc).get() != outputName)
            return Status::fail(ErrorKind::ContractViolation, "model I/O names differ from the manifest");
        out.reset(new OrtTileEngine(std::move(s), ep, inputName, outputName));
        out->shared_ = sharedRunOptions;
        return Status::success();
    } catch (const Ort::Exception& e) {
        return classifyOrtError((int)e.GetOrtErrorCode(), e.what());
    }
}

Status OrtTileEngine::run(const float* input, int tile, int outSide, std::vector<float>& output) {
    try {
        // A shared RunOptions keeps a terminate request until its owner resets it (so a
        // cancel between the caller's check and this Run is not lost).
        Ort::RunOptions& ro = shared_ ? *shared_ : runOptions_;
        if (!shared_) runOptions_.UnsetTerminate();
        const int64_t shape[4] = {1, 3, tile, tile};
        auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value x = Ort::Value::CreateTensor<float>(mem, const_cast<float*>(input), (size_t)3 * tile * tile, shape, 4);
        const char* in[] = {in_.c_str()};
        const char* out[] = {out_.c_str()};
        auto y = session_.Run(ro, in, &x, 1, out, 1);
        auto info = y[0].GetTensorTypeAndShapeInfo();
        const auto s = info.GetShape();
        if (s.size() != 4 || s[0] != 1 || s[1] != 3 || s[2] != outSide || s[3] != outSide)
            return Status::fail(ErrorKind::ContractViolation, "unexpected output shape");
        const float* p = y[0].GetTensorData<float>();
        output.assign(p, p + info.GetElementCount());
        return Status::success();
    } catch (const Ort::Exception& e) {
        return classifyOrtError((int)e.GetOrtErrorCode(), e.what());
    }
}

}  // namespace arx::core
