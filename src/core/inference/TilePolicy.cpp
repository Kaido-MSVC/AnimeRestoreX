#include "TilePolicy.h"

namespace arx::core {

uint64_t sessionVramMiB(int scale, int tile) {
    const bool x4 = scale == 4;
    switch (tile) {
    case 160: return x4 ? 376 : 424;
    case 256: return x4 ? 772 : 1146;
    case 352: return x4 ? 1484 : 2218;
    case 448: return x4 ? 2909 : 3025;
    default: return 0;
    }
}

TileDecision chooseTile(int requestedTile, int scale, bool cpu, uint64_t availableMiB) {
    TileDecision d;
    if (cpu) {
        d.tile = requestedTile > 0 ? requestedTile : kAutoTile;
        d.allowOomRetry = requestedTile == 0;
        d.alphaTogether = true;  // host RAM, no GPU budget
        d.reason = requestedTile > 0 ? TileReason::Manual : TileReason::Cpu;
        return d;
    }
    if (requestedTile > 0) {
        d.tile = requestedTile;
        d.allowOomRetry = false;
        d.reason = TileReason::Manual;
    } else {
        d.tile = kAutoTile;
        d.reason = TileReason::Auto;
        if (availableMiB > 0 && availableMiB < sessionVramMiB(scale, kAutoTile) + kVramMarginMiB) {
            d.tile = kFallbackTile;
            d.reason = TileReason::AutoVram;
        }
        d.allowOomRetry = d.tile != kFallbackTile;
    }
    const uint64_t one = sessionVramMiB(scale, d.tile);
    d.alphaTogether = availableMiB > 0 && one > 0 && availableMiB >= 2 * one + kVramMarginMiB;
    return d;
}

}  // namespace arx::core
