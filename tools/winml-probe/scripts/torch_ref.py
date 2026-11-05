"""PyTorch reference for one tile: runs nunif .pth weights on input_<T>.bin (torch CPU, FP32)
and writes ref_<model>.bin / ref_<model>.shape.json for reference.py compare.

  (venv-torch) python -I torch_ref.py <nunif root> <pth dir> <work dir> <tile> <model> [...]

The tile is fed straight to forward(): the nunif tile validator is not applied, so illegal
tiles (e.g. 400 on v3) show what PyTorch itself does. A failure there is printed and recorded
in torch_ref.json, the remaining models still run.
"""
import json
import sys
import time
from pathlib import Path

import numpy as np


def main():
    root, pth_dir, work = map(Path, sys.argv[1:4])
    tile = int(sys.argv[4])
    sys.path.insert(0, str(root))
    import torch
    from nunif.models import load_model
    import waifu2x.models  # noqa: F401  (registers the swin_unet classes)

    x = torch.from_numpy(np.fromfile(work / f"input_{tile}.bin", dtype=np.float32).reshape(1, 3, tile, tile))
    log = work / "torch_ref.json"
    rows = json.loads(log.read_text()) if log.exists() else []
    for name in sys.argv[5:]:
        model, _ = load_model(str(pth_dir / f"{name}.pth"), device_ids=[-1], weights_only=True)
        model = model.to_inference_model().eval() if hasattr(model, "to_inference_model") else model.eval()
        t = time.perf_counter()
        try:
            with torch.inference_mode():
                y = model(x).numpy()
        except Exception as e:  # noqa: BLE001
            msg = str(e).splitlines()[0][:200] if str(e) else type(e).__name__
            rows.append(dict(model=name, tile=tile, error=f"{type(e).__name__}: {msg}"))
            print(f"{name:<16} tile {tile} torch ERROR {type(e).__name__}: {msg}")
            continue
        y.astype(np.float32).tofile(work / f"ref_{name}.bin")
        (work / f"ref_{name}.shape.json").write_text(json.dumps(list(y.shape)))
        rows.append(dict(model=name, tile=tile, cls=type(model).__name__, shape=list(y.shape),
                         torch=torch.__version__, seconds=time.perf_counter() - t))
        print(f"{name:<16} tile {tile} {type(model).__name__} out {list(y.shape)} "
              f"min {y.min():.4f} max {y.max():.4f} {rows[-1]['seconds']:.1f}s", flush=True)
    log.write_text(json.dumps(rows, indent=1))


if __name__ == "__main__":
    main()
