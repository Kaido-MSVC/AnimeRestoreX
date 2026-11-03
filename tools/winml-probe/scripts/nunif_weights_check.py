"""Checks that the nunif PyTorch weights (.pth) are the same network as our ONNX models:
runs one 256 tile through the .pth model (torch CPU, FP32) and compares with the ONNX
CPU reference ref_<model>.bin written by verify_rewrite.py cpu-bitwise.

  (venv-torch) python check.py <nunif root> <pth dir> <work dir with input_256.bin, ref_*.bin> <model> [...]
Run with the nunif repository root on sys.path (it is inserted below).
"""
import json
import math
import sys
from pathlib import Path

import numpy as np


def main():
    root, pth_dir, work = map(Path, sys.argv[1:4])
    sys.path.insert(0, str(root))
    import torch
    from nunif.models import load_model
    import waifu2x.models  # noqa: F401  (registers the swin_unet classes)

    x = torch.from_numpy(np.fromfile(work / "input_256.bin", dtype=np.float32).reshape(1, 3, 256, 256))
    for name in sys.argv[4:]:
        model, _ = load_model(str(pth_dir / f"{name}.pth"), device_ids=[-1], weights_only=True)
        model = model.to_inference_model().eval() if hasattr(model, "to_inference_model") else model.eval()
        with torch.inference_mode():
            y = model(x).numpy()
        shape = json.loads((work / f"ref_{name}.shape.json").read_text())
        ref = np.fromfile(work / f"ref_{name}.bin", dtype=np.float32).reshape(shape)
        d = np.abs(y.astype(np.float64) - ref)
        mse = float((d ** 2).mean())
        q = lambda a: np.clip(np.round(a * 255.0), 0, 255).astype(np.int16)
        print(f"{name:<16} torch {torch.__version__} out {list(y.shape)} vs ONNX CPU: max_abs {d.max():.3e} "
              f"PSNR {10 * math.log10(1 / mse) if mse else math.inf:.2f} dB max8bit {int(np.abs(q(y) - q(ref)).max())}")


if __name__ == "__main__":
    main()
