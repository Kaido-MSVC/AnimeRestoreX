// Unit tests for the inference core (P2 module, moved from tools/winml-probe/tests/core_tests.cpp
// in P3; no GPU, no ORT, no Qt): tile plan coverage and alignment,
// sizes, stride, tile rule, cancel, OOM retry, device errors, alpha flow, quantization,
// manifest validation. A fake engine emulates the model as "nearest upscale, crop offset",
// so a correct tiler must reproduce a plain nearest upscale of the whole image exactly.
#include <algorithm>
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
                ProcessOptions o;
                o.tile = tile;
                uint64_t lastDone = 0, total = 0;
                int progressCalls = 0;
                o.progress = [&](const Progress& pr) { lastDone = pr.donePixels; total = pr.totalPixels; ++progressCalls; };
                ProcessStats st;
                const auto out = upscaleImage(in.view(), p, e, nullptr, nullptr, o, &st);
                CHECK(out.has_value());
                if (!out) continue;
                const bool eq = equalsNearest(in, *out, p.scale, false);
                if (!eq) std::printf("  coverage failed: scale %d tile %d size %dx%d\n", p.scale, tile, c.w, c.h);
                CHECK(eq);
                const auto plan = makeTilePlan(c.w, c.h, p, tile);
                CHECK(plan && e.calls == plan->tiles());
                CHECK(plan && progressCalls == plan->tiles());
                CHECK(lastDone == total && total == (uint64_t)c.w * c.h * p.scale * p.scale);
            }
}

static void testPlanMatchesUpstream() {
    // Values from SeamBlending.create_config (seam_blending.py) computed by hand:
    // 1x tile 256, 640x480: input_offset 8, blend 4, step 236; h: 256,492,728>=496 -> 3 blocks.
    const auto r1 = makeTilePlan(640, 480, P1, 256);
    CHECK(r1.has_value());
    const TilePlan p = r1.value_or(TilePlan{});
    CHECK(p.inputOffset == 8 && p.inputBlend == 4 && p.stepIn == 236 && p.tileOut == 240);
    CHECK(p.hBlocks == 3 && p.wBlocks == 3);  // w: 256,492,728 >= 656
    CHECK(p.padLeft == 8 && p.padRight == 728 - (640 + 8) && p.padTop == 8 && p.padBottom == 728 - (480 + 8));
    const auto r4 = makeTilePlan(640, 480, P4, 256);
    CHECK(r4.has_value());
    const TilePlan q = r4.value_or(TilePlan{});
    CHECK(q.inputOffset == 8 && q.inputBlend == 4 && q.stepOut == 944 && q.tileOut == 960);
    CHECK(q.outWidth == 2560 && q.outHeight == 1920);
    // Errors carry only the Status (no plan).
    const auto bad = makeTilePlan(640, 480, P1, 250);
    CHECK(!bad && bad.error().kind == ErrorKind::InvalidTileSize);
    CHECK(!makeTilePlan(0, 480, P1, 256) && makeTilePlan(0, 480, P1, 256).error().kind == ErrorKind::InvalidArgument);
}

static void testTileRule() {
    TestImage in = makeImage(50, 50, 3, 0, 1);
    for (int bad : {0, 16, 63, 100, 128, 200, 250, 300, 512}) {
        FakeEngine e(P2);
        ProcessOptions o;
        o.tile = bad;
        const auto out = upscaleImage(in.view(), P2, e, nullptr, nullptr, o);
        CHECK(!out && out.error().kind == ErrorKind::InvalidTileSize);
        CHECK(e.calls == 0);  // rejected before any Run
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
        ProcessOptions o;
        o.tile = 240;
        CHECK(upscaleImage(in.view(), v3, e, nullptr, nullptr, o).has_value());
        CHECK(e.calls > 0);
    }
}

static void testCancel() {
    TestImage in = makeImage(700, 500, 3, 0, 2);
    std::atomic<bool> cancel{false};
    FakeEngine e(P2);
    e.cancelAt = &cancel;
    e.cancelAfterCalls = 3;
    ProcessOptions o;
    o.tile = 160;
    o.cancel = &cancel;
    const auto out = upscaleImage(in.view(), P2, e, nullptr, nullptr, o);
    CHECK(!out && out.error().kind == ErrorKind::Cancelled);  // no partial image
    CHECK(e.calls == 3);  // stops before the 4th tile
}

static void testOomRetryAndDeviceLost() {
    TestImage in = makeImage(300, 200, 3, 0, 3);
    {  // explicit OOM on the first attempt -> one restart with tile 160 from scratch
        FakeEngine e(P2);
        e.failAtCall = 2;
        e.failWith = Status::fail(ErrorKind::OutOfMemory, "E_OUTOFMEMORY", 0x8007000E);
        ProcessOptions o;
        o.tile = 256;
        int callsAtRebuild = -1;
        o.beforeRetry = [&] { callsAtRebuild = e.calls; };
        ProcessStats st;
        const auto out = upscaleImage(in.view(), P2, e, nullptr, nullptr, o, &st);
        CHECK(out.has_value());
        CHECK(st.attempts == 2 && st.tileUsed == 160 && st.firstAttemptError.kind == ErrorKind::OutOfMemory);
        CHECK(e.tiles[0] == 256 && e.tiles[1] == 256 && e.tiles[2] == 160);
        CHECK(callsAtRebuild == 2);  // released after the failed Run, before any Run at 160
        CHECK(out && equalsNearest(in, *out, 2, false));
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
        ProcessOptions o;
        int rebuilds = 0;
        o.beforeRetry = [&] { ++rebuilds; };
        const auto out = upscaleImage(in.view(), P2, e, nullptr, nullptr, o);
        CHECK(!out && out.error().kind == ErrorKind::OutOfMemory && e.calls == 2);
        CHECK(rebuilds == 1);
    }
    {  // manual tile: no retry
        FakeEngine e(P2);
        e.failAtCall = 1;
        e.failWith = Status::fail(ErrorKind::OutOfMemory, "oom", 0x8007000E);
        ProcessOptions o;
        o.allowOomRetry = false;
        int rebuilds = 0;
        o.beforeRetry = [&] { ++rebuilds; };
        const auto out = upscaleImage(in.view(), P2, e, nullptr, nullptr, o);
        CHECK(!out && out.error().kind == ErrorKind::OutOfMemory && e.calls == 1 && rebuilds == 0);
    }
    for (uint32_t hr : {0x887A0005u, 0x887A0006u, 0x887A0007u, 0x887A0020u}) {  // recovered like OOM
        FakeEngine e(P2);
        e.failAtCall = 2;
        e.failWith = Status::fail(ErrorKind::DeviceLost, "device", hr);
        ProcessOptions o;
        int rebuilds = 0;
        o.beforeRetry = [&] { ++rebuilds; };
        ProcessStats st;
        const auto out = upscaleImage(in.view(), P2, e, nullptr, nullptr, o, &st);
        CHECK(out && st.tileUsed == 160 && rebuilds == 1 && st.firstAttemptError.hresult == hr);
    }
    for (uint32_t hr : {0x887A0005u, 0x887A0006u, 0x887A0007u, 0x887A0020u}) {  // manual: no retry
        FakeEngine e(P2);
        e.failAtCall = 2;
        e.failWith = Status::fail(ErrorKind::DeviceLost, "device", hr);
        ProcessOptions o;
        o.allowOomRetry = false;
        const auto out = upscaleImage(in.view(), P2, e, nullptr, nullptr, o);
        CHECK(!out && out.error().kind == ErrorKind::DeviceLost && out.error().hresult == hr && e.calls == 2);
    }
    {  // any other engine error: no retry
        FakeEngine e(P2);
        e.failAtCall = 1;
        e.failWith = Status::fail(ErrorKind::EngineFailure, "engine");
        ProcessOptions o;
        const auto out = upscaleImage(in.view(), P2, e, nullptr, nullptr, o);
        CHECK(!out && out.error().kind == ErrorKind::EngineFailure && e.calls == 1);
    }
}

static void testAlpha() {
    {  // blank alpha (all 255) at 2x: alpha model never used, alpha nearest = 255
        TestImage in = makeImage(90, 70, 4, 3, 4, true);
        FakeEngine e(P2);
        ProcessOptions o;
        ProcessStats st;
        const Image8 out = upscaleImage(in.view(), P2, e, nullptr, nullptr, o, &st).value_or(Image8{});
        CHECK(!st.alphaPassed && out.channels == 4);
        bool all255 = true;
        for (size_t i = 0; i < (size_t)out.width * out.height; ++i) all255 &= out.pixels[i * 4 + 3] == 255;
        CHECK(all255);
        CHECK(equalsNearest(in, out, 2, false));  // no alpha border padding for blank alpha
    }
    {  // 1x: alpha unchanged
        TestImage in = makeImage(90, 70, 4, 0, 5);
        FakeEngine e(P1);
        ProcessOptions o;
        const auto r = upscaleImage(in.view(), P1, e, nullptr, nullptr, o);
        CHECK(r.has_value());
        if (!r) return;
        const Image8& out = *r;
        bool same = true;
        for (int y = 0; y < in.h; ++y)
            for (int x = 0; x < in.w; ++x) same &= out.pixels[((size_t)y * out.width + x) * 4 + 3] == in.at(x, y, 3);
        CHECK(same);
        CHECK(equalsNearest(in, out, 1, true));
    }
    {  // non-blank alpha at 4x: alpha pass through the alpha engine, mean of 3 equal channels
        TestImage in = makeImage(130, 61, 4, 1, 6);
        FakeEngine e(P4), ea(P4);
        ProcessOptions o;
        ProcessStats st;
        const auto r = upscaleImage(in.view(), P4, e, &P4, &ea, o, &st);
        CHECK(r.has_value());
        if (!r) return;
        const Image8& out = *r;
        CHECK(st.alphaPassed && ea.calls > 0);
        bool alphaNearest = true;
        for (int y = 0; y < out.height; ++y)
            for (int x = 0; x < out.width; ++x)
                alphaNearest &= out.pixels[((size_t)y * out.width + x) * 4 + 3] == in.at(x / 4, y / 4, 3);
        CHECK(alphaNearest);
        CHECK(equalsNearest(in, out, 4, true));
        const auto missing = upscaleImage(in.view(), P4, e, nullptr, nullptr, o);  // no alpha engine -> error
        CHECK(!missing && missing.error().kind == ErrorKind::InvalidArgument);
    }
}

// tta_split written out with torch semantics on an H x W plane (dims (1, 2) = (y, x)):
// flip(x, (2,)) reverses x, flip(x, (1,)) reverses y, rot90(x, 1, (1, 2))[i][j] = x[j][W-1-i].
using Plane2 = std::vector<std::vector<int>>;
static Plane2 hflipP(const Plane2& p) {
    Plane2 r = p;
    for (auto& row : r) std::reverse(row.begin(), row.end());
    return r;
}
static Plane2 vflipP(const Plane2& p) { return Plane2(p.rbegin(), p.rend()); }
static Plane2 rot90P(const Plane2& p) {
    const size_t H = p.size(), W = p[0].size();
    Plane2 r(W, std::vector<int>(H));
    for (size_t i = 0; i < W; ++i)
        for (size_t j = 0; j < H; ++j) r[i][j] = p[j][W - 1 - i];
    return r;
}

static void testTtaMap() {
    for (auto [H, W] : {std::pair{1, 1}, {3, 5}, {4, 2}, {7, 7}}) {
        Plane2 x(H, std::vector<int>(W));
        for (int y = 0; y < H; ++y)
            for (int c = 0; c < W; ++c) x[y][c] = y * W + c;  // value = own index
        const Plane2 tr = rot90P(x);
        const Plane2 split[8] = {x, hflipP(x), vflipP(x), hflipP(vflipP(x)),
                                 tr, hflipP(tr), vflipP(tr), hflipP(vflipP(tr))};
        bool ok = true;
        for (int k = 0; k < 8; ++k) {
            const TtaMap m = ttaMap(k, H, W);
            const Plane2& v = split[k];
            ok &= (int)v.size() == (k >= 4 ? W : H) && (int)v[0].size() == (k >= 4 ? H : W);
            for (int y = 0; y < (int)v.size(); ++y)
                for (int c = 0; c < (int)v[0].size(); ++c) ok &= v[y][c] == m.a + m.b * y + m.c * c;
        }
        if (!ok) std::printf("  ttaMap mismatch for %dx%d\n", W, H);
        CHECK(ok);
    }
}

// The nearest fake engine commutes with flips and rot90, so all 8 TTA renders give the plain
// result in the original orientation: TTA on must equal TTA off (catches a wrong transform,
// inverse or rotated tile plan). Non-square sizes, RGB and RGBA (alpha pass).
// Exact everywhere, except the RGB of fully transparent pixels: alpha border padding fills them
// with values like (a+b)/(2+1e-7), and the 8-term float sum * 1/8 of tta_merge moves ~2.7% of
// such values across a quantize256 rounding boundary (checked by brute force; opaque pixels
// hold k/255 and never do). Those may differ by 1; their alpha is 0 either way.
// Film grain, apply step: fixed noise values against tests/data/grain_reference.txt (numpy float32
// version of nunif apply_rgb_noise; it matches the torch original with 8-bit max diff 0). Max diff 1.
static void testGrainMatchesUpstream(const char* dataDir) {
    std::ifstream f(fs::path(dataDir) / "grain_reference.txt");
    CHECK(f.good());
    int lines = 0, maxDiff = 0, differ = 0, values = 0;
    std::string strength;
    while (f >> strength) {
        const float s = std::stof(strength);
        for (int k = 0; k < 256; ++k)
            for (int j = 0; j <= 16; ++j) {
                int want = -1;
                f >> want;
                const float v = (float)k / 255.0f, n = ((float)j - 8.0f) / 4.0f;
                const int got = quantize256(applyGrain(v, n, s));
                const int d = std::abs(got - want);
                maxDiff = std::max(maxDiff, d);
                differ += d != 0;
                ++values;
            }
        ++lines;
    }
    std::printf("  grain vs numpy reference: %d strengths, %d values, %d differ, max diff %d\n", lines, values, differ,
                maxDiff);
    CHECK(lines == 3 && values == 3 * 256 * 17);
    CHECK(maxDiff <= 1);
    // Zero noise or zero strength leaves the value unchanged up to the gamma round trip.
    for (int k = 0; k < 256; ++k) CHECK(quantize256(applyGrain((float)k / 255.0f, 0.0f, 0.1f)) == k);
}

// Film grain, noise field: N(0,1) values, level-2 mix of full and half resolution, nearest index
// as ATen, and the seed as a pure function of size and strength.
static void testGrainField() {
    // grainNormal: mean 0, variance 1 (1e6 samples), and a pure function of its arguments.
    double sum = 0, sq = 0;
    const int N = 1'000'000;
    for (int i = 0; i < N; ++i) {
        const double v = grainNormal(12345, 0, (uint64_t)i);
        sum += v;
        sq += v * v;
    }
    const double mean = sum / N, var = sq / N - mean * mean;
    std::printf("  grainNormal: mean %.5f, variance %.5f\n", mean, var);
    CHECK(std::abs(mean) < 0.005 && std::abs(var - 1.0) < 0.01);
    CHECK(grainNormal(7, 1, 99) == grainNormal(7, 1, 99));
    CHECK(grainNormal(7, 1, 99) != grainNormal(8, 1, 99) && grainNormal(7, 0, 99) != grainNormal(7, 1, 99));

    // F.interpolate(size=5, mode="nearest") from 2: [0,0,0,1,1]; 2x: dst >> 1; same size: dst.
    const int idx5[] = {0, 0, 0, 1, 1};
    for (int x = 0; x < 5; ++x) CHECK(GrainField::nearestSource(x, 2, 5) == idx5[x]);
    for (int x = 0; x < 8; ++x) CHECK(GrainField::nearestSource(x, 4, 8) == x / 2);
    CHECK(GrainField::nearestSource(3, 1, 1) == 3);

    // at() = 0.5 * full(c,y,x) + 0.5 * half(c, y', x'); the half field repeats in 2x2 blocks.
    const uint64_t seed = grainSeed(7, 5, 0.1f);
    const GrainField g(seed, 7, 5);  // half field 3 x 2: odd sizes use the general nearest index
    bool mix = true;
    for (int c = 0; c < 3; ++c)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x) {
                const float n1 = grainNormal(seed, 0, ((uint64_t)c * 5 + y) * 7 + x);
                const int sy = GrainField::nearestSource(y, 2, 5), sx = GrainField::nearestSource(x, 3, 7);
                const float n2 = grainNormal(seed, 1, ((uint64_t)c * 2 + sy) * 3 + sx);
                mix = mix && g.at(c, y, x) == n1 * 0.5f + 0.5f * n2;
            }
    CHECK(mix);
    CHECK(grainSeed(7, 5, 0.1f) == seed && grainSeed(5, 7, 0.1f) != seed && grainSeed(7, 5, 0.25f) != seed);
    const GrainField one(seed, 1, 1);  // upstream randn(0, 0) would fail; one half-res pixel here
    CHECK(std::isfinite(one.at(2, 0, 0)));
}

// Film grain through upscaleImage: applied to the final float RGB before quantize256 (TTA too),
// alpha untouched, same seed and same result every time, off = no change at all.
static void testGrainThroughUpscale() {
    const float s = 0.1f;
    for (int c : {3, 4}) {
        TestImage in = makeImage(61, 37, c, 1, 77u + c);
        ProcessOptions o;
        o.tile = 64;
        FakeEngine e0(P2), a0(P2), e1(P2), a1(P2), e2(P2), a2(P2), e3(P2), a3(P2);
        const auto plain = upscaleImage(in.view(), P2, e0, &P2, &a0, o);
        o.grainStrength = s;
        ProcessStats st;
        const auto g1 = upscaleImage(in.view(), P2, e1, &P2, &a1, o, &st);
        const auto g2 = upscaleImage(in.view(), P2, e2, &P2, &a2, o);
        CHECK(plain && g1 && g2);
        if (!plain || !g1 || !g2) continue;
        CHECK(g1->pixels == g2->pixels);  // deterministic
        CHECK(st.grainSeed == grainSeed(122, 74, s));
        // Expected: grain on the nearest-upscaled input value (the fake model's exact output; the
        // seam blend may move it by float rounding, hence max diff 1 and only rarely).
        const GrainField field(st.grainSeed, 122, 74);
        int maxDiff = 0, differ = 0, changed = 0;
        bool alphaSame = true;
        for (int y = 0; y < g1->height; ++y)
            for (int x = 0; x < g1->width; ++x) {
                const size_t i = ((size_t)y * g1->width + x) * c;
                if (c == 4) alphaSame = alphaSame && g1->pixels[i + 3] == plain->pixels[i + 3];
                // alpha = 0: alpha border padding replaced the RGB, the input is not the model input.
                if (c == 4 && in.at(x / 2, y / 2, 3) == 0) continue;
                for (int ch = 0; ch < 3; ++ch) {
                    const float v = (float)in.at(x / 2, y / 2, ch) / 255.0f;
                    const int want = quantize256(applyGrain(v, field.at(ch, y, x), s));
                    const int d = std::abs(want - (int)g1->pixels[i + ch]);
                    maxDiff = std::max(maxDiff, d);
                    differ += d != 0;
                    changed += g1->pixels[i + ch] != plain->pixels[i + ch];
                }
            }
        std::printf("  grain c=%d: %d of %d values changed by the grain; vs expected %d differ, max %d\n", c, changed,
                    g1->width * g1->height * 3, differ, maxDiff);
        CHECK(maxDiff <= 1 && changed > 0);
        CHECK(alphaSame);
        // TTA: grain after the merge, same field.
        o.tta = true;
        const auto t = upscaleImage(in.view(), P2, e3, &P2, &a3, o);
        CHECK(t.has_value());
        if (t) {
            int td = 0;
            for (size_t i = 0; i < t->pixels.size(); ++i)
                td = std::max(td, std::abs((int)t->pixels[i] - (int)g1->pixels[i]));
            CHECK(td <= 1);
        }
    }
    // Strength outside (0, 1] is rejected before any Run.
    TestImage in = makeImage(8, 8, 3, 0, 1);
    FakeEngine e(P2);
    ProcessOptions o;
    o.grainStrength = 1.5f;
    const auto r = upscaleImage(in.view(), P2, e, nullptr, nullptr, o);
    CHECK(!r && r.error().kind == ErrorKind::InvalidArgument && e.calls == 0);
}

static void testTtaMatchesPlain() {
    struct Case { int w, h, c; bool holes; };
    const Case cases[] = {{37, 23, 3, false},  {257, 131, 3, false}, {1, 70, 3, false},
                          {131, 61, 4, false}, {131, 61, 4, true},   {90, 170, 4, true}};
    for (const TileParams& p : {P1, P2, P4})
        for (int tile : {64, 160})
            for (const Case& cs : cases) {
                TestImage in = makeImage(cs.w, cs.h, cs.c, 3, (unsigned)(cs.w * 7 + cs.h + tile));
                if (cs.c == 4 && !cs.holes)  // non-blank alpha without fully transparent pixels
                    for (int y = 0; y < cs.h; ++y)
                        for (int x = 0; x < cs.w; ++x) {
                            uint8_t& a = in.buf[(size_t)y * in.stride + x * 4 + 3];
                            a = (uint8_t)std::clamp((int)a, 1, 254);
                        }
                ProcessOptions o;
                o.tile = tile;
                FakeEngine e0(p), a0(p), e1(p), a1(p);
                const auto plain = upscaleImage(in.view(), p, e0, &p, &a0, o);
                o.tta = true;
                uint64_t lastDone = 0, total = 0;
                int lastTiles = 0, tilesTotal = 0;
                o.progress = [&](const Progress& pr) {
                    lastDone = pr.donePixels; total = pr.totalPixels;
                    lastTiles = pr.tilesDone; tilesTotal = pr.tilesTotal;
                };
                ProcessStats st;
                const auto tta = upscaleImage(in.view(), p, e1, &p, &a1, o, &st);
                CHECK(plain && tta);
                if (!plain || !tta) continue;
                bool same = plain->width == tta->width && plain->height == tta->height &&
                            plain->channels == tta->channels && plain->pixels.size() == tta->pixels.size();
                int strictDiffs = 0, holeDiffs = 0;
                for (size_t i = 0; same && i < plain->pixels.size(); ++i) {
                    const int d = std::abs((int)plain->pixels[i] - (int)tta->pixels[i]);
                    if (!d) continue;
                    const size_t px = i / cs.c;
                    const int x = (int)(px % plain->width) / p.scale, y = (int)(px / plain->width) / p.scale;
                    const bool holeRgb = cs.c == 4 && i % 4 != 3 && in.at(x, y, 3) == 0;
                    if (holeRgb && d == 1) ++holeDiffs;
                    else ++strictDiffs;
                }
                same &= strictDiffs == 0;
                if (!same)
                    std::printf("  TTA vs plain: scale %d tile %d %dx%dx%d%s: %d exact-pixel diffs, %d hole RGB diffs (<= 1)\n",
                                p.scale, tile, cs.w, cs.h, cs.c, cs.holes ? " holes" : "", strictDiffs, holeDiffs);
                CHECK(same);
                if (!cs.holes) CHECK(plain->pixels == tta->pixels);
                CHECK(st.tta && st.attempts == 1 && st.firstAttemptError.kind == ErrorKind::None);
                // 8 RGB renders (rotated ones planned for h x w), alpha once.
                const auto pw = makeTilePlan(cs.w, cs.h, p, tile), ph = makeTilePlan(cs.h, cs.w, p, tile);
                CHECK(pw && ph && e1.calls == 4 * (pw->tiles() + ph->tiles()));
                const bool alphaPass = cs.c == 4 && p.scale > 1;
                CHECK(a1.calls == (alphaPass ? pw->tiles() : 0) && a1.calls == a0.calls);
                const uint64_t outPx = (uint64_t)cs.w * cs.h * p.scale * p.scale;
                CHECK(total == outPx * (8 + (alphaPass ? 1 : 0)) && lastDone == total);
                CHECK(lastTiles == tilesTotal && tilesTotal == e1.calls + a1.calls);
            }
}

static void testTtaRetryAndCancel() {
    TestImage in = makeImage(300, 200, 4, 0, 9);
    {  // OOM in the 4th variant: all 8 are redone at 160 from scratch
        FakeEngine e(P2), ea(P2);
        ProcessOptions o;
        o.tile = 256;
        o.tta = true;
        const auto pw = makeTilePlan(300, 200, P2, 256), ph = makeTilePlan(200, 300, P2, 256);
        e.failAtCall = 3 * pw->tiles() + 1;  // first tile of variant 3
        e.failWith = Status::fail(ErrorKind::OutOfMemory, "oom", 0x8007000E);
        int rebuilds = 0;
        o.beforeRetry = [&] { ++rebuilds; };
        ProcessStats st;
        const auto out = upscaleImage(in.view(), P2, e, &P2, &ea, o, &st);
        CHECK(out && rebuilds == 1 && st.attempts == 2 && st.tileUsed == 160 && st.tta);
        const auto qw = makeTilePlan(300, 200, P2, 160), qh = makeTilePlan(200, 300, P2, 160);
        const int before = 3 * pw->tiles() + 1;
        CHECK(e.calls == before + 4 * (qw->tiles() + qh->tiles()));
        bool all160 = true;
        for (size_t i = (size_t)before; i < e.tiles.size(); ++i) all160 &= e.tiles[i] == 160;
        CHECK(all160 && ea.calls == qw->tiles());
        CHECK(out && equalsNearest(in, *out, 2, true));
        CHECK(ph && st.firstAttemptError.kind == ErrorKind::OutOfMemory);
    }
    {  // cancel between tiles of a later variant: no image
        std::atomic<bool> cancel{false};
        FakeEngine e(P2), ea(P2);
        e.cancelAt = &cancel;
        e.cancelAfterCalls = 10;
        ProcessOptions o;
        o.tile = 160;
        o.tta = true;
        o.cancel = &cancel;
        const auto out = upscaleImage(in.view(), P2, e, &P2, &ea, o);
        CHECK(!out && out.error().kind == ErrorKind::Cancelled && e.calls == 10 && ea.calls == 0);
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
    const auto mismatch = mc.resolveVerified(*m, scratch);
    CHECK(!mismatch && mismatch.error().kind == ErrorKind::HashMismatch);
    fs::remove(scratch / "swin_unet/art/noise3_scale4x.onnx");
    const auto missing = mc.resolveVerified(*m, scratch);
    CHECK(!missing && missing.error().kind == ErrorKind::ModelNotFound);

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
    if (argc < 4) {
        std::printf("usage: core_tests <models/manifest.json> <scratch dir> <tests/data dir>\n");
        return 64;
    }
    testGrainMatchesUpstream(argv[3]);
    testGrainField();
    testGrainThroughUpscale();
    testCoverage();
    testPlanMatchesUpstream();
    testTileRule();
    testCancel();
    testOomRetryAndDeviceLost();
    testAlpha();
    testTtaMap();
    testTtaMatchesPlain();
    testTtaRetryAndCancel();
    testAlphaBorderPadding();
    testQuantize();
    testManifest(argv[1], argv[2]);
    std::printf("core_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
