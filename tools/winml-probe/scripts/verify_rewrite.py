"""Verification of the Pad->Slice rewritten models (dev only).

  python -I verify_rewrite.py make-input <work dir> <tile>
      Deterministic anime-like test tile of side <tile> (valid swin tile), written
      as input_<tile>.bin (float32 [1,3,T,T], RGB 0..1). 256 reuses reference.py.

  python -I verify_rewrite.py cpu-bitwise <work dir> <tile> <orig dir> <rw dir> <model> [...]
      Python ORT CPU: original vs rewritten, must be bit-identical. Also writes the
      reference ref_<model>.bin / .shape.json (original model, CPU) for compare.

  python -I verify_rewrite.py compare <work dir> <model> [...]
      reference.py compare (out_<model>_<ep>.bin vs ref) + worst-tile listing.

  python -I verify_rewrite.py nodes <probe.exe> <model.onnx> <input.bin> <tile> <scratch> [chunk]
      Node-by-node DML check: every float intermediate tensor is exposed as a
      graph output (in chunks), run once by winml_probe --dump-dir on DML and once by
      Python ORT CPU, and compared. Prints the worst nodes; writes nodes_<model>.json.
"""
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


def load_x(p: Path, tile: int):
    return np.fromfile(p, dtype=np.float32).reshape(1, 3, tile, tile)


def make_input(work: Path, tile: int):
    import reference
    if tile == 256:
        reference.make_input(work)
        return
    # Tile the 240x240 core image (mirrored so that there are no hard seams), then
    # replicate-pad by 8 like the 256 input.
    from PIL import Image
    core_png = work / "input_core.png"
    if not core_png.exists():
        reference.make_input(work)
    core = np.asarray(Image.open(core_png).convert("RGB")).astype(np.float32) / 255.0
    n = tile - 16
    reps = -(-n // core.shape[0])
    row = np.concatenate([core if i % 2 == 0 else core[:, ::-1] for i in range(reps)], axis=1)
    big = np.concatenate([row if i % 2 == 0 else row[::-1] for i in range(reps)], axis=0)[:n, :n]
    x = np.pad(big.transpose(2, 0, 1), ((0, 0), (8, 8), (8, 8)), mode="edge")[None]
    x = np.ascontiguousarray(x, dtype=np.float32)
    assert x.shape == (1, 3, tile, tile)
    x.tofile(work / f"input_{tile}.bin")
    print("wrote", work / f"input_{tile}.bin", x.shape)


def cpu_bitwise(work: Path, tile: int, orig: Path, rw: Path, names):
    import onnxruntime as ort
    x = load_x(work / f"input_{tile}.bin", tile)
    rows, bad = [], 0
    for name in names:
        ys = []
        for d in (orig, rw):
            s = ort.InferenceSession(str(d / f"{name}.onnx"), providers=["CPUExecutionProvider"])
            ys.append(s.run(["y"], {"x": x})[0])
        a, b = ys
        same = a.shape == b.shape and a.tobytes() == b.tobytes()
        a.astype(np.float32).tofile(work / f"ref_{name}.bin")
        (work / f"ref_{name}.shape.json").write_text(json.dumps(list(a.shape)))
        rows.append(dict(model=name, tile=tile, shape=list(a.shape), bitwise=same,
                         max_abs=float(np.abs(a - b).max()) if a.shape == b.shape else None,
                         ort=ort.__version__))
        print(f"{name:<16} tile {tile} out {list(a.shape)} orig vs rewritten CPU: "
              f"{'BIT-IDENTICAL' if same else 'DIFFERENT max_abs=%g' % rows[-1]['max_abs']}")
        bad += not same
    (work / "cpu_bitwise.json").write_text(json.dumps(rows, indent=1))
    sys.exit(1 if bad else 0)


def compare(work: Path, names):
    import reference
    reference.compare(work, names)
    # Worst 32x32 output block (by MSE) per model, for manual inspection.
    for name in names:
        p = work / f"out_{name}_dml.bin"
        if not p.exists():
            continue
        shape = json.loads((work / f"ref_{name}.shape.json").read_text())
        ref = np.fromfile(work / f"ref_{name}.bin", dtype=np.float32).reshape(shape)[0]
        out = np.fromfile(p, dtype=np.float32).reshape(shape)[0]
        H = shape[2] // 32 * 32
        e = ((out[:, :H, :H] - ref[:, :H, :H]).astype(np.float64) ** 2)
        e = e.mean(0).reshape(H // 32, 32, H // 32, 32).mean((1, 3))
        i, j = np.unravel_index(int(e.argmax()), e.shape)
        print(f"{name:<16} worst 32x32 block at y={i*32} x={j*32}: PSNR {10*math.log10(1/max(e.max(),1e-30)):.2f} dB")


def spot(work: Path, tile: int, out_png: Path, names):
    """Side-by-side crops for manual inspection: input (nearest-upscaled) | CPU ref | DML |
    |DML-CPU| x 10000 (so a 1e-4 error shows as full white)."""
    from PIL import Image
    x = load_x(work / f"input_{tile}.bin", tile)[0]
    rows = []
    for name in names:
        shape = json.loads((work / f"ref_{name}.shape.json").read_text())
        s = shape[2] // (tile - 16)  # 1/2/4
        ref = np.fromfile(work / f"ref_{name}.bin", dtype=np.float32).reshape(shape)[0]
        dml = np.fromfile(work / f"out_{name}_dml.bin", dtype=np.float32).reshape(shape)[0]
        c = 48 * s  # crop 48 input px -> 48*s output px, then everything shown at 192 px
        y0, x0 = 140 * s, 120 * s  # region with outlines, thin strokes and the triangle edge
        inp = x[:, 8 + y0 // s: 8 + y0 // s + 48, 8 + x0 // s: 8 + x0 // s + 48]
        tiles = [inp, ref[:, y0:y0 + c, x0:x0 + c], dml[:, y0:y0 + c, x0:x0 + c],
                 np.clip(np.abs(dml - ref)[:, y0:y0 + c, x0:x0 + c] * 1e4, 0, 1)]
        ims = [Image.fromarray((np.clip(t, 0, 1).transpose(1, 2, 0) * 255 + 0.5).astype(np.uint8))
               .resize((192, 192), Image.NEAREST) for t in tiles]
        row = Image.new("RGB", (4 * 196, 192), (128, 128, 128))
        for k, im in enumerate(ims):
            row.paste(im, (k * 196, 0))
        rows.append(row)
    sheet = Image.new("RGB", (4 * 196, len(rows) * 196), (128, 128, 128))
    for k, r in enumerate(rows):
        sheet.paste(r, (0, k * 196))
    sheet.save(out_png)
    print("wrote", out_png)


def padvals(model_path: Path, tiles):
    """Actual runtime pads of every Pad node (Python ORT CPU) for each tile size T.
    Exposes the pads input tensors through Identity outputs; input content is irrelevant."""
    import onnx
    import onnxruntime as ort
    from onnx import helper
    m = onnx.load(str(model_path))
    pads = [(n.name, n.input[1]) for n in m.graph.node if n.op_type == "Pad"]
    g = m.graph
    outs = [helper.make_tensor_value_info(f"pv_{k}", onnx.TensorProto.INT64, None) for k in range(len(pads))]
    nodes = list(g.node) + [helper.make_node("Identity", [t], [f"pv_{k}"]) for k, (_, t) in enumerate(pads)]
    mm = helper.make_model(helper.make_graph(nodes, "pv", list(g.input), outs, list(g.initializer)),
                           opset_imports=m.opset_import, ir_version=m.ir_version)
    so = ort.SessionOptions()
    so.log_severity_level = 3
    sess = ort.InferenceSession(mm.SerializeToString(), so, providers=["CPUExecutionProvider"])
    rows = []
    for t in tiles:
        x = np.zeros((1, 3, t, t), np.float32)
        legal = t > 16 and (t - 16) % 48 == 0
        try:
            vals = sess.run([o.name for o in outs], {"x": x})
        except Exception as e:  # illegal tiles can fail outright (skip-connection shape mismatch)
            msg = str(e).splitlines()[0][-160:]
            rows.append(dict(tile=t, legal=legal, error=msg))
            print(f"T={t:4d} legal={legal!s:<5} ORT CPU error: {msg}")
            continue
        attn = [v.tolist() for (nm, _), v in zip(pads, vals) if "/attn/" in nm]
        nonzero = [nm for (nm, _), v in zip(pads, vals) if "/attn/" in nm and np.any(v != 0)]
        rows.append(dict(tile=t, legal=legal, attn_all_zero=not nonzero, attn_nonzero=nonzero,
                         attn_max=int(max(max(a) for a in attn)), attn_min=int(min(min(a) for a in attn))))
        print(f"T={t:4d} legal={legal!s:<5} attention pads: min {rows[-1]['attn_min']} max {rows[-1]['attn_max']}"
              f" nonzero nodes {len(nonzero)}/{len(attn)}")
    return rows


def sub_model(m, tensors):
    """Ancestors of `tensors` only, each exposed as probe_out_<k> through an Identity
    (the construction bisect_dml.py used in P1). Exposing raw intermediate tensors of the
    full graph as outputs made the DML EP fail (empty OrtValue / E_INVALIDARG in
    DmlGraphFusionHelper / DXGI 887A0020) - see the P1 fix section of the report."""
    from onnx import helper
    g = m.graph
    producers = {o: i for i, n in enumerate(g.node) for o in n.output}
    keep, stack = set(), [producers[t] for t in tensors]
    while stack:
        i = stack.pop()
        if i in keep:
            continue
        keep.add(i)
        stack.extend(producers[x] for x in g.node[i].input if x in producers)
    nodes = [g.node[i] for i in sorted(keep)]
    used = {i for n in nodes for i in n.input}
    inits = [t for t in g.initializer if t.name in used]
    outs = []
    for k, t in enumerate(tensors):
        nodes.append(helper.make_node("Identity", [t], [f"probe_out_{k}"], name=f"probe_out_{k}"))
        outs.append(helper.make_tensor_value_info(f"probe_out_{k}", 1, None))
    graph = helper.make_graph(nodes, "sub", list(g.input), outs, inits)
    return helper.make_model(graph, opset_imports=m.opset_import, ir_version=m.ir_version)


def nodes(probe: Path, model_path: Path, inp: Path, tile: int, scratch: Path, chunk: int):
    import onnx
    import onnxruntime as ort
    from onnx import helper, shape_inference
    m = onnx.load(str(model_path))
    inf = shape_inference.infer_shapes(m)
    types = {v.name: v.type.tensor_type.elem_type for v in inf.graph.value_info}
    order, owner = [], {}
    for i, n in enumerate(m.graph.node):
        for o in n.output:
            if o and types.get(o) == onnx.TensorProto.FLOAT:
                order.append(o)
                owner[o] = (i, n.op_type, n.name)
    x = load_x(inp, tile)
    results, failures = [], []
    scratch.mkdir(parents=True, exist_ok=True)
    work = [order[c0:c0 + chunk] for c0 in range(0, len(order), chunk)]
    done = 0
    while work:
        names = work.pop(0)
        sub = scratch / "nodes_sub.onnx"
        onnx.save(sub_model(m, names), str(sub))
        dump = scratch / "dump"
        if dump.exists():
            shutil.rmtree(dump)
        r = subprocess.run([str(probe), "--model", str(sub), "--input", str(inp), "--ep", "dml",
                            "--tile", str(tile), "--scale", "1", "--offset", "0", "--dump-dir", str(dump)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            err = next((l for l in (r.stdout + r.stderr).splitlines() if "ERROR" in l), "?")
            if len(names) > 1:  # split and retry (DXGI 887A0020 shows up on large exposed-output sets)
                h = len(names) // 2
                work[:0] = [names[:h], names[h:]]
                print(f"  probe failed on {len(names)} outputs ({err[-60:]}), splitting", flush=True)
                continue
            i, op, nn = owner[names[0]]
            failures.append(dict(index=i, op=op, name=nn, tensor=names[0], error=err))
            print(f"  probe failed on single tensor #{i} {op} {nn}: {err}", flush=True)
            continue
        sess = ort.InferenceSession(str(sub), providers=["CPUExecutionProvider"])
        onames = [f"probe_out_{k}" for k in range(len(names))]
        refs = sess.run(onames, {"x": x})
        idx = {}
        for line in (dump / "index.txt").read_text().splitlines():
            i, nm, _ = line.split("\t")
            idx[nm] = dump / f"{i}.bin"
        for t, on, ref in zip(names, onames, refs):
            d = np.fromfile(idx[on], dtype=np.float32)
            if d.size != ref.size:
                err, rel, zero = math.inf, math.inf, False
            else:
                diff = np.abs(d.astype(np.float64) - ref.ravel())
                err = float(diff.max())
                rel = err / (float(np.abs(ref).max()) + 1e-6)
                zero = bool(np.all(d == 0) and np.any(ref != 0))
            i, op, nn = owner[t]
            results.append(dict(index=i, op=op, name=nn, tensor=t, max_abs=err, rel=rel, all_zero=zero))
        shutil.rmtree(dump)
        done += len(names)
        print(f"  {done}/{len(order)} tensors checked", flush=True)
    bad = sorted([r for r in results if r["rel"] > 1e-2 or r["all_zero"]], key=lambda r: r["index"])
    worst = sorted(results, key=lambda r: -r["rel"])[:5]
    name = model_path.stem
    (scratch / f"nodes_{name}.json").write_text(json.dumps(dict(
        model=name, tensors=len(order), checked=len(results), probe_failures=failures, bad=bad, worst=worst,
        max_rel=max(r["rel"] for r in results), first_bad=bad[0] if bad else None), indent=1))
    print(f"{name}: {len(results)}/{len(order)} float tensors checked, max rel err "
          f"{max(r['rel'] for r in results):.3e}, bad (rel>1e-2 or all-zero) {len(bad)}, "
          f"single-tensor probe failures {len(failures)}")
    for r in worst:
        print(f"   worst #{r['index']:5d} {r['op']:<10} {r['name']:<55} rel {r['rel']:.2e} abs {r['max_abs']:.2e}")
    if bad or failures:
        print("   FIRST BAD:", bad[0] if bad else failures[0])
        sys.exit(1)


def main():
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "make-input":
        make_input(Path(args[0]), int(args[1]))
    elif cmd == "cpu-bitwise":
        cpu_bitwise(Path(args[0]), int(args[1]), Path(args[2]), Path(args[3]), args[4:])
    elif cmd == "compare":
        compare(Path(args[0]), args[1:])
    elif cmd == "padvals":  # padvals <model.onnx> <out.json> <tile> [...]
        rows = padvals(Path(args[0]), [int(t) for t in args[2:]])
        Path(args[1]).write_text(json.dumps(rows, indent=1))
    elif cmd == "spot":
        spot(Path(args[0]), int(args[1]), Path(args[2]), args[3:])
    elif cmd == "nodes":
        nodes(Path(args[0]), Path(args[1]), Path(args[2]), int(args[3]), Path(args[4]),
              int(args[5]) if len(args) > 5 else 400)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
