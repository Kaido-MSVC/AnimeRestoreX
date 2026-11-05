"""Builds the first draft of models/manifest.json from the rewrite_pad.py per-model reports.
Historical: the pipeline fields now come from update_manifest_pipeline.py.

  python -I make_manifest.py <rewritten dir> <out manifest.json>
"""
import json
import sys
from pathlib import Path

MODELS = ["noise0", "noise1", "noise2", "noise3",
          "noise0_scale2x", "noise1_scale2x", "noise2_scale2x", "noise3_scale2x", "scale2x",
          "noise0_scale4x", "noise1_scale4x", "noise2_scale4x", "noise3_scale4x", "scale4x"]


def scale_of(n):
    return 4 if "scale4x" in n else 2 if "scale2x" in n else 1


def noise_of(n):
    return int(n[5]) if n.startswith("noise") else -1


def main():
    rw, out = Path(sys.argv[1]), Path(sys.argv[2])
    reports = {n: json.loads((rw / f"{n}.json").read_text(encoding="utf-8")) for n in MODELS}
    r0 = reports[MODELS[0]]
    models = []
    for n in MODELS:
        r = reports[n]
        s = scale_of(n)
        models.append({
            "id": f"swin_unet/art/{n}",
            "file": f"swin_unet/art/{n}.onnx",
            "scale": s,
            "noise_level": noise_of(n),
            "offset": 8 * s,
            "blend_size": 4 * s,
            "source_sha256": r["src_sha256"],
            "sha256": r["dst_sha256"],
            "size": r["dst_size"],
            "rewrite": [{k: p[k] for k in ("name", "kind", "action") if k in p} |
                        ({"pads": p["pads"]} if "pads" in p else {}) |
                        ({"slice": p["slice"]} if "slice" in p else {})
                        for p in r["pads"] if p["action"].startswith("Slice")],
            "pad_nodes_untouched": sum(1 for p in r["pads"] if not p["action"].startswith("Slice")),
        })
    manifest = {
        "schema": "animerestorex-models/0-draft",
        "status": "DRAFT - not for release: model licence unresolved (docs/v2.0-P0-P1報告.md §5)",
        "source": {
            "project": "nagadomi/nunif",
            "url": "https://github.com/nagadomi/nunif/releases/download/0.0.0/waifu2x_onnx_models_20250502.zip",
            "zip_sha256": "4d531e299884f8895df82947939f85c5ff6fca15236b9b72f1aa8ea476132e08",
            "path_in_zip": "onnx_models/swin_unet/art/",
            "exported_with_commit": "31c0137dc949807e382d918dc325c65fd6703092",
            "license": "UNRESOLVED",
        },
        "rewrite_tool": {
            "script": "tools/winml-probe/scripts/rewrite_pad.py",
            "script_sha256": r0["tool_sha256"],
            "python": r0["python"],
            "onnx": r0["onnx"],
            "numpy": r0["numpy"],
            "reason": "DirectML EP (Windows ML 2.5.96 / ORT 1.28.3 / DirectML 1.15.4) returns zeros for Pad "
                      "with negative pads; negative static Pad -> equivalent Slice, weights unchanged",
        },
        "io_contract": {
            "input": {"name": "x", "dtype": "float32", "layout": "NCHW", "shape": [1, 3, "T", "T"],
                      "color": "RGB", "range": [0.0, 1.0]},
            "output": {"name": "y", "dtype": "float32", "layout": "NCHW",
                       "shape": [1, 3, "T*scale-2*offset", "T*scale-2*offset"], "range": [0.0, 1.0],
                       "clamped_in_model": True},
            "tile_rule": "T > 16 and (T - 16) % 48 == 0 (64, 112, 160, 208, 256, 304, 352, 400, ..., 640)",
            "input_offset": "ceil(offset / scale) = 8 for every model",
            "input_blend": "ceil(blend_size / scale) = 4 for every model",
            "step_in": "T - 20",
            "input_padding": "replicate",
            "opset": 17,
            "ir_version": 8,
            "batch": 1,
        },
        "alpha": {
            "scale1x": "not used (bypass model is itself a negative Pad); 1x alpha is kept as is / cropped in C++",
            "scale2x_alpha_model": "swin_unet/art/scale2x",
            "scale4x_alpha_model": "swin_unet/art/scale4x",
        },
        "models": models,
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("wrote", out, len(models), "models")


if __name__ == "__main__":
    main()
