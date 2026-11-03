// Model contract from models/manifest.json: per-model scale / offset / blend size,
// I/O names, file hash, legal tile rule and the alpha helper model per scale.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Status.h"

namespace arx::core {

struct ModelInfo {
    std::string id;      // e.g. "swin_unet/art/noise3_scale4x"
    std::string file;    // relative path below the models root, '/' separated
    std::string sha256;  // lower-case hex of the (rewritten) ONNX file
    int scale = 0;       // 1, 2 or 4
    int noiseLevel = -1; // -1 = no denoise
    int offset = 0;      // output pixels removed per side by the model
    int blendSize = 0;   // seam blending width in output pixels
};

// What the tiler needs from a model. ceil(offset/scale) is the input halo.
struct TileParams {
    int scale = 1;
    int offset = 0;
    int blendSize = 0;
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

    // swin_unet tile rule (waifu2x/models/swin_unet.py tile_size_validator):
    // T > 16 && (T-16) % 12 == 0 && (T-16) % 16 == 0, i.e. (T-16) % 48 == 0.
    static bool isValidTile(int tile) { return tile > 16 && (tile - 16) % 12 == 0 && (tile - 16) % 16 == 0; }

    static TileParams tileParams(const ModelInfo& m) { return {m.scale, m.offset, m.blendSize}; }

private:
    std::vector<ModelInfo> models_;
    std::string inputName_ = "x", outputName_ = "y";
    std::string alpha2x_, alpha4x_;
};

}  // namespace arx::core
