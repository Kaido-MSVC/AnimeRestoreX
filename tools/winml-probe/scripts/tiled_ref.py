"""PyTorch reference for a whole image through nunif's own tiler (torch CPU, FP32, batch 1).

  (venv-torch) python -I tiled_ref.py <nunif root> <pth dir> <in.png> <out dir> <tile> <model> [...]

Writes <out dir>/<model>_t<tile>.png and appends timing to <out dir>/tiled_ref.jsonl.
The tile validator is removed after loading: find_valid_tile_size() would otherwise turn 400 into
384 and 240 into 224 without a word, and the reference would no longer use the requested tile.
"""
import json
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    root, pth_dir, src, out = map(Path, sys.argv[1:5])
    tile = int(sys.argv[5])
    sys.path.insert(0, str(root))
    import torch
    from nunif.models import load_model
    from nunif.models import model as nunif_model
    from nunif.utils.render import tiled_render
    import waifu2x.models  # noqa: F401  (registers the classes)

    out.mkdir(parents=True, exist_ok=True)
    rgb = np.array(Image.open(src).convert("RGB"), dtype=np.float32) / 255.0
    x = torch.from_numpy(rgb).permute(2, 0, 1)
    for name in sys.argv[6:]:
        model, _ = load_model(str(pth_dir / f"{name}.pth"), device_ids=[-1], weights_only=True)
        model = model.to_inference_model().eval() if hasattr(model, "to_inference_model") else model.eval()
        nunif_model._tile_size_validators.pop(model.name, None)
        used = model.find_valid_tile_size(tile)
        if used != tile:
            raise SystemExit(f"tile {tile} became {used}")
        t = time.perf_counter()
        with torch.inference_mode():
            y = tiled_render(x, model, tile_size=tile, batch_size=1, enable_amp=False)
        sec = time.perf_counter() - t
        a = (y * 255.0).round_().clamp_(0, 255).to(torch.uint8).permute(1, 2, 0).numpy()
        p = out / f"{name}_t{tile}.png"
        Image.fromarray(a, "RGB").save(p)
        with open(out / "tiled_ref.jsonl", "a", encoding="utf-8") as f:
            f.write(json.dumps(dict(model=name, tile=tile, input=src.name, shape=list(a.shape), seconds=sec,
                                    torch=torch.__version__)) + "\n")
        print(f"{name} tile {tile} {a.shape} {sec:.1f}s", flush=True)


if __name__ == "__main__":
    main()
