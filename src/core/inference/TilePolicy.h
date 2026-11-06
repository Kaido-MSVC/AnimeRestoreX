// Tile size and session scheduling for a job (docs/v2.0-P2報告.md §9, docs/v2.0-P2重做報告.md):
//  - Auto = 256; 160 when the GPU memory budget is too small for 256, or after an explicit
//    out-of-memory or device-lost error: the sessions are rebuilt, TileProcessor restarts that
//    image from scratch, and the rest of the batch stays on 160. One recovery per batch.
//  - Manual 160 / 256 / 352 / 448: used as is, never retried with another size. These four are
//    legal for both model families.
//  - 640 and above are not offered (6 GB GTX 1060: device hung at 4x).
//  - RGB + alpha sessions stay open together only if both fit the budget; otherwise the
//    alpha session is created after the RGB session was released.
#pragma once

#include <cstdint>

namespace arx::core {

inline constexpr int kAutoTile = 256;
inline constexpr int kFallbackTile = 160;
inline constexpr uint64_t kVramMarginMiB = 256;

// Tile sizes a job may request: 0 (Auto), 160, 256, 352, 448. Everything else is refused before
// the engine is touched, even if the model's tile rule would accept it (e.g. 64, 400, 640).
inline constexpr bool isOfferedTile(int tile) {
    return tile == 0 || tile == 160 || tile == 256 || tile == 352 || tile == 448;
}

// Measured peak VRAM of one DML session (GTX 1060 6GB, ORT 1.24.4, docs/v2.0-P1重做報告.md §6),
// in MiB. 1x and 2x are the v3 models (the higher of the two); 0 = not measured.
uint64_t sessionVramMiB(int scale, int tile);

enum class TileReason {
    Manual,      // chosen in the settings
    Auto,        // Auto, default size
    AutoVram,    // Auto, smaller size because the GPU memory budget is short
    AutoRecovered,  // Auto, smaller size kept after this batch recovered from a GPU failure
    Cpu,        // CPU device: no GPU memory budget applies
};

struct TileDecision {
    int tile = kAutoTile;
    bool allowOomRetry = true;   // Auto at 256: one restart at 160 after OOM or a lost device
    int fallbackTile = kFallbackTile;
    bool alphaTogether = true;   // RGB and alpha sessions may be open at the same time
    TileReason reason = TileReason::Auto;
};

// requestedTile: 0 = Auto, else 160/256/352/448. availableMiB: 0 = unknown (then 256 is kept
// and the alpha session is scheduled after the RGB session, the safe order).
TileDecision chooseTile(int requestedTile, int scale, bool cpu, uint64_t availableMiB);

}  // namespace arx::core
