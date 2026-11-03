#include "EngineHost.h"

#include "OrtTileEngine.h"

#include <windows.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <atomic>
#include <list>
#include <mutex>
#include <utility>

namespace arx::core {

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace {

std::once_flag g_runtimeOnce;
RuntimeInfo g_runtime;
std::atomic<bool> g_runtimeDone{false};

// One Ort::Env per process (ORT requirement). Created after the runtime check passed and
// intentionally never destroyed: sessions may be released late during shutdown, and the
// pinned onnxruntime.dll is never unloaded.
Ort::Env& processEnv() {
    static Ort::Env* env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "AnimeRestoreX");
    return *env;
}

std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

ComPtr<IDXGIAdapter3> dxgiAdapter(int index) {
    ComPtr<IDXGIFactory1> f;
    ComPtr<IDXGIAdapter1> a;
    ComPtr<IDXGIAdapter3> a3;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&f))) && SUCCEEDED(f->EnumAdapters1((UINT)index, &a)))
        a.As(&a3);
    return a3;
}

}  // namespace

struct EngineHost::Impl {
    fs::path dllDir, modelsRoot;
    std::string ortVersion;
    int dmlDevice = 0;

    bool loaded = false;
    Status loadStatus;
    ModelContract contract;

    ExecutionDevice device = ExecutionDevice::Dml;
    int maxSessions = 2;
    std::unique_ptr<Ort::RunOptions> runOptions;
    std::atomic<Ort::RunOptions*> runOptionsPtr{nullptr};
    std::mutex terminateMutex;  // SetTerminate from the GUI thread vs. UnsetTerminate in beginJob
    std::list<std::pair<std::string, std::unique_ptr<OrtTileEngine>>> sessions;  // front = most recent
};

const RuntimeInfo& EngineHost::initRuntime(const fs::path& dllDir, const std::string& expectedOrtVersion) {
    std::call_once(g_runtimeOnce, [&] {
        try {
            g_runtime.status = initOrtRuntime(dllDir, expectedOrtVersion, g_runtime.details);
        } catch (const std::exception& e) {
            g_runtime.status = Status::fail(ErrorKind::EngineFailure, std::string("runtime init: ") + e.what());
        }
        g_runtimeDone.store(true);
    });
    return g_runtime;
}

const RuntimeInfo* EngineHost::runtime() { return g_runtimeDone.load() ? &g_runtime : nullptr; }

EngineHost::EngineHost(fs::path dllDir, std::string expectedOrtVersion, fs::path modelsRoot, int dmlDevice)
    : d(std::make_unique<Impl>()) {
    d->dllDir = std::move(dllDir);
    d->ortVersion = std::move(expectedOrtVersion);
    d->modelsRoot = std::move(modelsRoot);
    d->dmlDevice = dmlDevice;
}

EngineHost::~EngineHost() { releaseAll(); }

Status EngineHost::runtimeReady() { return initRuntime(d->dllDir, d->ortVersion).status; }

Status EngineHost::ready() {
    Status rt = runtimeReady();
    if (!rt.ok()) return rt;
    if (!d->loaded) {
        d->loaded = true;
        d->loadStatus = ModelContract::load(d->modelsRoot / "manifest.json", d->contract);
        if (d->loadStatus.ok()) {
            try {
                processEnv();
                d->runOptions = std::make_unique<Ort::RunOptions>();
                d->runOptionsPtr.store(d->runOptions.get());
            } catch (const Ort::Exception& e) {
                d->loadStatus = classifyOrtError((int)e.GetOrtErrorCode(), e.what());
            }
        }
    }
    return d->loadStatus;
}

const ModelContract& EngineHost::contract() const { return d->contract; }

Status EngineHost::verify(const ModelInfo& m) {
    fs::path p;
    return d->contract.resolveVerified(m, d->modelsRoot, p);
}

Status EngineHost::beginJob(ExecutionDevice device, int maxSessions) {
    Status s = ready();
    if (!s.ok()) return s;
    if (device != d->device) releaseAll();
    d->device = device;
    d->maxSessions = maxSessions < 1 ? 1 : maxSessions;
    while ((int)d->sessions.size() > d->maxSessions) d->sessions.pop_back();
    std::lock_guard<std::mutex> lock(d->terminateMutex);
    d->runOptions->UnsetTerminate();
    return Status::success();
}

Status EngineHost::acquire(const ModelInfo& m, ITileEngine*& engine) {
    engine = nullptr;
    for (auto it = d->sessions.begin(); it != d->sessions.end(); ++it) {
        if (it->first == m.id) {
            d->sessions.splice(d->sessions.begin(), d->sessions, it);
            engine = d->sessions.front().second.get();
            return Status::success();
        }
    }
    Status s = ready();
    if (!s.ok()) return s;
    // Release first, so two large sessions are never alive together when only one fits.
    while ((int)d->sessions.size() >= d->maxSessions) d->sessions.pop_back();
    fs::path file;
    s = d->contract.resolveVerified(m, d->modelsRoot, file);
    if (!s.ok()) return s;
    std::unique_ptr<OrtTileEngine> e;
    const auto ep = d->device == ExecutionDevice::Dml ? OrtTileEngine::Ep::Dml : OrtTileEngine::Ep::Cpu;
    s = OrtTileEngine::create(processEnv(), file, ep, d->dmlDevice, d->contract.inputName(), d->contract.outputName(), e,
                              d->runOptions.get());
    if (!s.ok()) return s;
    d->sessions.emplace_front(m.id, std::move(e));
    engine = d->sessions.front().second.get();
    return Status::success();
}

void EngineHost::releaseAll() { d->sessions.clear(); }

void EngineHost::requestTerminate() {
    std::lock_guard<std::mutex> lock(d->terminateMutex);
    if (Ort::RunOptions* ro = d->runOptionsPtr.load()) ro->SetTerminate();
}

std::string EngineHost::deviceName(ExecutionDevice device) {
    if (device == ExecutionDevice::Cpu) return "CPU";
    ComPtr<IDXGIAdapter3> a = dxgiAdapter(d->dmlDevice);
    DXGI_ADAPTER_DESC1 desc{};
    if (!a || FAILED(a->GetDesc1(&desc))) return "GPU " + std::to_string(d->dmlDevice);
    return narrow(desc.Description);
}

uint64_t EngineHost::availableVramMiB() {
    ComPtr<IDXGIAdapter3> a = dxgiAdapter(d->dmlDevice);
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (!a || FAILED(a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)) || info.Budget == 0)
        return 0;
    const uint64_t freeBytes = info.Budget > info.CurrentUsage ? info.Budget - info.CurrentUsage : 0;
    return freeBytes > (1u << 20) ? freeBytes >> 20 : 1;  // never report "unknown" for a full budget
}

int EngineHost::sessionCount() const { return (int)d->sessions.size(); }

}  // namespace arx::core
