// Tiled inference for nunif swin_unet models, following upstream nunif exactly:
//   nunif/utils/seam_blending.py  SeamBlending.create_config / create_blend_filter / update / tiled_render
//   nunif/utils/alpha.py           AlphaBorderPadding
//   waifu2x/utils.py               Waifu2x.convert (alpha flow)
//   nunif/transforms/functional.py quantize256
// at nunif commit 0d3d92c8f4396da564c44524b6390ee5c76b9668 (see docs/v2.0-P2報告.md).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <vector>

#include "ModelContract.h"
#include "Status.h"

namespace arx::core {

// 8-bit interleaved image, byte order R,G,B[,A] (straight alpha). `stride` in bytes.
struct ImageView {
    const uint8_t* data = nullptr;
    int width = 0, height = 0;
    int channels = 0;  // 3 = RGB, 4 = RGBA
    std::ptrdiff_t stride = 0;
};

// Owned, tightly packed 8-bit result (stride = width * channels).
struct Image8 {
    int width = 0, height = 0, channels = 0;
    std::vector<uint8_t> pixels;
    void clear() { width = height = channels = 0; std::vector<uint8_t>().swap(pixels); }
};

// One model Run on one tile. `input` is [1,3,T,T] float32 NCHW RGB in [0,1];
// the engine must return exactly 3*outSide*outSide floats (outSide = T*scale - 2*offset).
class ITileEngine {
public:
    virtual ~ITileEngine() = default;
    virtual Status run(const float* input, int tile, int outSide, std::vector<float>& output) = 0;
};

// Mirrors SeamBlending.create_config (seam_blending.py:121-152).
struct TilePlan {
    int tile = 0;           // T, input tile side
    int inputOffset = 0;    // ceil(offset / scale)
    int inputBlend = 0;     // ceil(blend / scale)
    int stepIn = 0;         // T - (2*inputOffset + inputBlend)
    int stepOut = 0;        // stepIn * scale
    int tileOut = 0;        // T*scale - 2*offset (model output side)
    int hBlocks = 0, wBlocks = 0;
    int padLeft = 0, padRight = 0, padTop = 0, padBottom = 0;  // replicate padding
    int outWidth = 0, outHeight = 0;  // exact W*scale, H*scale
    int tiles() const { return hBlocks * wBlocks; }
};

std::expected<TilePlan, Status> makeTilePlan(int width, int height, const TileParams& p, int tile);

struct Progress {
    uint64_t donePixels = 0, totalPixels = 0;  // effective output pixels (all passes)
    int tilesDone = 0, tilesTotal = 0;
    int attempt = 1;                           // 2 after a restart with the fallback tile
    int pass = 0;                              // 0 = RGB, 1 = alpha
};

struct ProcessOptions {
    int tile = 256;
    bool allowOomRetry = true;   // Auto tile: one restart with fallbackTile on OOM or a lost device
    int fallbackTile = 160;
    // Runs once, right before that restart. Must release the sessions: after either error the
    // D3D device is unusable, and the same session fails with 887A0006 (P2 redo report §4.4).
    std::function<void()> beforeRetry;
    uint64_t maxOutputPixels = 75'000'000;  // plan §8.1 independent output guard
    const std::atomic<bool>* cancel = nullptr;  // checked before every tile
    std::function<void(const Progress&)> progress;  // after every tile
};

struct ProcessStats {
    int tileUsed = 0;
    int attempts = 0;
    int tilesRun = 0;            // Runs in the successful attempt (both passes)
    bool alphaPassed = false;    // a non-blank alpha went through the alpha model
    Status firstAttemptError;    // the error that caused a restart, if any
};

// Waifu2x.convert (waifu2x/utils.py:261-311) for one 8-bit image:
//  - RGB -> float /255; RGBA: alpha all 255 = blank alpha (treated as no alpha for RGB).
//  - non-blank alpha: AlphaBorderPadding(rgb, alpha, offset) before the RGB pass.
//  - RGB tiled render, clamp, quantize256.
//  - alpha: 1x kept as is; 2x/4x non-blank: alpha -> 3 channels -> tiled render with
//    alphaEngine (scale2x / scale4x) -> mean of channels; blank: nearest upscale.
// Output channels = input channels. Output size is exactly W*scale x H*scale.
// On any error only the Status is returned (no partial result); `stats` is filled either way.
std::expected<Image8, Status> upscaleImage(const ImageView& in, const TileParams& rgbModel, ITileEngine& rgbEngine,
                                           const TileParams* alphaModel, ITileEngine* alphaEngine,
                                           const ProcessOptions& opt, ProcessStats* stats = nullptr);

// AlphaBorderPadding.forward (nunif/utils/alpha.py:46-64) on planar float RGB (3*W*H) in place.
void alphaBorderPadding(float* rgb, const float* alpha, int width, int height, int iterations);

// quantize256: (x * 255.0).round_().clamp_(0, 255) with round-half-to-even.
uint8_t quantize256(float v);

// Overflow-checked multiply; false on overflow.
bool mulOk(uint64_t a, uint64_t b, uint64_t& r);

}  // namespace arx::core
