#include "TileProcessor.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <mdspan>
#include <new>
#include <ranges>

namespace arx::core {

namespace {
// Planar float views. Planes3 = [c][y][x] with 3 channels (RGB planes, the [1,3,T,T] tile
// input / [1,3,Ho,Ho] output with batch 1 left out); Plane = [y][x] (alpha, weights, filter).
using Planes3 = std::extents<size_t, 3, std::dynamic_extent, std::dynamic_extent>;
using Plane = std::dextents<size_t, 2>;
}  // namespace

bool mulOk(uint64_t a, uint64_t b, uint64_t& r) {
    if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a) return false;
    r = a * b;
    return true;
}

uint8_t quantize256(float v) {
    // (ft * 255.0).round_().clamp_(0, 255): float32 multiply, round half to even
    // (std::nearbyint under the default FE_TONEAREST mode), then clamp.
    float t = std::nearbyint(v * 255.0f);
    if (!(t >= 0.0f)) t = 0.0f;  // also maps NaN to 0 (never expected: the model clamps)
    if (t > 255.0f) t = 255.0f;
    return (uint8_t)t;
}

std::expected<TilePlan, Status> makeTilePlan(int width, int height, const TileParams& p, int tile) {
    TilePlan plan;
    if (width < 1 || height < 1) return std::unexpected(Status::fail(ErrorKind::InvalidArgument, "empty image"));
    if (p.scale < 1 || p.offset < 0 || p.blendSize < 0)
        return std::unexpected(Status::fail(ErrorKind::InvalidArgument, "bad model params"));
    // Checked before any Run: the upstream PyTorch assert does not exist in the ONNX graph and
    // illegal sizes either fail (/unet/Add broadcast) or silently give non-upstream results.
    // tools/winml-probe/CMakeLists.txt (tile_runner_anytile) rewrites the next line verbatim.
    if (!ModelContract::isValidTile(p.tileRule, tile))
        return std::unexpected(Status::fail(ErrorKind::InvalidTileSize,
                                            std::format("tile {} violates the tile rule of this model", tile)));
    plan.tile = tile;
    plan.inputOffset = (p.offset + p.scale - 1) / p.scale;      // math.ceil(offset / scale)
    plan.inputBlend = (p.blendSize + p.scale - 1) / p.scale;    // math.ceil(blend_size / scale)
    plan.stepIn = tile - (plan.inputOffset * 2 + plan.inputBlend);
    plan.tileOut = tile * p.scale - p.offset * 2;
    if (plan.stepIn <= 0 || plan.tileOut - 2 * p.blendSize < 0)
        return std::unexpected(Status::fail(ErrorKind::InvalidTileSize, "tile too small for this model"));
    plan.stepOut = plan.stepIn * p.scale;

    // while input_h < x_h + input_offset * 2: input_h = h_blocks * step + tile; h_blocks += 1
    auto blocks = [&](int64_t x, int64_t& inputSize) {
        int64_t n = 0;
        inputSize = 0;
        while (inputSize < x + plan.inputOffset * 2) {
            inputSize = n * plan.stepIn + tile;
            ++n;
        }
        return n;
    };
    int64_t inputH = 0, inputW = 0;
    const int64_t hb = blocks(height, inputH), wb = blocks(width, inputW);
    uint64_t ow = 0, oh = 0;
    if (!mulOk((uint64_t)width, (uint64_t)p.scale, ow) || !mulOk((uint64_t)height, (uint64_t)p.scale, oh) ||
        ow > (uint64_t)std::numeric_limits<int>::max() || oh > (uint64_t)std::numeric_limits<int>::max() ||
        hb > std::numeric_limits<int>::max() || wb > std::numeric_limits<int>::max())
        return std::unexpected(Status::fail(ErrorKind::SizeOverflow, "output size overflows"));
    plan.hBlocks = (int)hb;
    plan.wBlocks = (int)wb;
    plan.padLeft = plan.inputOffset;
    plan.padRight = (int)(inputW - (width + plan.inputOffset));
    plan.padTop = plan.inputOffset;
    plan.padBottom = (int)(inputH - (height + plan.inputOffset));
    plan.outWidth = (int)ow;   // y_w = floor(x_w * scale)
    plan.outHeight = (int)oh;  // y_h
    return plan;
}

void alphaBorderPadding(float* rgb, const float* alpha, int width, int height, int iterations) {
    const size_t n = (size_t)width * height;
    // [c][pixel]: the three planes, pixels in row-major order (box3 works on whole planes).
    const std::mdspan<float, std::extents<size_t, 3, std::dynamic_extent>> planes(rgb, n);
    std::vector<float> mask(n), mw(n), tmp(n), border(n);
    bool anyHole = false;
    for (size_t i = 0; i < n; ++i) {
        mask[i] = alpha[i] > 0.0f ? 1.0f : 0.0f;  // mask[alpha > 0] = 1.0
        if (mask[i] < 1.0f) {
            anyHole = true;
            for (int c = 0; c < 3; ++c) planes[c, i] = 0.0f;  // rgb[:, mask_nega] = 0.0
        }
    }
    // 3x3 sum with zero padding (ChannelWiseSum: Conv2d 3x3, weight 1, padding 1, zeros).
    auto box3 = [&](const float* s, float* d) {
        for (int y = 0; y < height; ++y) {
            const float* r = s + (size_t)y * width;
            float* t = tmp.data() + (size_t)y * width;
            for (int x = 0; x < width; ++x)
                t[x] = (x > 0 ? r[x - 1] : 0.0f) + r[x] + (x + 1 < width ? r[x + 1] : 0.0f);
        }
        for (int y = 0; y < height; ++y) {
            float* o = d + (size_t)y * width;
            const float* t0 = y > 0 ? tmp.data() + (size_t)(y - 1) * width : nullptr;
            const float* t1 = tmp.data() + (size_t)y * width;
            const float* t2 = y + 1 < height ? tmp.data() + (size_t)(y + 1) * width : nullptr;
            for (int x = 0; x < width; ++x) o[x] = (t0 ? t0[x] : 0.0f) + t1[x] + (t2 ? t2[x] : 0.0f);
        }
    };
    for (int it = 0; it < iterations && anyHole; ++it) {
        box3(mask.data(), mw.data());  // mask_weight = sum_alpha(mask)
        for (int c = 0; c < 3; ++c) {
            float* plane = &planes[c, 0];
            box3(plane, border.data());  // border = sum_rgb(rgb)
            for (size_t i = 0; i < n; ++i)
                if (mask[i] < 1.0f) plane[i] = border[i] / (mw[i] + 1e-7f);  // rgb[:, nega] = border / (w + 1e-7)
        }
        anyHole = false;
        for (size_t i = 0; i < n; ++i) {  // mask = (mask_weight > 0)
            mask[i] = mw[i] > 0.0f ? 1.0f : 0.0f;
            anyHole |= mask[i] < 1.0f;
        }
    }
    for (size_t i = 0; i < 3 * n; ++i) rgb[i] = std::clamp(rgb[i], 0.0f, 1.0f);  // rgb.clamp_(0, 1)
}

namespace {

enum class Sink { Rgb, AlphaMean };

struct RenderCtx {
    const ProcessOptions& opt;
    Progress& prog;
    int& tilesRun;
};

bool cancelled(const ProcessOptions& o) { return o.cancel && o.cancel->load(std::memory_order_relaxed); }

// SeamBlending.tiled_render + update + get_output for one 3-channel planar float image, written
// straight into the 8-bit output (channels 0..2 for Sink::Rgb, channel 3 for Sink::AlphaMean).
// Accumulates sum(y*f) and sum(f) and normalizes at the end; upstream normalizes incrementally
// (pixels = pixels*W/(W+f) + y*(1-W/(W+f))), which is the same weighted mean up to float rounding.
Status tiledRender(const float* src, int width, int height, const TilePlan& plan, const TileParams& p,
                   ITileEngine& engine, Sink sink, Image8& out, RenderCtx& ctx) {
    const int T = plan.tile, Ho = plan.tileOut, b = p.blendSize;
    const size_t on = (size_t)plan.outWidth * plan.outHeight;
    const std::mdspan<const float, Planes3> in(src, height, width);

    // create_blend_filter (seam_blending.py:155-162): inner (Ho-2b)^2 ones, then b rings padded
    // outward with value 1 - (1/(b+1))*(i+1), i = 0 (innermost ring) .. b-1 (outermost).
    // ringValue[d] is the value at distance d from the tile edge (d < b), computed in double and
    // stored as float32 like F.pad(value=python float).
    std::vector<float> ringValue((size_t)b + 1, 1.0f);
    for (int d = 0; d < b; ++d) ringValue[d] = (float)(1.0 - (1.0 / (b + 1)) * (b - d));
    std::vector<float> filterBuf((size_t)Ho * Ho);
    const std::mdspan<float, Plane> filter(filterBuf.data(), Ho, Ho);
    for (int y = 0; y < Ho; ++y)
        for (int x = 0; x < Ho; ++x) {
            int d = std::min(std::min(y, x), std::min(Ho - 1 - y, Ho - 1 - x));
            filter[y, x] = d < b ? ringValue[d] : 1.0f;
        }

    std::vector<float> sum, wsum, tileIn, tileOut;
    try {
        sum.assign(3 * on, 0.0f);
        wsum.assign(on, 0.0f);
        tileIn.resize((size_t)3 * T * T);
        tileOut.reserve((size_t)3 * Ho * Ho);
    } catch (const std::bad_alloc&) {
        return Status::fail(ErrorKind::SizeOverflow,
                            std::format("host allocation failed for the blend accumulators ({} output pixels)", on));
    }

    const std::mdspan<float, Planes3> tin(tileIn.data(), T, T);
    const std::mdspan<float, Planes3> acc(sum.data(), plan.outHeight, plan.outWidth);
    const std::mdspan<float, Plane> weight(wsum.data(), plan.outHeight, plan.outWidth);

    std::vector<int> sx(T);
    // Row-major tile order (wi fastest), as upstream: the accumulation order is part of the result.
    for (const auto [hi, wi] : std::views::cartesian_product(std::views::iota(0, plan.hBlocks),
                                                              std::views::iota(0, plan.wBlocks))) {
        if (cancelled(ctx.opt)) return Status::fail(ErrorKind::Cancelled, "cancelled before tile");
        // x = F.pad(x, pad, mode="replicate"); tile = x[:, i:i+T, j:j+T] with i = hi*step, j = wi*step.
        // Replicate padding == clamping the source coordinate.
        const int i0 = hi * plan.stepIn - plan.padTop, j0 = wi * plan.stepIn - plan.padLeft;
        for (int tx = 0; tx < T; ++tx) sx[tx] = std::clamp(j0 + tx, 0, width - 1);
        // Hot loops: mdspan gives the row start, the innermost loop stays on plain pointers
        // (per-element 3-D subscripts measured ~5% slower in release).
        for (int c = 0; c < 3; ++c)
            for (int ty = 0; ty < T; ++ty) {
                const float* row = &in[c, std::clamp(i0 + ty, 0, height - 1), 0];
                float* dst = &tin[c, ty, 0];
                for (int tx = 0; tx < T; ++tx) dst[tx] = row[sx[tx]];
            }

        Status st = engine.run(tileIn.data(), T, Ho, tileOut);
        if (!st.ok()) {
            if (cancelled(ctx.opt)) return Status::fail(ErrorKind::Cancelled, "cancelled during Run: " + st.message);
            return st;
        }
        if (tileOut.size() != (size_t)3 * Ho * Ho)
            return Status::fail(ErrorKind::ContractViolation,
                                std::format("model output has {} values, expected 3x{}x{}", tileOut.size(), Ho, Ho));
        for (float v : tileOut)
            if (!std::isfinite(v)) return Status::fail(ErrorKind::EngineFailure, "non-finite model output");
        ++ctx.tilesRun;

        // update(): pixels[:, step*i : step*i+Ho, step*j : step*j+Ho], clipped to the final crop.
        const std::mdspan<const float, Planes3> tout(tileOut.data(), Ho, Ho);
        const int oy0 = hi * plan.stepOut, ox0 = wi * plan.stepOut;
        const int hh = std::min(Ho, plan.outHeight - oy0), ww = std::min(Ho, plan.outWidth - ox0);
        for (int y = 0; y < hh; ++y) {
            const float* f = &filter[y, 0];
            float* wrow = &weight[oy0 + y, ox0];
            for (int x = 0; x < ww; ++x) wrow[x] += f[x];
            for (int c = 0; c < 3; ++c) {
                const float* yr = &tout[c, y, 0];
                float* srow = &acc[c, oy0 + y, ox0];
                for (int x = 0; x < ww; ++x) srow[x] += yr[x] * f[x];
            }
        }

        // Effective (non-overlapping) share of this tile: rows [oy0, next tile's oy0) etc.
        const int ey = (hi + 1 == plan.hBlocks ? plan.outHeight : std::min(plan.outHeight, oy0 + plan.stepOut)) - oy0;
        const int ex = (wi + 1 == plan.wBlocks ? plan.outWidth : std::min(plan.outWidth, ox0 + plan.stepOut)) - ox0;
        ctx.prog.donePixels += (uint64_t)std::max(ey, 0) * (uint64_t)std::max(ex, 0);
        ++ctx.prog.tilesDone;
        if (ctx.opt.progress) ctx.opt.progress(ctx.prog);
    }

    // get_output(): clamp(pixels[:, :y_h, :y_w], 0, 1); then quantize256 (to_image).
    // Pixel i of a plane is element i of its row-major [y][x] storage.
    const int C = out.channels;
    for (size_t i = 0; i < on; ++i) {
        const float w = wsum[i];
        if (!(w > 0.0f)) return Status::fail(ErrorKind::ContractViolation, "tile plan left an output pixel uncovered");
        float v[3];
        for (int c = 0; c < 3; ++c) v[c] = std::clamp(sum[c * on + i] / w, 0.0f, 1.0f);
        uint8_t* o = out.pixels.data() + i * C;
        if (sink == Sink::Rgb) {
            o[0] = quantize256(v[0]);
            o[1] = quantize256(v[1]);
            o[2] = quantize256(v[2]);
        } else {
            o[3] = quantize256((v[0] + v[1] + v[2]) / 3.0f);  // .mean(0, keepdim=True)
        }
    }
    return Status::success();
}

}  // namespace

std::expected<Image8, Status> upscaleImage(const ImageView& in, const TileParams& rgbModel, ITileEngine& rgbEngine,
                                           const TileParams* alphaModel, ITileEngine* alphaEngine,
                                           const ProcessOptions& opt, ProcessStats* stats) {
    ProcessStats local;
    ProcessStats& st = stats ? *stats : local;
    st = ProcessStats{};
    if (!in.data || in.width < 1 || in.height < 1 || (in.channels != 3 && in.channels != 4) ||
        in.stride < (std::ptrdiff_t)in.width * in.channels)
        return std::unexpected(Status::fail(ErrorKind::InvalidArgument,
                                            "input must be 8-bit RGB or RGBA with stride >= width*channels"));

    uint64_t outPixels = 0, outBytes = 0, sumBytes = 0;
    if (!mulOk((uint64_t)in.width * rgbModel.scale, (uint64_t)in.height * rgbModel.scale, outPixels) ||
        !mulOk(outPixels, (uint64_t)in.channels, outBytes) || !mulOk(outPixels, 16, sumBytes) ||
        sumBytes > (uint64_t)std::numeric_limits<size_t>::max())
        return std::unexpected(Status::fail(ErrorKind::SizeOverflow, "output size overflows"));
    if (outPixels > opt.maxOutputPixels)
        return std::unexpected(Status::fail(
            ErrorKind::SizeOverflow, std::format("output {} px exceeds limit {}", outPixels, opt.maxOutputPixels)));

    // to_tensor: uint8 -> float32 / 255 (pil_io.py:232-240). Planar RGB + separate alpha.
    const size_t n = (size_t)in.width * in.height;
    std::vector<float> rgb, alpha;
    bool hasAlpha = in.channels == 4, blankAlpha = true;
    try {
        rgb.resize(3 * n);
        if (hasAlpha) alpha.resize(n);
    } catch (const std::bad_alloc&) {
        return std::unexpected(Status::fail(ErrorKind::SizeOverflow, "host allocation failed for the input planes"));
    }
    const std::mdspan<float, Planes3> rgbPlanes(rgb.data(), in.height, in.width);
    // Zero extents without alpha: the view must not describe memory that does not exist.
    const std::mdspan<float, Plane> alphaPlane(alpha.data(), hasAlpha ? in.height : 0, hasAlpha ? in.width : 0);
    for (int y = 0; y < in.height; ++y) {
        const uint8_t* r = in.data + y * in.stride;
        for (int x = 0; x < in.width; ++x) {
            for (int c = 0; c < 3; ++c) rgbPlanes[c, y, x] = (float)r[x * in.channels + c] / 255.0f;
            if (hasAlpha) {
                const uint8_t a = r[x * 4 + 3];
                alphaPlane[y, x] = (float)a / 255.0f;
                blankAlpha &= a == 255;  // torch.equal(alpha, ones)
            }
        }
    }
    const bool alphaPass = hasAlpha && !blankAlpha && rgbModel.scale > 1;
    if (alphaPass) {
        if (!alphaModel || !alphaEngine)
            return std::unexpected(
                Status::fail(ErrorKind::InvalidArgument, "non-blank alpha at scale > 1 needs the alpha model"));
        if (alphaModel->scale != rgbModel.scale)
            return std::unexpected(
                Status::fail(ErrorKind::ContractViolation, "alpha model scale differs from the RGB model"));
    }
    if (hasAlpha && !blankAlpha) alphaBorderPadding(rgb.data(), alpha.data(), in.width, in.height, rgbModel.offset);

    Image8 out;
    int tile = opt.tile;
    for (int attempt = 1;; ++attempt) {
        out.clear();
        const auto rgbPlan = makeTilePlan(in.width, in.height, rgbModel, tile);
        if (!rgbPlan) return std::unexpected(rgbPlan.error());
        const TilePlan& plan = *rgbPlan;
        TilePlan aplan;
        if (alphaPass) {
            const auto a = makeTilePlan(in.width, in.height, *alphaModel, tile);
            if (!a) return std::unexpected(a.error());
            aplan = *a;
        }
        try {
            out.pixels.assign((size_t)outBytes, 0);
        } catch (const std::bad_alloc&) {
            return std::unexpected(Status::fail(ErrorKind::SizeOverflow, "host allocation failed for the output image"));
        }
        out.width = plan.outWidth;
        out.height = plan.outHeight;
        out.channels = in.channels;

        Progress prog;
        prog.attempt = attempt;
        prog.totalPixels = outPixels * (alphaPass ? 2 : 1);
        prog.tilesTotal = plan.tiles() + (alphaPass ? aplan.tiles() : 0);
        st.tilesRun = 0;
        RenderCtx ctx{opt, prog, st.tilesRun};

        Status s = tiledRender(rgb.data(), in.width, in.height, plan, rgbModel, rgbEngine, Sink::Rgb, out, ctx);
        if (s.ok() && hasAlpha) {
            if (rgbModel.scale == 1) {
                // noise (1x): alpha is returned unchanged.
                for (int y = 0; y < in.height; ++y)
                    for (int x = 0; x < in.width; ++x)
                        out.pixels[((size_t)y * out.width + x) * 4 + 3] = in.data[y * in.stride + x * 4 + 3];
            } else if (blankAlpha) {
                // F.interpolate(alpha, scale_factor=s, mode="nearest") of an all-1 alpha.
                for (size_t i = 0; i < outPixels; ++i) out.pixels[i * 4 + 3] = 255;
            } else {
                // alpha.expand(3, H, W) -> tiled_render(alpha model) -> mean over channels.
                if (cancelled(opt)) s = Status::fail(ErrorKind::Cancelled, "cancelled before alpha pass");
                else {
                    prog.pass = 1;
                    std::vector<float> a3;
                    try {
                        a3.resize(3 * n);
                    } catch (const std::bad_alloc&) {
                        s = Status::fail(ErrorKind::SizeOverflow, "host allocation failed for the alpha planes");
                    }
                    if (s.ok()) {
                        for (int c = 0; c < 3; ++c) std::copy(alpha.begin(), alpha.end(), a3.begin() + c * n);
                        s = tiledRender(a3.data(), in.width, in.height, aplan, *alphaModel, *alphaEngine,
                                        Sink::AlphaMean, out, ctx);
                    }
                }
            }
        }
        if (s.ok()) {
            st.tileUsed = tile;
            st.attempts = attempt;
            st.alphaPassed = alphaPass;
            return out;
        }
        out.clear();
        // Only an explicit OOM or a lost device may restart, once, with the fallback tile, from a
        // clean state. The engine that failed is unusable: beforeRetry must release it.
        const bool recoverable = s.kind == ErrorKind::OutOfMemory || s.kind == ErrorKind::DeviceLost;
        if (recoverable && opt.allowOomRetry && attempt == 1 && tile != opt.fallbackTile) {
            st.firstAttemptError = s;
            if (opt.beforeRetry) opt.beforeRetry();
            tile = opt.fallbackTile;
            continue;
        }
        st.attempts = attempt;
        return std::unexpected(std::move(s));
    }
}

}  // namespace arx::core
