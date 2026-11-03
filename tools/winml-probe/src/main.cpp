// winml_probe: stand-alone Windows ML (ONNX Runtime C/C++ API) probe for
// AnimeRestoreX v2.0, phase P1. Development tool only; no Qt.
//
// Runs one nunif ONNX model on one pre-padded NCHW float32 tile with either
// the DirectML EP or the CPU EP, and reports load path / version, adapter,
// timings, output shape, peak memory and (optionally) node placement,
// profiling and a cross-thread RunOptions::SetTerminate() cancel test.
#define ORT_API_MANUAL_INIT
#include <winml/onnxruntime_cxx_api.h>
#include <winml/dml_provider_factory.h>
#undef ORT_API_MANUAL_INIT

#include <windows.h>
#include <dxgi1_6.h>
#include <psapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

namespace {

double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': break;
        case '\t': o += "\\t"; break;
        default:
            if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
            else o += c;
        }
    }
    return o;
}

struct Args {
    fs::path model, input, output, log, json, profile, dumpDir;
    std::string ep = "dml";
    int device = 0;
    int tile = 256;
    int scale = 1;
    int offset = 8;
    int warm = 3;
    bool verbose = false;
    int cancelDelayMs = -1;
    std::string opt = "all";  // graph optimization level: disable|basic|extended|all
    std::vector<std::pair<std::string, std::string>> config;  // --config key=value (AddConfigEntry)
};

[[noreturn]] void usage() {
    std::fputs(
        "usage: winml_probe --model M.onnx --input in.bin --ep dml|cpu --scale S --offset O\n"
        "                   [--tile 256] [--device 0] [--warm 3] [--output out.bin]\n"
        "                   [--verbose --log file] [--profile prefix] [--cancel-delay-ms D]\n"
        "                   [--opt disable|basic|extended|all] [--json results.jsonl]\n"
        "                   [--dump-dir D]  (run once, write every float graph output to D)\n"
        "                   [--config key=value]...  (session config entry, e.g. ep.dml.disable_graph_fusion=1)\n", stderr);
    std::exit(64);
}

Args parseArgs(int argc, wchar_t** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::wstring k = argv[i];
        auto next = [&]() -> std::wstring { if (i + 1 >= argc) usage(); return argv[++i]; };
        if (k == L"--model") a.model = next();
        else if (k == L"--input") a.input = next();
        else if (k == L"--output") a.output = next();
        else if (k == L"--log") a.log = next();
        else if (k == L"--json") a.json = next();
        else if (k == L"--profile") a.profile = next();
        else if (k == L"--ep") a.ep = narrow(next());
        else if (k == L"--device") a.device = std::stoi(next());
        else if (k == L"--tile") a.tile = std::stoi(next());
        else if (k == L"--scale") a.scale = std::stoi(next());
        else if (k == L"--offset") a.offset = std::stoi(next());
        else if (k == L"--warm") a.warm = std::stoi(next());
        else if (k == L"--cancel-delay-ms") a.cancelDelayMs = std::stoi(next());
        else if (k == L"--opt") a.opt = narrow(next());
        else if (k == L"--dump-dir") a.dumpDir = next();
        else if (k == L"--config") {
            std::string kv = narrow(next());
            auto eq = kv.find('=');
            if (eq == std::string::npos) usage();
            a.config.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        }
        else if (k == L"--verbose") a.verbose = true;
        else usage();
    }
    if (a.model.empty() || a.input.empty() || (a.ep != "dml" && a.ep != "cpu")) usage();
    return a;
}

fs::path exeDir() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n < buf.size()) { buf.resize(n); break; }
        buf.resize(buf.size() * 2);
    }
    return fs::path(buf).parent_path();
}

std::wstring modulePath(HMODULE h) {
    std::wstring buf(32768, L'\0');
    DWORD n = GetModuleFileNameW(h, buf.data(), (DWORD)buf.size());
    buf.resize(n);
    return buf;
}

bool samePath(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}

// Every module in this process whose file name matches `name`.
std::vector<std::wstring> loadedModulesNamed(const wchar_t* name) {
    std::vector<HMODULE> mods(1024);
    DWORD needed = 0;
    EnumProcessModules(GetCurrentProcess(), mods.data(), (DWORD)(mods.size() * sizeof(HMODULE)), &needed);
    mods.resize(std::min<size_t>(mods.size(), needed / sizeof(HMODULE)));
    std::vector<std::wstring> out;
    for (HMODULE m : mods) {
        std::wstring p = modulePath(m);
        if (_wcsicmp(fs::path(p).filename().c_str(), name) == 0) out.push_back(p);
    }
    return out;
}

// Pin onnxruntime.dll and DirectML.dll to the files next to the exe.
// Returns false (with a message) if they are missing or something else got loaded.
bool pinRuntimeDlls(const fs::path& dir, std::string& report) {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    const wchar_t* names[] = {L"DirectML.dll", L"onnxruntime.dll"};
    for (const wchar_t* n : names) {
        fs::path want = dir / n;
        if (!fs::exists(want)) { report = "missing " + narrow(want.wstring()); return false; }
        HMODULE h = LoadLibraryExW(want.c_str(), nullptr,
                                   LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!h) { report = "LoadLibraryExW failed for " + narrow(want.wstring()) + " err=" + std::to_string(GetLastError()); return false; }
        HMODULE byName = GetModuleHandleW(n);
        std::wstring got = modulePath(byName);
        if (!samePath(got, want)) { report = "module " + narrow(n) + " resolved to " + narrow(got); return false; }
        auto all = loadedModulesNamed(n);
        if (all.size() != 1) {
            report = "more than one " + narrow(n) + " loaded:";
            for (auto& p : all) report += " " + narrow(p);
            return false;
        }
    }
    return true;
}

struct Adapter {
    bool ok = false;
    std::string name;
    unsigned vendor = 0, deviceId = 0;
    unsigned long long dedicatedVram = 0;
    std::string driver;
    ComPtr<IDXGIAdapter3> a3;
};

Adapter describeAdapter(int index) {
    Adapter r;
    ComPtr<IDXGIFactory1> f;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f)))) return r;
    ComPtr<IDXGIAdapter1> a;
    // DML device_id = index in IDXGIFactory::EnumAdapters order.
    if (FAILED(f->EnumAdapters1((UINT)index, &a))) return r;
    DXGI_ADAPTER_DESC1 d{};
    a->GetDesc1(&d);
    r.ok = true;
    r.name = narrow(d.Description);
    r.vendor = d.VendorId;
    r.deviceId = d.DeviceId;
    r.dedicatedVram = d.DedicatedVideoMemory;
    LARGE_INTEGER umd{};
    if (SUCCEEDED(a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
        char b[64];
        std::snprintf(b, sizeof b, "%u.%u.%u.%u", HIWORD(umd.HighPart), LOWORD(umd.HighPart),
                      HIWORD(umd.LowPart), LOWORD(umd.LowPart));
        r.driver = b;
    }
    a.As(&r.a3);
    return r;
}

// Samples this process's local (dedicated) GPU memory usage for a peak value.
class VramSampler {
public:
    explicit VramSampler(IDXGIAdapter3* a) : a_(a) {
        if (a_) { baseline_ = sample(); th_ = std::thread([this] { loop(); }); }
    }
    ~VramSampler() { stop(); }
    void stop() { if (th_.joinable()) { run_ = false; th_.join(); } }
    unsigned long long baseline() const { return baseline_; }
    unsigned long long peak() const { return peak_; }
    bool available() const { return a_ != nullptr; }
private:
    unsigned long long sample() {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (FAILED(a_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) return 0;
        return info.CurrentUsage;
    }
    void loop() {
        while (run_) {
            peak_ = std::max<unsigned long long>(peak_, sample());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        peak_ = std::max<unsigned long long>(peak_, sample());
    }
    IDXGIAdapter3* a_;
    std::atomic<bool> run_{true};
    std::atomic<unsigned long long> peak_{0};
    unsigned long long baseline_ = 0;
    std::thread th_;
};

struct LogSink {
    std::mutex m;
    std::ofstream file;
    std::vector<std::string> placement;  // "placed on [...]" lines
};

void ORT_API_CALL logFn(void* param, OrtLoggingLevel severity, const char* category,
                        const char* logid, const char* codeLocation, const char* message) {
    auto* s = static_cast<LogSink*>(param);
    std::lock_guard lk(s->m);
    if (s->file) s->file << severity << " [" << category << "] " << logid << " " << codeLocation << " " << message << "\n";
    std::string msg = message;
    if (msg.find("placed on [") != std::string::npos || msg.find("Node placements") != std::string::npos)
        s->placement.push_back(msg);
    if (severity >= ORT_LOGGING_LEVEL_WARNING) std::fprintf(stderr, "[ort %d] %s\n", severity, message);
}

std::vector<float> readFloats(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open " + narrow(p.wstring()));
    auto n = (size_t)f.tellg();
    std::vector<float> v(n / sizeof(float));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(v.data()), (std::streamsize)(v.size() * sizeof(float)));
    return v;
}

void writeFloats(const fs::path& p, const float* d, size_t n) {
    std::ofstream f(p, std::ios::binary);
    f.write(reinterpret_cast<const char*>(d), (std::streamsize)(n * sizeof(float)));
}

std::string shapeStr(const std::vector<int64_t>& s) {
    std::string o = "[";
    for (size_t i = 0; i < s.size(); ++i) o += (i ? "," : "") + std::to_string(s[i]);
    return o + "]";
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    Args a = parseArgs(argc, argv);
    std::ostringstream js;  // one JSON object per invocation
    js << "{\"model\":\"" << jsonEscape(narrow(a.model.filename().wstring())) << "\",\"ep\":\"" << a.ep << "\"";

    // 1. Runtime DLLs from the app directory only.
    const fs::path dir = exeDir();
    std::string why;
    if (!pinRuntimeDlls(dir, why)) {
        std::fprintf(stderr, "FATAL: runtime DLL check failed: %s\n", why.c_str());
        return 3;
    }
    Ort::InitApi();
    const std::string ortPath = narrow(modulePath(GetModuleHandleW(L"onnxruntime.dll")));
    const std::string dmlPath = narrow(modulePath(GetModuleHandleW(L"DirectML.dll")));
    const std::string ver = Ort::GetVersionString();
    std::printf("onnxruntime.dll : %s\nDirectML.dll    : %s\nGetVersionString: %s (expected %s)\n",
                ortPath.c_str(), dmlPath.c_str(), ver.c_str(), WINML_EXPECTED_ORT_VERSION);
    js << ",\"ort_path\":\"" << jsonEscape(ortPath) << "\",\"ort_version\":\"" << ver << "\"";
    if (ver != WINML_EXPECTED_ORT_VERSION) {
        std::fprintf(stderr, "FATAL: unexpected ORT version\n");
        return 3;
    }

    // 2. Adapter (also used for the DML case; reported for CPU runs for context).
    Adapter ad = describeAdapter(a.device);
    if (ad.ok)
        std::printf("adapter %d      : %s (VEN_%04X DEV_%04X) VRAM %.0f MiB, UMD driver %s\n", a.device,
                    ad.name.c_str(), ad.vendor, ad.deviceId, ad.dedicatedVram / 1048576.0, ad.driver.c_str());
    js << ",\"adapter\":\"" << jsonEscape(ad.name) << "\",\"driver\":\"" << ad.driver << "\"";

    LogSink sink;
    if (!a.log.empty()) sink.file.open(a.log);
    int exitCode = 0;
    try {
        Ort::Env env(a.verbose ? ORT_LOGGING_LEVEL_VERBOSE : ORT_LOGGING_LEVEL_WARNING, "probe", logFn, &sink);

        Ort::SessionOptions so;
        // DirectML EP requirements (ORT DirectML EP docs): no memory pattern, sequential execution.
        // Applied to the CPU runs too so that both paths use the same session settings.
        so.DisableMemPattern();
        so.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        if (a.opt == "disable") so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
        else if (a.opt == "basic") so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
        else if (a.opt == "extended") so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        else if (a.opt == "all") so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        else throw std::runtime_error("bad --opt " + a.opt);
        std::printf("graph opt level : %s\n", a.opt.c_str());
        js << ",\"opt\":\"" << a.opt << "\"";
        for (auto& [k, v] : a.config) {
            so.AddConfigEntry(k.c_str(), v.c_str());
            std::printf("config entry    : %s=%s\n", k.c_str(), v.c_str());
            js << ",\"config_" << jsonEscape(k) << "\":\"" << jsonEscape(v) << "\"";
        }
        if (a.verbose) so.SetLogSeverityLevel(ORT_LOGGING_LEVEL_VERBOSE);
        if (!a.profile.empty()) so.EnableProfiling(a.profile.c_str());
        if (a.ep == "dml") {
            const OrtDmlApi* dml = nullptr;
            Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION,
                                                                    reinterpret_cast<const void**>(&dml)));
            Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML(so, a.device));
        }

        // 3. Input tile.
        std::vector<float> in = readFloats(a.input);
        const size_t want = 3ull * a.tile * a.tile;
        if (in.size() != want) throw std::runtime_error("input has " + std::to_string(in.size()) + " floats, expected " + std::to_string(want));
        const std::vector<int64_t> inShape{1, 3, a.tile, a.tile};
        const int64_t outSide = (int64_t)a.tile * a.scale - 2LL * a.offset;
        const std::vector<int64_t> expectShape{1, 3, outSide, outSide};
        auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value x = Ort::Value::CreateTensor<float>(mem, in.data(), in.size(), inShape.data(), inShape.size());
        const char* inNames[] = {"x"};
        const char* outNames[] = {"y"};

        // Node-by-node check: one Run fetching every graph output (the caller adds
        // intermediate tensors as outputs). Writes <i>.bin and index.txt "i<TAB>name<TAB>shape".
        if (!a.dumpDir.empty()) {
            fs::create_directories(a.dumpDir);
            Ort::Session session(env, a.model.c_str(), so);
            Ort::AllocatorWithDefaultOptions alloc;
            const size_t n = session.GetOutputCount();
            std::vector<std::string> names;
            for (size_t i = 0; i < n; ++i) names.emplace_back(session.GetOutputNameAllocated(i, alloc).get());
            std::vector<const char*> cnames;
            for (auto& s : names) cnames.push_back(s.c_str());
            Ort::RunOptions ro;
            auto outs = session.Run(ro, inNames, &x, 1, cnames.data(), cnames.size());
            std::ofstream idx(a.dumpDir / "index.txt");
            for (size_t i = 0; i < n; ++i) {
                auto info = outs[i].GetTensorTypeAndShapeInfo();
                if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) continue;
                writeFloats(a.dumpDir / (std::to_string(i) + ".bin"), outs[i].GetTensorData<float>(), info.GetElementCount());
                idx << i << "\t" << names[i] << "\t" << shapeStr(info.GetShape()) << "\n";
            }
            std::printf("dumped %zu outputs to %s\n", n, narrow(a.dumpDir.wstring()).c_str());
            return 0;
        }

        VramSampler vram(ad.a3.Get());

        // 4. Cold start: session creation + first run.
        auto t0 = Clock::now();
        Ort::Session session(env, a.model.c_str(), so);
        auto t1 = Clock::now();
        Ort::RunOptions ro;
        ro.SetRunLogSeverityLevel(ORT_LOGGING_LEVEL_WARNING);
        auto y = session.Run(ro, inNames, &x, 1, outNames, 1);
        auto t2 = Clock::now();
        if (!a.profile.empty()) {
            Ort::AllocatorWithDefaultOptions alloc;
            auto pf = session.EndProfilingAllocated(alloc);
            std::printf("profile file    : %s\n", pf.get());
            js << ",\"profile\":\"" << jsonEscape(pf.get()) << "\"";
        }
        auto outInfo = y[0].GetTensorTypeAndShapeInfo();
        auto outShape = outInfo.GetShape();
        const bool shapeOk = outShape == expectShape;
        const size_t outN = outInfo.GetElementCount();
        std::vector<float> first(y[0].GetTensorData<float>(), y[0].GetTensorData<float>() + outN);
        std::printf("session create  : %.1f ms\nfirst Run       : %.1f ms (cold total %.1f ms)\n",
                    ms(t0, t1), ms(t1, t2), ms(t0, t2));
        std::printf("output shape    : %s expected %s -> %s\n", shapeStr(outShape).c_str(),
                    shapeStr(expectShape).c_str(), shapeOk ? "OK" : "MISMATCH");
        js << ",\"session_ms\":" << ms(t0, t1) << ",\"first_run_ms\":" << ms(t1, t2)
           << ",\"out_shape\":\"" << shapeStr(outShape) << "\",\"shape_ok\":" << (shapeOk ? "true" : "false");
        if (!shapeOk) exitCode = 4;

        // 5. Warm runs.
        std::vector<double> warm;
        float maxRunDiff = 0.f;
        for (int i = 0; i < a.warm; ++i) {
            auto s = Clock::now();
            auto yy = session.Run(ro, inNames, &x, 1, outNames, 1);
            warm.push_back(ms(s, Clock::now()));
            const float* p = yy[0].GetTensorData<float>();
            for (size_t k = 0; k < outN; ++k) maxRunDiff = std::max(maxRunDiff, std::fabs(p[k] - first[k]));
        }
        if (!warm.empty()) {
            auto sorted = warm;
            std::sort(sorted.begin(), sorted.end());
            double mean = std::accumulate(warm.begin(), warm.end(), 0.0) / warm.size();
            std::printf("warm Run x%zu    : min %.1f / median %.1f / mean %.1f ms; max |run_i - run_0| = %g\n",
                        warm.size(), sorted.front(), sorted[sorted.size() / 2], mean, maxRunDiff);
            js << ",\"warm_min_ms\":" << sorted.front() << ",\"warm_median_ms\":" << sorted[sorted.size() / 2]
               << ",\"warm_mean_ms\":" << mean << ",\"run_to_run_max_abs\":" << maxRunDiff;
        }
        if (!a.output.empty()) writeFloats(a.output, first.data(), first.size());

        // 6. Cancel test: SetTerminate() from another thread while Run is in flight.
        if (a.cancelDelayMs >= 0) {
            Ort::RunOptions cro;
            cro.SetRunLogSeverityLevel(ORT_LOGGING_LEVEL_WARNING);
            Clock::time_point tSet{};
            std::atomic<bool> setDone{false};
            auto start = Clock::now();
            std::thread killer([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(a.cancelDelayMs));
                tSet = Clock::now();
                cro.SetTerminate();
                setDone = true;
            });
            std::string status = "completed";
            int code = 0;
            try {
                auto yy = session.Run(cro, inNames, &x, 1, outNames, 1);
            } catch (const Ort::Exception& e) {
                status = std::string("exception: ") + e.what();
                code = (int)e.GetOrtErrorCode();
            }
            auto tRet = Clock::now();
            killer.join();
            const double afterSet = tRet > tSet ? ms(tSet, tRet) : -ms(tRet, tSet);
            std::printf("cancel test     : delay %d ms, Run returned after %.1f ms; %.1f ms after SetTerminate; code %d; %s\n",
                        a.cancelDelayMs, ms(start, tRet), afterSet, code, status.c_str());
            js << ",\"cancel_delay_ms\":" << a.cancelDelayMs << ",\"cancel_run_ms\":" << ms(start, tRet)
               << ",\"cancel_after_set_ms\":" << afterSet << ",\"cancel_code\":" << code
               << ",\"cancel_status\":\"" << jsonEscape(status) << "\"";
            // Is the session still usable after a terminated Run?
            cro.UnsetTerminate();
            auto s2 = Clock::now();
            auto yy = session.Run(cro, inNames, &x, 1, outNames, 1);
            const float* p = yy[0].GetTensorData<float>();
            float d = 0.f;
            for (size_t k = 0; k < outN; ++k) d = std::max(d, std::fabs(p[k] - first[k]));
            std::printf("after cancel    : Run OK in %.1f ms, max |diff vs first| = %g\n", ms(s2, Clock::now()), d);
            js << ",\"after_cancel_ok\":true,\"after_cancel_max_abs\":" << d;
        }

        vram.stop();
        PROCESS_MEMORY_COUNTERS_EX pmc{};
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc);
        std::printf("peak RAM        : working set %.0f MiB, private (pagefile) %.0f MiB\n",
                    pmc.PeakWorkingSetSize / 1048576.0, pmc.PeakPagefileUsage / 1048576.0);
        js << ",\"peak_ws_mib\":" << pmc.PeakWorkingSetSize / 1048576.0
           << ",\"peak_private_mib\":" << pmc.PeakPagefileUsage / 1048576.0;
        if (vram.available()) {
            std::printf("process VRAM    : baseline %.0f MiB, sampled peak %.0f MiB (10 ms sampling)\n",
                        vram.baseline() / 1048576.0, vram.peak() / 1048576.0);
            js << ",\"vram_baseline_mib\":" << vram.baseline() / 1048576.0
               << ",\"vram_peak_mib\":" << vram.peak() / 1048576.0;
        }
        if (a.verbose) {
            std::printf("node placement  :\n");
            for (auto& l : sink.placement) std::printf("  %s\n", l.c_str());
        }
        js << ",\"ok\":true";
    } catch (const Ort::Exception& e) {
        std::fprintf(stderr, "ORT ERROR code %d: %s\n", (int)e.GetOrtErrorCode(), e.what());
        js << ",\"ok\":false,\"error_code\":" << (int)e.GetOrtErrorCode() << ",\"error\":\"" << jsonEscape(e.what()) << "\"";
        exitCode = 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        js << ",\"ok\":false,\"error\":\"" << jsonEscape(e.what()) << "\"";
        exitCode = 2;
    }
    js << "}";
    if (!a.json.empty()) {
        std::ofstream jf(a.json, std::ios::app);
        jf << js.str() << "\n";
    }
    return exitCode;
}
