"""EXPERIMENT (P1 redo, dev only; not part of the decided rewrite pipeline):
clear allowzero on Reshape nodes whose static shape contains -1 and no 0.

  python -I fix_reshape_allowzero.py <in.onnx> <out.onnx> [--report report.json]

Why: dynamo exports Reshape with allowzero=1. OperatorHelper.cpp ReshapeHelper::GetOutputShapes
in the DirectML EP (seen in ORT 1.24.4 source; same failure measured on 1.28.3) copies the shape
as is when allowzero is set and never resolves -1, so Run fails on such a node.
allowzero only changes the meaning of 0 in the shape, so with a static shape that has no 0
allowzero=0 is the same operation. Static shapes with a 0 are left alone; dynamic shapes
cannot be proven the same way and are left alone and counted.
STOP = exit 2: a static shape has both 0 and -1 (invalid with allowzero=1). Nothing is written.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
import onnx

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rewrite_pad import Graph, sha256  # noqa: E402


def fix(model):
    G = Graph(model)
    c = dict(reshape=0, allowzero=0, cleared=0, static_kept=0, dynamic_kept=0)
    for n in G.g.node:
        if n.op_type != "Reshape":
            continue
        c["reshape"] += 1
        az = next((a for a in n.attribute if a.name == "allowzero" and a.i), None)
        if az is None:
            continue
        c["allowzero"] += 1
        if G.ancestors(n.input[1])[1]:
            c["dynamic_kept"] += 1
            continue
        shape = G.eval_static(n.input[1]).ravel()
        if (shape == 0).any() and (shape == -1).any():
            raise SystemExit(f"STOP: {n.name}: shape {shape.tolist()} has both 0 and -1")
        if (shape == -1).any() and not (shape == 0).any():
            az.i = 0
            c["cleared"] += 1
        else:
            c["static_kept"] += 1
    return c


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src", type=Path)
    ap.add_argument("dst", type=Path)
    ap.add_argument("--report", type=Path)
    a = ap.parse_args()
    model = onnx.load(str(a.src))
    try:
        c = fix(model)
    except SystemExit as e:
        print(e, file=sys.stderr)
        sys.exit(2)
    onnx.checker.check_model(model, full_check=True)
    a.dst.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, str(a.dst))
    info = dict(src=a.src.name, src_sha256=sha256(a.src), dst=a.dst.name, dst_sha256=sha256(a.dst),
                dst_size=a.dst.stat().st_size, tool="fix_reshape_allowzero.py",
                tool_sha256=sha256(Path(__file__)), numpy=np.__version__, onnx=onnx.__version__, **c)
    print(f"{a.src.name}: {c} sha256 {info['dst_sha256']}")
    if a.report:
        a.report.write_text(json.dumps(info, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
