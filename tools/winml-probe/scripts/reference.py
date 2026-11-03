"""Reference side of the Windows ML probe (development only, not shipped).

  python -I reference.py make-input <work dir>
      Builds a deterministic 240x240 anime-like test image (flat fills, dark
      anti-aliased lines, gradient, small text-like strokes, JPEG q=70 noise),
      pads it by 8 px with replicate padding (the upstream nunif default for
      swin_unet: input_offset = ceil(offset/scale) = 8 for 1x/2x/4x) and writes
      input_256.bin (float32 NCHW [1,3,256,256], RGB, 0..1) + input_core.png.

  python -I reference.py reference <work dir> <models dir> <model> [...]
      Runs each model with the Python onnxruntime CPU EP on input_256.bin and
      writes ref_<model>.bin.

  python -I reference.py compare <work dir> <model> [...]
      Compares out_<model>_<ep>.bin written by winml_probe.exe with
      ref_<model>.bin: max abs error, PSNR (float, peak 1.0) and max 8-bit
      difference after round(x*255).

  python -I reference.py placement <profile.json> [...]
      Counts profiled kernel events per execution provider.
"""
import json
import math
import sys
from pathlib import Path

import numpy as np

CORE = 240
HALO = 8
TILE = CORE + 2 * HALO  # 256, valid swin_unet tile: (256-16) % 48 == 0


def make_input(work: Path):
    from PIL import Image, ImageDraw
    import io

    rng = np.random.default_rng(20261103)
    im = Image.new("RGB", (CORE, CORE), (250, 236, 220))
    d = ImageDraw.Draw(im)
    # gradient sky
    for y in range(80):
        c = int(120 + y * 1.5)
        d.line([(0, y), (CORE, y)], fill=(c, c + 20 if c < 235 else 255, 255))
    # flat filled shapes with dark outlines
    d.ellipse([30, 60, 150, 180], fill=(255, 214, 196), outline=(40, 30, 30), width=3)
    d.polygon([(140, 200), (230, 120), (235, 235)], fill=(90, 120, 200), outline=(20, 20, 40))
    d.rectangle([160, 30, 220, 90], fill=(230, 80, 90), outline=(30, 10, 10), width=2)
    # thin lines (hair-like strokes)
    for i in range(12):
        x0 = 40 + i * 8
        d.line([(x0, 60), (x0 + 30, 20 + i * 3)], fill=(50, 30, 20), width=1)
    # text-like glyph strokes
    for i in range(6):
        x = 20 + i * 14
        d.rectangle([x, 200, x + 8, 202], fill=(10, 10, 10))
        d.line([(x + 4, 196), (x + 4, 216)], fill=(10, 10, 10), width=1)
    arr = np.asarray(im).astype(np.float32)
    arr += rng.normal(0, 2.0, arr.shape).astype(np.float32)  # light sensor-like noise
    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))
    buf = io.BytesIO()
    im.save(buf, format="JPEG", quality=70)  # JPEG artifacts for the denoise models
    im = Image.open(io.BytesIO(buf.getvalue())).convert("RGB")
    im.save(work / "input_core.png")

    x = np.asarray(im).astype(np.float32).transpose(2, 0, 1) / 255.0  # CHW RGB 0..1
    x = np.pad(x, ((0, 0), (HALO, HALO), (HALO, HALO)), mode="edge")  # replicate
    x = np.ascontiguousarray(x[None], dtype=np.float32)
    assert x.shape == (1, 3, TILE, TILE)
    x.tofile(work / "input_256.bin")
    print("wrote", work / "input_256.bin", x.shape, "sha256", sha256(work / "input_256.bin"))


def sha256(p: Path):
    import hashlib
    return hashlib.sha256(p.read_bytes()).hexdigest()


def load_input(work: Path):
    return np.fromfile(work / "input_256.bin", dtype=np.float32).reshape(1, 3, TILE, TILE)


def reference(work: Path, models: Path, names):
    import onnxruntime as ort
    import time
    x = load_input(work)
    for name in names:
        so = ort.SessionOptions()
        sess = ort.InferenceSession(str(models / f"{name}.onnx"), so, providers=["CPUExecutionProvider"])
        t = time.perf_counter()
        y = sess.run(["y"], {"x": x})[0]
        dt = time.perf_counter() - t
        y.astype(np.float32).tofile(work / f"ref_{name}.bin")
        (work / f"ref_{name}.shape.json").write_text(json.dumps(list(y.shape)))
        print(f"{name}: ort {ort.__version__} CPU out {list(y.shape)} "
              f"min {y.min():.4f} max {y.max():.4f} run {dt:.2f}s")


def compare(work: Path, names):
    rows = []
    for name in names:
        shape = json.loads((work / f"ref_{name}.shape.json").read_text())
        ref = np.fromfile(work / f"ref_{name}.bin", dtype=np.float32).reshape(shape)
        for ep in ("dml", "cpu"):
            p = work / f"out_{name}_{ep}.bin"
            if not p.exists():
                print(f"{name} {ep}: 未執行 (no output file)")
                continue
            out = np.fromfile(p, dtype=np.float32)
            if out.size != ref.size:
                print(f"{name} {ep}: size mismatch {out.size} vs {ref.size}")
                continue
            out = out.reshape(shape)
            finite = bool(np.isfinite(out).all())
            diff = np.abs(out.astype(np.float64) - ref.astype(np.float64))
            mse = float((diff ** 2).mean())
            psnr = math.inf if mse == 0 else 10 * math.log10(1.0 / mse)
            q = lambda a: np.clip(np.round(a * 255.0), 0, 255).astype(np.int16)
            d8 = np.abs(q(out) - q(ref))
            row = dict(model=name, ep=ep, finite=finite, max_abs=float(diff.max()),
                       mean_abs=float(diff.mean()), psnr_db=psnr, max_diff_8bit=int(d8.max()),
                       pct_8bit_diff=float((d8 > 0).mean() * 100))
            rows.append(row)
            print(f"{name:<16} {ep:<3} finite={finite} max_abs={row['max_abs']:.3e} "
                  f"mean_abs={row['mean_abs']:.3e} PSNR={psnr:.2f} dB "
                  f"max8bit={row['max_diff_8bit']} px8bit!=0={row['pct_8bit_diff']:.3f}%")
    (work / "compare.json").write_text(json.dumps(rows, indent=1))


def placement(files):
    for f in files:
        events = json.loads(Path(f).read_text(encoding="utf-8"))
        counts = {}
        cpu_ops = {}
        for e in events:
            if e.get("cat") != "Node":
                continue
            args = e.get("args", {})
            prov = args.get("provider")
            if prov is None or not e.get("name", "").endswith("_kernel_time"):
                continue
            counts[prov] = counts.get(prov, 0) + 1
            if prov == "CPUExecutionProvider":
                op = args.get("op_name", "?")
                cpu_ops[op] = cpu_ops.get(op, 0) + 1
        print(Path(f).name, "kernel events per provider:", counts, "CPU ops:", cpu_ops)


def main():
    cmd = sys.argv[1]
    if cmd == "make-input":
        make_input(Path(sys.argv[2]))
    elif cmd == "reference":
        reference(Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4:])
    elif cmd == "compare":
        compare(Path(sys.argv[2]), sys.argv[3:])
    elif cmd == "placement":
        placement(sys.argv[2:])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
