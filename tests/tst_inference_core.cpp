// Unit tests for the inference core (P2 module, moved from tools/winml-probe/tests/core_tests.cpp
// in P3; no GPU, no ORT, no Qt): tile plan coverage and alignment,
// sizes, stride, tile rule, cancel, OOM retry, device errors, alpha flow, quantization,
// manifest validation. A fake engine emulates the model as "nearest upscale, crop offset",
// so a correct tiler must reproduce a plain nearest upscale of the whole image exactly.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "core/inference/ModelContract.h"
#include "core/inference/TileProcessor.h"

using namespace arx::core;
namespace fs = std::filesystem;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (cond) ++g_pass;                                                         \
        else { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

struct FakeEngine : ITileEngine {
    TileParams p;
    int calls = 0;
    int failAtCall = -1;  // 1-based
    Status failWith;
    std::vector<int> tiles;
    std::atomic<bool>* cancelAt = nullptr;
    int cancelAfterCalls = -1;
    explicit FakeEngine(TileParams pp) : p(pp) {}
    Status run(const float* in, int T, int outSide, std::vector<float>& out) override {
        ++calls;
        tiles.push_back(T);
        if (calls == failAtCall) return failWith;
        if (cancelAt && calls == cancelAfterCalls) cancelAt->store(true);
        out.assign((size_t)3 * outSide * outSide, 0.f);
        for (int c = 0; c < 3; ++c)
            for (int y = 0; y < outSide; ++y)
                for (int x = 0; x < outSide; ++x)
                    out[((size_t)c * outSide + y) * outSide + x] =
                        in[((size_t)c * T + (y + p.offset) / p.scale) * T + (x + p.offset) / p.scale];
        return Status::success();
    }
};

struct TestImage {
    int w, h, c;
    std::ptrdiff_t stride;
    std::vector<uint8_t> buf;
    ImageView view() const { return {buf.data(), w, h, c, stride}; }
    uint8_t at(int x, int y, int ch) const { return buf[(size_t)y * stride + x * c + ch]; }
};

static TestImage makeImage(int w, int h, int c, int extraStride, unsigned seed, bool blankAlpha = false) {
    TestImage t{w, h, c, (std::ptrdiff_t)w * c + extraStride, {}};
    t.buf.assign((size_t)t.stride * h, 0xCD);  // padding bytes must never be read as pixels
    std::mt19937 rng(seed);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int ch = 0; ch < c; ++ch) {
                uint8_t v = (uint8_t)(rng() & 0xFF);
                if (ch == 3) v = blankAlpha ? 255 : (x % 7 == 0 ? 0 : v);  // fully transparent columns
                t.buf[(size_t)y * t.stride + x * c + ch] = v;
            }
    return t;
}

static const TileParams P1{1, 8, 4}, P2{2, 16, 8}, P4{4, 32, 16};

// Every output pixel equals the nearest-upscaled input (RGB channels; alpha>0 pixels only
// when alpha border padding changed transparent RGB).
static bool equalsNearest(const TestImage& in, const Image8& out, int s, bool onlyOpaque) {
    if (out.width != in.w * s || out.height != in.h * s || out.channels != in.c) return false;
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const int sx = x / s, sy = y / s;
            if (onlyOpaque && in.c == 4 && in.at(sx, sy, 3) == 0) continue;
            for (int ch = 0; ch < 3; ++ch)
                if (out.pixels[((size_t)y * out.width + x) * out.channels + ch] != in.at(sx, sy, ch)) {
                    std::printf("  mismatch at (%d,%d) ch%d: %d vs %d\n", x, y, ch,
                                out.pixels[((size_t)y * out.width + x) * out.channels + ch], in.at(sx, sy, ch));
                    return false;
                }
        }
    return true;
}

static void testCoverage() {
    struct Case { int w, h; };
    const Case sizes[] = {{1, 1}, {1, 300}, {300, 1}, {37, 23}, {257, 131}, {240, 240}, {241, 239}, {700, 333}};
    for (const TileParams& p : {P1, P2, P4})
        for (int tile : {64, 160, 256})
            for (const Case& c : sizes) {
                TestImage in = makeImage(c.w, c.h, 3, 5, (unsigned)(c.w * 31 + c.h + tile));
                FakeEngine e(p);
                Image8 out;
                ProcessOptions o;
                o.tile = tile;
                uint64_t lastDone = 0, total = 0;
                int progressCalls = 0;
                o.progress = [&](const Progress& pr) { lastDone = pr.donePixels; total = pr.totalPixels; ++progressCalls; };
                ProcessStats st;
                Status s = upscaleImage(in.view(), p, e, nullptr, nullptr, o, out, &st);
                CHECK(s.ok());
                const bool eq = equalsNearest(in, out, p.scale, false);
                if (!eq) std::printf("  coverage failed: scale %d tile %d size %dx%d\n", p.scale, tile, c.w, c.h);
                CHECK(eq);
                TilePlan plan;
                makeTilePlan(c.w, c.h, p, tile, plan);
                CHECK(e.calls == plan.tiles());
                CHECK(progressCalls == plan.tiles());
                CHECK(lastDone == total && total == (uint64_t)c.w * c.h * p.scale * p.scale);
            }
}

static void testPlanMatchesUpstream() {
    // Values from SeamBlending.create_config (seam_blending.py) computed by hand:
    // 1x tile 256, 640x480: input_offset 8, blend 4, step 236; h: 256,492,728>=496 -> 3 blocks.
    TilePlan p;
    CHECK(makeTilePlan(640, 480, P1, 256, p).ok());
    CHECK(p.inputOffset == 8 && p.inputBlend == 4 && p.stepIn == 236 && p.tileOut == 240);
    CHECK(p.hBlocks == 3 && p.wBlocks == 3);  // w: 256,492,728 >= 656
    CHECK(p.padLeft == 8 && p.padRight == 728 - (640 + 8) && p.padTop == 8 && p.padBottom == 728 - (480 + 8));
    CHECK(makeTilePlan(640, 480, P4, 256, p).ok());
    CHECK(p.inputOffset == 8 && p.inputBlend == 4 && p.stepOut == 944 && p.tileOut == 960);
    CHECK(p.outWidth == 2560 && p.outHeight == 1920);
}

static void testTileRule() {
    TestImage in = makeImage(50, 50, 3, 0, 1);
    for (int bad : {0, 16, 63, 100, 128, 200, 250, 300, 512}) {
        FakeEngine e(P2);
        Image8 out;
        ProcessOptions o;
        o.tile = bad;
        Status s = upscaleImage(in.view(), P2, e, nullptr, nullptr, o, out);
        CHECK(s.kind == ErrorKind::InvalidTileSize);
        CHECK(e.calls == 0);  // rejected before any Run
        CHECK(out.pixels.empty());
    }
    for (int good : {64, 112, 160, 208, 256, 304, 352, 400, 448, 640})
        CHECK(ModelContract::isValidTile(TileRule::SwinUnet, good));
    for (int bad : {48, 96, 240, 384}) CHECK(!ModelContract::isValidTile(TileRule::SwinUnet, bad));
    for (int good : {64, 96, 160, 240, 256, 352, 384, 400, 448}) CHECK(ModelContract::isValidTile(TileRule::SwinUnetV3, good));
    for (int bad : {0, 16, 48, 120, 408}) CHECK(!ModelContract::isValidTile(TileRule::SwinUnetV3, bad));
    // The rule comes with the model: 240 is refused for swin_unet and runs for v3.
    {
        TestImage in = makeImage(300, 200, 3, 0, 2);
        TileParams v3 = P2;
        v3.tileRule = TileRule::SwinUnetV3;
        FakeEngine e(v3);
        Image8 out;
        ProcessOptions o;
        o.tile = 240;
        CHECK(upscaleImage(in.view(), v3, e, nullptr, nullptr, o, out).ok());
        CHECK(e.calls > 0);
    }
}

static void testCancel() {
    TestImage in = makeImage(700, 500, 3, 0, 2);
    std::atomic<bool> cancel{false};
    FakeEngine e(P2);
    e.cancelAt = &cancel;
    e.cancelAfterCalls = 3;
    Image8 out;
    ProcessOptions o;
    o.tile = 160;
    o.cancel = &cancel;
    Status s = upscaleImage(in.view(), P2, e, nullptr, nullptr, o, out);
    CHECK(s.kind == ErrorKind::Cancelled);
    CHECK(e.calls == 3);  // stops before the 4th tile
    CHECK(out.pixels.empty() && out.width == 0);
}

static void testOomRetryAndDeviceLost() {
    TestImage in = makeImage(300, 200, 3, 0, 3);
    {  // explicit OOM on the first attempt -> one restart with tile 160 from scratch
        FakeEngine e(P2);
        e.failAtCall = 2;
        e.failWith = Status::fail(ErrorKind::OutOfMemory, "E_OUTOFMEMORY", 0x8007000E);
        Image8 out;
        ProcessOptions o;
        o.tile = 256;
        ProcessStats st;
        Status s = upscaleImage(in.view(), P2, e, nullptr, nullptr, o, out, &st);
        CHECK(s.ok());
        CHECK(st.attempts == 2 && st.tileUsed == 160 && st.firstAttemptError.kind == ErrorKind::OutOfMemory);
        CHECK(e.tiles[0] == 256 && e.tiles[1] == 256 && e.tiles[2] == 160);
        CHECK(equalsNearest(in, out, 2, false));
    }
    {  // OOM twice -> fail after the single retry
        struct TwiceOom : FakeEngine {
            using FakeEngine::FakeEngine;
            Status run(const float* i, int T, int o, std::vector<float>& out) override {
                ++calls;
                tiles.push_back(T);
                (void)i; (void)o; out.clear();
                return Status::fail(ErrorKind::OutOfMemory, "oom", 0x8007000E);
            }
        } e(P2);
        Image8 out;
        ProcessOptions o;
        Status s = upscaleImage(in.view(), P2, e, nullptr, nullptr, o, out);
        CHECK(s.kind == ErrorKind::OutOfMemory && e.calls == 2 && out.pixels.empty());
    }
    {  // manual tile: no retry
        FakeEngine e(P2);
        e.failAtCall = 1;
        e.failWith = Status::fail(ErrorKind::OutOfMemory, "oom", 0x8007000E);
        Image8 out;
        ProcessOptions o;
        o.allowOomRetry = false;
        Status s = upscaleImage(in.view(), P2, e, nullptr, nullptr, o, out);
        CHECK(s.kind == ErrorKind::OutOfMemory && e.calls == 1);
    }
    for (uint32_t hr : {0x887A0005u, 0x887A0006u, 0x887A0007u, 0x887A0020u}) {  // never retried
        FakeEngine e(P2);
        e.failAtCall = 2;
        e.failWith = Status::fail(ErrorKind::DeviceLost, "device", hr);
        Image8 out;
        ProcessOptions o;
        Status s = upscaleImage(in.view(), P2, e, nullptr, nullptr, o, out);
        CHECK(s.kind == ErrorKind::DeviceLost && s.hresult == hr && e.calls == 2 && out.pixels.empty());
    }
}

static void testAlpha() {
    {  // blank alpha (all 255) at 2x: alpha model never used, alpha nearest = 255
        TestImage in = makeImage(90, 70, 4, 3, 4, true);
        FakeEngine e(P2);
        Image8 out;
        ProcessOptions o;
        ProcessStats st;
        CHECK(upscaleImage(in.view(), P2, e, nullptr, nullptr, o, out, &st).ok());
        CHECK(!st.alphaPassed && out.channels == 4);
        bool all255 = true;
        for (size_t i = 0; i < (size_t)out.width * out.height; ++i) all255 &= out.pixels[i * 4 + 3] == 255;
        CHECK(all255);
        CHECK(equalsNearest(in, out, 2, false));  // no alpha border padding for blank alpha
    }
    {  // 1x: alpha unchanged
        TestImage in = makeImage(90, 70, 4, 0, 5);
        FakeEngine e(P1);
        Image8 out;
        ProcessOptions o;
        CHECK(upscaleImage(in.view(), P1, e, nullptr, nullptr, o, out).ok());
        bool same = true;
        for (int y = 0; y < in.h; ++y)
            for (int x = 0; x < in.w; ++x) same &= out.pixels[((size_t)y * out.width + x) * 4 + 3] == in.at(x, y, 3);
        CHECK(same);
        CHECK(equalsNearest(in, out, 1, true));
    }
    {  // non-blank alpha at 4x: alpha pass through the alpha engine, mean of 3 equal channels
        TestImage in = makeImage(130, 61, 4, 1, 6);
        FakeEngine e(P4), ea(P4);
        Image8 out;
        ProcessOptions o;
        ProcessStats st;
        CHECK(upscaleImage(in.view(), P4, e, &P4, &ea, o, out, &st).ok());
        CHECK(st.alphaPassed && ea.calls > 0);
        bool alphaNearest = true;
        for (int y = 0; y < out.height; ++y)
            for (int x = 0; x < out.width; ++x)
                alphaNearest &= out.pixels[((size_t)y * out.width + x) * 4 + 3] == in.at(x / 4, y / 4, 3);
        CHECK(alphaNearest);
        CHECK(equalsNearest(in, out, 4, true));
        Image8 out2;  // missing alpha engine -> error, no output
        CHECK(upscaleImage(in.view(), P4, e, nullptr, nullptr, o, out2).kind == ErrorKind::InvalidArgument);
    }
}

static void testAlphaBorderPadding() {
    // 3x3, only the centre is opaque (red). One iteration: every neighbour sees one opaque
    // pixel -> red / (1 + 1e-7f); corners as well (3x3 window).
    std::vector<float> rgb(27, 0.5f), a(9, 0.f);
    a[4] = 1.f;
    rgb[0 * 9 + 4] = 1.f; rgb[1 * 9 + 4] = 0.f; rgb[2 * 9 + 4] = 0.f;
    alphaBorderPadding(rgb.data(), a.data(), 3, 3, 1);
    bool ok = true;
    for (int i = 0; i < 9; ++i) ok &= std::fabs(rgb[i] - 1.f / (1.f + 1e-7f)) < 1e-6f && rgb[9 + i] == 0.f && rgb[18 + i] == 0.f;
    CHECK(ok);
    // pixels with alpha > 0 are never modified
    std::vector<float> rgb2(27), a2(9, 0.2f);
    for (int i = 0; i < 27; ++i) rgb2[i] = i / 27.f;
    auto copy = rgb2;
    alphaBorderPadding(rgb2.data(), a2.data(), 3, 3, 8);
    CHECK(rgb2 == copy);
}

static void testQuantize() {
    bool ok = true;
    for (int k = 0; k < 256; ++k) ok &= quantize256((float)k / 255.0f) == k;
    CHECK(ok);
    CHECK(quantize256(-0.1f) == 0 && quantize256(1.2f) == 255 && quantize256(NAN) == 0);
    CHECK(quantize256(0.5f / 255.0f + 1e-6f) == 1);
}

static void testManifest(const fs::path& manifest, const fs::path& scratch) {
    ModelContract mc;
    Status s = ModelContract::load(manifest, mc);
    CHECK(s.ok());
    if (!s.ok()) { std::printf("  %s\n", s.message.c_str()); return; }
    CHECK(mc.models().size() == 14);
    const ModelInfo* m = mc.find("swin_unet/art/noise3_scale4x");
    CHECK(m && m->scale == 4 && m->offset == 32 && m->blendSize == 16);
    CHECK(m && m->tileRule == TileRule::SwinUnet);
    const ModelInfo* v3 = mc.find("swin_unet_v3/art/noise3");
    CHECK(v3 && v3->scale == 1 && v3->offset == 8 && v3->tileRule == TileRule::SwinUnetV3);
    const ModelInfo* v32 = mc.find("swin_unet_v3/art/noise2_scale2x");
    CHECK(v32 && v32->scale == 2 && v32->offset == 16 && v32->blendSize == 8 && v32->tileRule == TileRule::SwinUnetV3);
    CHECK(!mc.find("swin_unet/art/noise3") && !mc.find("swin_unet/art/scale2x"));  // replaced by v3
    CHECK(mc.alphaModelFor(2) == "swin_unet_v3/art/scale2x" && mc.alphaModelFor(4) == "swin_unet/art/scale4x");
    for (const ModelInfo& info : mc.models())
        for (int t : {160, 256, 352, 448}) CHECK(ModelContract::isValidTile(info, t));
    CHECK(mc.alphaModelFor(1).empty());
    CHECK(mc.inputName() == "x" && mc.outputName() == "y");

    // Hash mismatch: a file with the right name but different contents.
    fs::create_directories(scratch / "swin_unet/art");
    { std::ofstream(scratch / "swin_unet/art/noise3_scale4x.onnx") << "not a model"; }
    fs::path resolved;
    CHECK(mc.resolveVerified(*m, scratch, resolved).kind == ErrorKind::HashMismatch);
    fs::remove(scratch / "swin_unet/art/noise3_scale4x.onnx");
    CHECK(mc.resolveVerified(*m, scratch, resolved).kind == ErrorKind::ModelNotFound);

    // Unsafe relative paths are rejected when the manifest is loaded.
    std::ifstream f(manifest, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(f)), {});
    for (const char* evil : {"../evil.onnx", "C:/x.onnx", "swin_unet/../../x.onnx", "/abs.onnx"}) {
        std::string t = text;
        const std::string from = "\"file\": \"swin_unet_v3/art/noise0.onnx\"";
        t.replace(t.find(from), from.size(), std::string("\"file\": \"") + evil + "\"");
        { std::ofstream(scratch / "evil.json", std::ios::binary) << t; }
        ModelContract bad;
        CHECK(ModelContract::load(scratch / "evil.json", bad).kind == ErrorKind::ContractViolation);
    }
    // A model without a known tile rule is refused.
    {
        std::string t = text;
        const std::string from = "\"tile_rule\": \"swin_unet_v3\"";
        t.replace(t.find(from), from.size(), "\"tile_rule\": \"other\"");
        { std::ofstream(scratch / "rule.json", std::ios::binary) << t; }
        ModelContract bad;
        CHECK(ModelContract::load(scratch / "rule.json", bad).kind == ErrorKind::ContractViolation);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: core_tests <models/manifest.json> <scratch dir>\n");
        return 64;
    }
    testCoverage();
    testPlanMatchesUpstream();
    testTileRule();
    testCancel();
    testOomRetryAndDeviceLost();
    testAlpha();
    testAlphaBorderPadding();
    testQuantize();
    testManifest(argv[1], argv[2]);
    std::printf("core_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
