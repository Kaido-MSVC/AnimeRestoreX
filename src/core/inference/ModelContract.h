// Model contract from models/manifest.json: per-model scale / offset / blend size,
// I/O names, file hash, legal tile rule and the alpha helper model per scale.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Status.h"

namespace arx::core {

// Which tile sizes a model family accepts (manifest "tile_rule"). The ONNX graphs do not check it.
enum class TileRule {
    SwinUnet,    // "swin_unet": waifu2x/models/swin_unet.py, T > 16 && (T-16) % 48 == 0
    SwinUnetV3,  // "swin_unet_v3": T >= 64 && T % 16 == 0. What the v3 graph needs (T/2 is a
                 // multiple of window 8). The upstream validator is stricter (T % 32 == 0);
                 // tiled runs at 240 and 400 still matched PyTorch (docs/v2.0-P1收尾報告.md §2).
};

struct ModelInfo {
    std::string id;      // e.g. "swin_unet_v3/art/noise3"
    std::string file;    // relative path below the models root, '/' separated
    std::string sha256;  // lower-case hex of the (rewritten) ONNX file
    int scale = 0;       // 1, 2 or 4
    int noiseLevel = -1; // -1 = no denoise
    int offset = 0;      // output pixels removed per side by the model
    int blendSize = 0;   // seam blending width in output pixels
    TileRule tileRule = TileRule::SwinUnet;
};

// What the tiler needs from a model. ceil(offset/scale) is the input halo.
struct TileParams {
    int scale = 1;
    int offset = 0;
    int blendSize = 0;
    TileRule tileRule = TileRule::SwinUnet;
};

class ModelContract {
public:
    // Parses and validates the manifest (structure, value ranges, relative paths).
    static Status load(const std::filesystem::path& manifest, ModelContract& out);

    const ModelInfo* find(std::string_view id) const;
    const std::vector<ModelInfo>& models() const { return models_; }
    const std::string& inputName() const { return inputName_; }
    const std::string& outputName() const { return outputName_; }

    // Model used to upscale a non-blank alpha channel for this scale (upstream
    // Waifu2x.convert: scale2x for 2x, scale4x for 4x). Empty for 1x (alpha kept).
    std::string alphaModelFor(int scale) const;

    // Resolves models_root/file, refuses paths that leave the root, and checks the
    // SHA-256 against the manifest. A mismatch is a hard error (no fallback model).
    Status resolveVerified(const ModelInfo& m, const std::filesystem::path& modelsRoot,
                           std::filesystem::path& resolved) const;

    static bool isValidTile(TileRule rule, int tile) {
        return rule == TileRule::SwinUnetV3 ? tile >= 64 && tile % 16 == 0 : tile > 16 && (tile - 16) % 48 == 0;
    }
    static bool isValidTile(const ModelInfo& m, int tile) { return isValidTile(m.tileRule, tile); }

    static TileParams tileParams(const ModelInfo& m) { return {m.scale, m.offset, m.blendSize, m.tileRule}; }

private:
    std::vector<ModelInfo> models_;
    std::string inputName_ = "x", outputName_ = "y";
    std::string alpha2x_, alpha4x_;
};

}  // namespace arx::core
