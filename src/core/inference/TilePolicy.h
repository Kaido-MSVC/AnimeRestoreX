// Tile size and session scheduling for a job (decisions of P1/P2, see docs/v2.0-P2報告.md §9):
//  - Auto = 256; 160 only when the GPU memory budget is too small for 256, or once per image
//    after an explicit out-of-memory error (TileProcessor restarts that image from scratch).
//  - Manual 160 / 256 / 400: used as is, never retried with another size.
//  - 640 and above are not offered (6 GB GTX 1060: device hung at 4x).
//  - RGB + alpha sessions stay open together only if both fit the budget; otherwise the
//    alpha session is created after the RGB session was released.
#pragma once

#include <cstdint>

namespace arx::core {

inline constexpr int kAutoTile = 256;
inline constexpr int kFallbackTile = 160;
inline constexpr uint64_t kVramMarginMiB = 256;

// Tile sizes a job may request: 0 (Auto), 160, 256, 400. Everything else is refused before
// the engine is touched, even if the model's alignment rule would accept it (e.g. 64, 640).
inline constexpr bool isOfferedTile(int tile) { return tile == 0 || tile == 160 || tile == 256 || tile == 400; }

// Measured peak VRAM of one DML session (GTX 1060 6GB, P1 §12.7 and P2 §9), in MiB.
// 1x and 2x models need the same; 0 = not measured.
uint64_t sessionVramMiB(int scale, int tile);

enum class TileReason {
    Manual,      // chosen in the settings
    Auto,        // Auto, default size
    AutoVram,    // Auto, smaller size because the GPU memory budget is short
    Cpu,         // CPU device: no GPU memory budget applies
};

struct TileDecision {
    int tile = kAutoTile;
    bool allowOomRetry = true;   // Auto at 256 on the GPU: one restart at 160 after OOM
    int fallbackTile = kFallbackTile;
    bool alphaTogether = true;   // RGB and alpha sessions may be open at the same time
    TileReason reason = TileReason::Auto;
};

// requestedTile: 0 = Auto, else 160/256/400. availableMiB: 0 = unknown (then 256 is kept
// and the alpha session is scheduled after the RGB session, the safe order).
TileDecision chooseTile(int requestedTile, int scale, bool cpu, uint64_t availableMiB);

}  // namespace arx::core
