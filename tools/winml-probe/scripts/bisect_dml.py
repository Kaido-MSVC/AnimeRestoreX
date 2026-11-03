"""Find the first node whose float output differs between DML (winml_probe.exe)
and the Python onnxruntime CPU EP, by bisecting over the topological node order.

  python -I bisect_dml.py <probe.exe> <model.onnx> <input_256.bin> <scratch dir>

For a candidate node k, a sub-model is built that keeps only the ancestors of
node k's first float output and exposes it as graph output "y" (through an
Identity), so winml_probe.exe can run it unchanged.
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import onnx
from onnx import helper, shape_inference

TILE = 256


def build_sub(model, inferred_types, k, path):
    g = model.graph
    node = g.node[k]
    tname = next(o for o in node.output if inferred_types.get(o) == onnx.TensorProto.FLOAT)
    producers = {o: i for i, n in enumerate(g.node) for o in n.output}
    keep, stack = set(), [k]
    while stack:
        i = stack.pop()
        if i in keep:
            continue
        keep.add(i)
        for inp in g.node[i].input:
            if inp in producers:
                stack.append(producers[inp])
    nodes = [g.node[i] for i in sorted(keep)]
    used = {i for n in nodes for i in n.input}
    inits = [t for t in g.initializer if t.name in used]
    nodes.append(helper.make_node("Identity", [tname], ["y"], name="probe_out"))
    y = helper.make_tensor_value_info("y", onnx.TensorProto.FLOAT, None)
    graph = helper.make_graph(nodes, "sub", list(g.input), [y], inits)
    m = helper.make_model(graph, opset_imports=model.opset_import, ir_version=model.ir_version)
    onnx.save(m, str(path))
    return node, tname


def mismatch(probe, sub, inp, work, x):
    import onnxruntime as ort
    ref = ort.InferenceSession(str(sub), providers=["CPUExecutionProvider"]).run(["y"], {"x": x})[0]
    out = work / "bisect_out.bin"
    if out.exists():
        out.unlink()
    subprocess.run([str(probe), "--model", str(sub), "--input", str(inp), "--ep", "dml",
                    "--scale", "1", "--offset", "0", "--tile", str(TILE), "--warm", "0",
                    "--output", str(out)], capture_output=True, text=True)
    d = np.fromfile(out, dtype=np.float32)
    if d.size != ref.size:
        return True, f"size {d.size} vs {ref.size}", ref.shape
    err = float(np.abs(d - ref.ravel()).max())
    scale = float(np.abs(ref).max()) + 1e-6
    return err / scale > 1e-2, f"max_abs {err:.3e} rel {err / scale:.3e}", ref.shape


def main():
    probe, model_path, inp, work = map(Path, sys.argv[1:5])
    work.mkdir(parents=True, exist_ok=True)
    model = onnx.load(str(model_path))
    inferred = shape_inference.infer_shapes(model)
    types = {v.name: v.type.tensor_type.elem_type for v in inferred.graph.value_info}
    types.update({o.name: o.type.tensor_type.elem_type for o in inferred.graph.output})
    cands = [i for i, n in enumerate(model.graph.node)
             if any(types.get(o) == onnx.TensorProto.FLOAT for o in n.output)]
    x = np.fromfile(inp, dtype=np.float32).reshape(1, 3, TILE, TILE)
    lo, hi = 0, len(cands) - 1  # invariant: cands[hi] mismatches (final output does)
    log = []
    while lo < hi:
        mid = (lo + hi) // 2
        k = cands[mid]
        sub = work / "bisect_sub.onnx"
        node, t = build_sub(model, types, k, sub)
        bad, info, shape = mismatch(probe, sub, inp, work, x)
        log.append(dict(index=k, op=node.op_type, name=node.name, tensor=t, bad=bad, info=info, shape=list(shape)))
        print(f"node {k:5d} {node.op_type:<12} {node.name:<50} {'BAD ' if bad else 'ok  '} {info} {list(shape)}")
        if bad:
            hi = mid
        else:
            lo = mid + 1
    k = cands[hi]
    node = model.graph.node[k]
    sub = work / "first_bad_sub.onnx"
    build_sub(model, types, k, sub)
    bad, info, shape = mismatch(probe, sub, inp, work, x)
    attrs = {a.name: str(helper.get_attribute_value(a))[:80] for a in node.attribute}
    print("FIRST BAD:", k, node.op_type, node.name, list(node.input), "->", list(node.output), attrs, info)
    prev = model.graph.node[cands[hi - 1]] if hi > 0 else None
    if prev is not None:
        print("last good float node:", cands[hi - 1], prev.op_type, prev.name)
    (work / "bisect.json").write_text(json.dumps(dict(steps=log, first_bad=dict(
        index=k, op=node.op_type, name=node.name, inputs=list(node.input), outputs=list(node.output),
        attrs=attrs, info=info)), indent=1))


if __name__ == "__main__":
    main()
