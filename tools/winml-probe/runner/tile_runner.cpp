// tile_runner: drives the P2 core module (ModelContract + TileProcessor + OrtTileEngine) on one
// image for validation. Development tool; no Qt, no image codecs (raw "ARX8" files from Python).
//
// ARX8 file: "ARX8" + uint32 width + uint32 height + uint32 channels (3|4) + width*height*channels
// bytes, R,G,B[,A], rows tightly packed.
#include "core/inference/OrtTileEngine.h"  // ORT_API_MANUAL_INIT comes from the arx_ort target

#include <windows.h>
#include <dxgi1_6.h>
#include <psapi.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/inference/ModelContract.h"
#include "core/inference/TileProcessor.h"

using namespace arx::core;
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

namespace {

double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

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
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o;
}

struct Args {
    fs::path manifest, modelsRoot, input, output, json, progressLog;
    std::string model, ep = "dml";
    int device = 0, tile = 256, stridePad = 0, cancelAfterMs = -1;
    bool manual = false;
};

[[noreturn]] void usage() {
    std::fputs("usage: tile_runner --manifest M --models-root R --model ID --input in.arx [--output out.arx]\n"
               "                   [--ep dml|cpu] [--device 0] [--tile 256] [--manual] [--stride-pad K]\n"
               "                   [--cancel-after-ms D] [--json results.jsonl] [--progress-log file]\n", stderr);
    std::exit(64);
}

Args parse(int argc, wchar_t** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::wstring k = argv[i];
        auto next = [&]() -> std::wstring { if (i + 1 >= argc) usage(); return argv[++i]; };
        if (k == L"--manifest") a.manifest = next();
        else if (k == L"--models-root") a.modelsRoot = next();
        else if (k == L"--model") a.model = narrow(next());
        else if (k == L"--input") a.input = next();
        else if (k == L"--output") a.output = next();
        else if (k == L"--json") a.json = next();
        else if (k == L"--progress-log") a.progressLog = next();
        else if (k == L"--ep") a.ep = narrow(next());
        else if (k == L"--device") a.device = std::stoi(next());
        else if (k == L"--tile") a.tile = std::stoi(next());
        else if (k == L"--stride-pad") a.stridePad = std::stoi(next());
        else if (k == L"--cancel-after-ms") a.cancelAfterMs = std::stoi(next());
        else if (k == L"--manual") a.manual = true;
        else usage();
    }
    if (a.manifest.empty() || a.modelsRoot.empty() || a.model.empty() || a.input.empty() || (a.ep != "dml" && a.ep != "cpu"))
        usage();
    return a;
}

bool readArx(const fs::path& p, int& w, int& h, int& c, std::vector<uint8_t>& data) {
    std::ifstream f(p, std::ios::binary);
    char magic[4];
    uint32_t hdr[3];
    if (!f.read(magic, 4) || std::memcmp(magic, "ARX8", 4) || !f.read(reinterpret_cast<char*>(hdr), sizeof hdr)) return false;
    w = (int)hdr[0]; h = (int)hdr[1]; c = (int)hdr[2];
    if (w < 1 || h < 1 || (c != 3 && c != 4)) return false;
    uint64_t n = (uint64_t)w * h * c;
    data.resize((size_t)n);
    return (bool)f.read(reinterpret_cast<char*>(data.data()), (std::streamsize)n);
}

bool writeArx(const fs::path& p, const Image8& im) {
    std::ofstream f(p, std::ios::binary);
    uint32_t hdr[3] = {(uint32_t)im.width, (uint32_t)im.height, (uint32_t)im.channels};
    f.write("ARX8", 4);
    f.write(reinterpret_cast<const char*>(hdr), sizeof hdr);
    f.write(reinterpret_cast<const char*>(im.pixels.data()), (std::streamsize)im.pixels.size());
    return (bool)f;
}

ComPtr<IDXGIAdapter3> adapter(int index, std::string& name) {
    ComPtr<IDXGIFactory1> f;
    ComPtr<IDXGIAdapter1> a;
    ComPtr<IDXGIAdapter3> a3;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&f))) || FAILED(f->EnumAdapters1((UINT)index, &a))) return a3;
    DXGI_ADAPTER_DESC1 d{};
    a->GetDesc1(&d);
    name = narrow(d.Description);
    a.As(&a3);
    return a3;
}

uint64_t vramNow(IDXGIAdapter3* a) {
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (!a || FAILED(a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) return 0;
    return info.CurrentUsage;
}

double privateNowMiB() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc);
    return pmc.PrivateUsage / 1048576.0;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    Args a = parse(argc, argv);
    std::ostringstream js;
    js << "{\"model\":\"" << jsonEscape(a.model) << "\",\"ep\":\"" << a.ep << "\",\"tile\":" << a.tile
       << ",\"manual\":" << (a.manual ? "true" : "false") << ",\"input\":\"" << jsonEscape(a.input.filename().string()) << "\"";
    auto finish = [&](int code) {
        js << ",\"exit\":" << code << "}";
        if (!a.json.empty()) std::ofstream(a.json, std::ios::app) << js.str() << "\n";
        std::printf("%s\n", js.str().c_str());
        return code;
    };

    std::vector<wchar_t> exe(32768);
    GetModuleFileNameW(nullptr, exe.data(), (DWORD)exe.size());
    std::string rtInfo;
    Status s = initOrtRuntime(fs::path(exe.data()).parent_path(), WINML_EXPECTED_ORT_VERSION, rtInfo);
    js << ",\"runtime\":\"" << jsonEscape(rtInfo) << "\"";
    if (!s.ok()) { js << ",\"error\":\"" << jsonEscape(s.message) << "\""; return finish(3); }

    ModelContract mc;
    s = ModelContract::load(a.manifest, mc);
    const ModelInfo* m = s.ok() ? mc.find(a.model) : nullptr;
    if (s.ok() && !m) s = Status::fail(ErrorKind::ModelNotFound, "model id not in manifest: " + a.model);
    int w = 0, h = 0, c = 0;
    std::vector<uint8_t> raw;
    if (s.ok() && !readArx(a.input, w, h, c, raw)) s = Status::fail(ErrorKind::InvalidArgument, "bad ARX8 input");
    js << ",\"in_w\":" << w << ",\"in_h\":" << h << ",\"in_c\":" << c;
    const ModelInfo* am = nullptr;
    if (s.ok() && c == 4 && m->scale > 1) {
        am = mc.find(mc.alphaModelFor(m->scale));
        if (!am) s = Status::fail(ErrorKind::ModelNotFound, "alpha model missing for scale " + std::to_string(m->scale));
    }
    fs::path rgbPath, alphaPath;
    auto resolve = [&](const ModelInfo& info, fs::path& path) {
        auto r = mc.resolveVerified(info, a.modelsRoot);
        if (r) path = std::move(*r);
        else s = r.error();
    };
    if (s.ok()) resolve(*m, rgbPath);
    if (s.ok() && am) resolve(*am, alphaPath);
    if (!s.ok()) {
        js << ",\"error_kind\":\"" << errorKindName(s.kind) << "\",\"error\":\"" << jsonEscape(s.message) << "\"";
        return finish(2);
    }
    js << ",\"sha256_verified\":true";

    // Re-pack with a padded stride so the module's stride handling is exercised.
    const std::ptrdiff_t stride = (std::ptrdiff_t)w * c + a.stridePad;
    std::vector<uint8_t> strided((size_t)stride * h, 0xEE);
    for (int y = 0; y < h; ++y) std::memcpy(strided.data() + (size_t)y * stride, raw.data() + (size_t)y * w * c, (size_t)w * c);
    std::vector<uint8_t>().swap(raw);

    std::string adName;
    ComPtr<IDXGIAdapter3> ad = adapter(a.device, adName);
    js << ",\"adapter\":\"" << jsonEscape(adName) << "\"";
    const double privBase = privateNowMiB();
    const uint64_t vramBase = vramNow(ad.Get());

    int code = 0;
    {
        std::atomic<bool> runVram{true};
        std::atomic<uint64_t> vramPeak{vramBase};
        std::thread vt([&] {
            while (runVram) {
                vramPeak = std::max<uint64_t>(vramPeak, vramNow(ad.Get()));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "tile_runner");
        const auto ep = a.ep == "dml" ? OrtTileEngine::Ep::Dml : OrtTileEngine::Ep::Cpu;
        std::unique_ptr<OrtTileEngine> rgbEng, alphaEng;
        auto t0 = Clock::now();
        s = OrtTileEngine::create(env, rgbPath, ep, a.device, mc.inputName(), mc.outputName(), rgbEng);
        if (s.ok() && am && am != m) s = OrtTileEngine::create(env, alphaPath, ep, a.device, mc.inputName(), mc.outputName(), alphaEng);
        auto t1 = Clock::now();
        js << ",\"session_ms\":" << ms(t0, t1);

        Image8 out;
        ProcessStats st;
        std::atomic<bool> cancel{false};
        Clock::time_point tCancel{};
        std::thread canceller;
        std::ofstream plog;
        if (!a.progressLog.empty()) plog.open(a.progressLog);
        double firstTileMs = -1;
        if (s.ok()) {
            ProcessOptions o;
            o.tile = a.tile;
            o.allowOomRetry = !a.manual;
            o.cancel = &cancel;
            auto tStart = Clock::now();
            o.progress = [&](const Progress& p) {
                const double t = ms(tStart, Clock::now());
                if (firstTileMs < 0) firstTileMs = t;
                if (plog) plog << t << " " << p.donePixels << " " << p.totalPixels << " " << p.tilesDone << " "
                               << p.tilesTotal << " " << p.pass << " " << p.attempt << "\n";
            };
            if (a.cancelAfterMs >= 0)
                canceller = std::thread([&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(a.cancelAfterMs));
                    tCancel = Clock::now();
                    cancel = true;
                    // CPU EP: also abort the Run in flight. DML ignores SetTerminate (P1 §6.6).
                    if (ep == OrtTileEngine::Ep::Cpu) {
                        rgbEng->requestTerminate();
                        if (alphaEng) alphaEng->requestTerminate();
                    }
                });
            const TileParams rgbP = ModelContract::tileParams(*m);
            const TileParams alphaP = am ? ModelContract::tileParams(*am) : TileParams{};
            auto r = upscaleImage({strided.data(), w, h, c, stride}, rgbP, *rgbEng, am ? &alphaP : nullptr,
                                  am ? (alphaEng ? alphaEng.get() : rgbEng.get()) : nullptr, o, &st);
            if (r) out = std::move(*r);
            else s = r.error();
            auto t2 = Clock::now();
            if (canceller.joinable()) canceller.join();
            js << ",\"process_ms\":" << ms(t1, t2) << ",\"first_tile_ms\":" << firstTileMs
               << ",\"tiles_run\":" << st.tilesRun << ",\"attempts\":" << st.attempts << ",\"tile_used\":" << st.tileUsed
               << ",\"alpha_pass\":" << (st.alphaPassed ? "true" : "false");
            if (a.cancelAfterMs >= 0)
                js << ",\"cancel_after_ms\":" << a.cancelAfterMs
                   << ",\"cancel_latency_ms\":" << (tCancel.time_since_epoch().count() ? ms(tCancel, t2) : -1);
            js << ",\"private_after_return_mib\":" << privateNowMiB() << ",\"out_pixels_held\":" << out.pixels.size();
        }
        js << ",\"status\":\"" << errorKindName(s.kind) << "\"";
        if (!s.ok()) {
            js << ",\"hresult\":\"" << std::hex << s.hresult << std::dec << "\",\"error\":\"" << jsonEscape(s.message) << "\"";
            code = s.kind == ErrorKind::Cancelled ? 5 : s.kind == ErrorKind::DeviceLost ? 6 : 2;
        } else {
            js << ",\"out_w\":" << out.width << ",\"out_h\":" << out.height << ",\"out_c\":" << out.channels;
            if (!a.output.empty() && !writeArx(a.output, out)) code = 2;
        }
        runVram = false;
        vt.join();
        js << ",\"vram_base_mib\":" << vramBase / 1048576.0 << ",\"vram_peak_mib\":" << vramPeak / 1048576.0;
        out.clear();
        rgbEng.reset();
        alphaEng.reset();
    }
    // Everything (engines, Env, buffers) released: what remains above the baseline?
    js << ",\"private_base_mib\":" << privBase << ",\"private_after_release_mib\":" << privateNowMiB()
       << ",\"vram_after_release_mib\":" << vramNow(ad.Get()) / 1048576.0;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc);
    js << ",\"peak_ws_mib\":" << pmc.PeakWorkingSetSize / 1048576.0 << ",\"peak_private_mib\":" << pmc.PeakPagefileUsage / 1048576.0;
    js << ",\"output_exists\":" << (!a.output.empty() && fs::exists(a.output) ? "true" : "false");
    return finish(code);
}
