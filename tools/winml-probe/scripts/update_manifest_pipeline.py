"""Records the two-step pipeline in models/manifest.json (dev only; run with the venv of
Historical: wrote the pre-P2 manifest shape ("v3_pending"); P2 merged that block into "models".
requirements.txt). Replaces "rewrite_tool" by "pipeline", adds pad_sha256 and the Reshape counts to
every model, and adds the v3 models as "v3_pending" (ModelContract reads "models" only, so the
app keeps using the 20250502 models until P2 moves them over).

  python -I update_manifest_pipeline.py <manifest.json> <processed 20250502 dir> <v3 exports dir> <work dir> <zip sha256>

Every hash comes from really running the scripts here, nothing is copied from older reports.
Stops without writing if a 20250502 model is changed by the second step.
"""
import json
import platform
import subprocess
import sys
from pathlib import Path

import numpy as np
import onnx

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from rewrite_pad import sha256  # noqa: E402

PAD = "tools/winml-probe/scripts/rewrite_pad.py"
FIX = "tools/winml-probe/scripts/fix_reshape_allowzero.py"
V3 = ["noise0", "noise1", "noise2", "noise3", "noise0_scale2x", "noise1_scale2x", "noise2_scale2x",
      "noise3_scale2x", "scale2x"]


def step(script, src, dst):
    rep = dst.with_suffix(".json")
    dst.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, "-I", str(HERE / script), str(src), str(dst), "--report", str(rep)],
                   check=True, stdout=subprocess.DEVNULL)
    return json.loads(rep.read_text(encoding="utf-8"))


def counts(r):
    return dict(reshape_allowzero_cleared=r["cleared"], reshape_allowzero_dynamic_kept=r["dynamic_kept"])


def main():
    manifest, old_dir, v3_dir, work, zip_sha = sys.argv[1:6]
    manifest, old_dir, v3_dir, work = map(Path, (manifest, old_dir, v3_dir, work))
    m = json.loads(manifest.read_text(encoding="utf-8"))

    for e in m["models"]:
        r = step("fix_reshape_allowzero.py", old_dir / e["file"], work / "old" / e["file"])
        if r["dst_sha256"] != e["sha256"] or r["cleared"]:
            raise SystemExit(f"{e['id']}: second step changed the model")
        e["pad_sha256"] = e["sha256"]
        e.update(counts(r))
        # keep the final hash fields next to each other
        for k in ("sha256", "size", "rewrite", "pad_nodes_untouched",
                  "reshape_allowzero_cleared", "reshape_allowzero_dynamic_kept"):
            e[k] = e.pop(k)

    models = []
    for n in V3:
        s = 2 if "scale2x" in n else 1
        pad = step("rewrite_pad.py", v3_dir / f"{n}.onnx", work / "v3-pad" / f"{n}.onnx")
        fix = step("fix_reshape_allowzero.py", work / "v3-pad" / f"{n}.onnx", work / "v3" / f"{n}.onnx")
        models.append({
            "id": f"swin_unet_v3/art/{n}",
            "file": f"swin_unet_v3/art/{n}.onnx",
            "scale": s,
            "noise_level": int(n[5]) if n.startswith("noise") else -1,
            "offset": 8 * s,
            "blend_size": 4 * s,
            "source_sha256": pad["src_sha256"],
            "source_size": (v3_dir / f"{n}.onnx").stat().st_size,
            "pad_sha256": pad["dst_sha256"],
            "sha256": fix["dst_sha256"],
            "size": fix["dst_size"],
            "rewrite": [{k: p[k] for k in ("name", "kind", "action", "pads", "slice") if k in p}
                        for p in pad["pads"] if p["action"].startswith("Slice")],
            "pad_nodes_untouched": sum(1 for p in pad["pads"] if not p["action"].startswith("Slice")),
            **counts(fix),
        })

    pipeline = {
        "python": platform.python_version(),
        "onnx": onnx.__version__,
        "numpy": np.__version__,
        "steps": [
            {"name": "rewrite_pad", "script": PAD, "script_sha256": sha256(HERE / "rewrite_pad.py"),
             "reason": "DirectML EP returns zeros for Pad with negative pads (still true on ORT 1.24.4 / DirectML "
                       "1.15.4); negative static Pad -> equivalent Slice, weights unchanged"},
            {"name": "fix_reshape_allowzero", "script": FIX, "script_sha256": sha256(HERE / "fix_reshape_allowzero.py"),
             "reason": "DirectML EP does not resolve -1 in Reshape when allowzero=1, Run fails on dynamo graphs; "
                       "allowzero cleared where the static shape has -1 and no 0 (same operation). "
                       "No change on the 20250502 graphs (no allowzero at all)"},
        ],
    }
    out = {}
    for k, v in m.items():  # same position in the file as the old block
        out["pipeline" if k == "rewrite_tool" else k] = pipeline if k == "rewrite_tool" else v
    out["v3_pending"] = {
        "status": "PENDING - P2 moves these into \"models\"; fetched by tools/fetch-models.ps1, not read by the app yet",
        "source": {
            "project": "nagadomi/nunif",
            "commit": "0d3d92c",
            "exported_with": "torch 2.14.1+cpu, onnx 1.23.2, onnxscript 0.7.2",
            "url": "https://github.com/Kaido-MSVC/AnimeRestoreX/releases/download/models-v3-nunif-0d3d92c/"
                   "waifu2x-swin_unet_v3-art-onnx-nunif-0d3d92c.zip",
            "zip_name": "waifu2x-swin_unet_v3-art-onnx-nunif-0d3d92c.zip",
            "zip_sha256": zip_sha,
            "path_in_zip": "swin_unet_v3/art/",
            "license": "MIT",
        },
        "tile_rule": "T >= 64 and T % 32 == 0",
        "alpha": {"scale2x_alpha_model": "swin_unet_v3/art/scale2x"},
        "models": models,
    }
    manifest.write_text(json.dumps(out, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print("updated", manifest, "with", len(models), "pending models")


if __name__ == "__main__":
    main()
