"""Illegal tile check (dev only): input_<T>.bin holds the top-left crop of one shared image, so
the interior of an output at tile T can be compared with the same pixels at the largest legal tile.
Legal tiles give the baseline for how much tile size alone changes the result.

  python -I p1redo_illegal.py <p1redo dir> <reference tile> <tile> [...]
"""
import json
import math
import sys
from pathlib import Path

import numpy as np

MARGIN = 32  # output pixels per scale dropped on the bottom/right edge of the crop


def load(root, tile, name):
    d = root / f"t{tile}"
    shape = json.loads((d / f"ref_{name}.shape.json").read_text())
    return np.fromfile(d / f"out_{name}_dml.bin", dtype=np.float32).reshape(shape)[0]


def main():
    root, ref_tile = Path(sys.argv[1]), int(sys.argv[2])
    for name, s in (("noise3", 1), ("noise2_scale2x", 2)):
        ref = load(root, ref_tile, name)
        for t in map(int, sys.argv[3:]):
            out = load(root, t, name)
            n = min(out.shape[1], ref.shape[1]) - MARGIN * s
            d = (out[:, :n, :n].astype(np.float64) - ref[:, :n, :n].astype(np.float64))
            mse = float((d ** 2).mean())
            q = lambda a: np.clip(np.round(a * 255.0), 0, 255).astype(np.int16)
            d8 = np.abs(q(out[:, :n, :n]) - q(ref[:, :n, :n]))
            legal = t >= 64 and t % 32 == 0
            print(f"{name:<16} tile {t:<4} legal={legal!s:<5} vs tile {ref_tile} on {n}x{n}: "
                  f"PSNR {10 * math.log10(1 / mse) if mse else math.inf:.2f} dB max8bit {int(d8.max())} "
                  f"px8bit!=0 {float((d8 > 0).mean() * 100):.2f}%")


if __name__ == "__main__":
    main()
