"""Offline rewrite of negative ONNX Pad nodes (DirectML EP workaround). Dev only.

  python -I rewrite_pad.py <in.onnx> <out.onnx> [--report report.json]

Why: the DirectML EP in Windows ML 2.5.96 (ORT 1.28.3, DirectML 1.15.4) returns
zeros for Pad with negative pads (= cropping). See docs/v2.0-P0-P1報告.md §6.4.

Rules for every Pad node:
  * mode attribute must be "constant" (absent == constant) or "edge". Otherwise: STOP.
  * "edge" is only let through when its pads are proven >= 0 (it is then left
    untouched). Negative or unproven edge pads: STOP. Edge is never rewritten.
  * an `axes` input (opset >= 18) is not handled: STOP.
  * pads are resolved in one of two ways:
      - static: the pads input depends only on initializers / Constant nodes.
        It is evaluated exactly (onnx.reference) - no guessing.
      - dynamic (depends on graph inputs, e.g. through Shape): a conservative
        interval analysis bounds every element. If the lower bound is >= 0 the
        node never crops, needs no rewrite and is left untouched. Otherwise: STOP.
  * static pads, all <= 0 (and at least one < 0): replaced by an equivalent Slice.
  * static pads, mixed signs: Slice (the negative part) followed by Pad (the
    positive part, same mode / constant_value).
  * static pads, all >= 0: untouched.
Weights and all other nodes are kept as they are; the original pads subgraph
of a rewritten node stays in the graph (unused; ORT drops dead nodes).
STOP = exit code 2 with a message; nothing is written.
"""
import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

INT64_MAX = np.iinfo(np.int64).max
INF = math.inf


class Stop(Exception):
    pass


def sha256(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


class Graph:
    def __init__(self, model):
        self.g = model.graph
        self.model = model
        self.producer = {o: n for n in self.g.node for o in n.output if o}
        self.inits = {t.name: t for t in self.g.initializer}
        self.inputs = {i.name for i in self.g.input} - set(self.inits)

    def ancestors(self, name):
        """Nodes needed to compute `name`, and whether a graph input is reached."""
        nodes, seen, stack, dyn = [], set(), [name], False
        while stack:
            t = stack.pop()
            if not t or t in seen:
                continue
            seen.add(t)
            if t in self.inits:
                continue
            if t in self.inputs or t not in self.producer:
                dyn = True
                continue
            n = self.producer[t]
            if id(n) not in {id(x) for x in nodes}:
                nodes.append(n)
            stack.extend(n.input)
        return nodes, dyn

    def eval_static(self, name):
        from onnx.reference import ReferenceEvaluator
        c = self._const(name)  # initializer or Constant node: no evaluation needed
        if c is not None:
            return np.asarray(c)
        nodes, dyn = self.ancestors(name)
        assert not dyn
        order = {id(n): i for i, n in enumerate(self.g.node)}
        nodes.sort(key=lambda n: order[id(n)])
        used = {i for n in nodes for i in n.input}
        inits = [t for t in self.g.initializer if t.name in used]
        out = helper.make_tensor_value_info(name, TensorProto.INT64, None)
        sub = helper.make_graph(nodes, "pads", [], [out], inits)
        m = helper.make_model(sub, opset_imports=self.model.opset_import, ir_version=self.model.ir_version)
        return np.asarray(ReferenceEvaluator(m).run(None, {})[0])

    # ---- interval analysis: one [lo, hi] bound for all elements of a tensor ----
    def interval(self, name, memo=None):
        memo = {} if memo is None else memo
        if name in memo:
            return memo[name]
        r = self._interval(name, memo)
        memo[name] = r
        return r

    def _const(self, name):
        if name in self.inits:
            return numpy_helper.to_array(self.inits[name])
        n = self.producer.get(name)
        if n is not None and n.op_type == "Constant":
            return numpy_helper.to_array(n.attribute[0].t)
        return None

    def _interval(self, name, memo):
        c = self._const(name)
        if c is not None:
            return (float(c.min()), float(c.max())) if c.size else (INF, -INF)
        if name in self.inputs or name not in self.producer:
            return (-INF, INF)
        n = self.producer[name]
        op = n.op_type
        iv = lambda i: self.interval(n.input[i], memo)
        attrs = {a.name: helper.get_attribute_value(a) for a in n.attribute}
        if op == "Shape":
            return (0.0, INF)
        if op in ("Gather", "Unsqueeze", "Squeeze", "Reshape", "Transpose", "Slice", "Identity",
                  "Expand", "Flatten"):
            return iv(0)  # element selection / re-arrangement only
        if op == "Cast":
            lo, hi = iv(0)
            return (math.floor(lo) if lo > -INF else lo, math.ceil(hi) if hi < INF else hi)
        if op == "Concat":
            ivs = [self.interval(i, memo) for i in n.input]
            return (min(a for a, _ in ivs), max(b for _, b in ivs))
        if op == "ConstantOfShape":
            v = attrs.get("value")
            v = numpy_helper.to_array(v) if v is not None else np.zeros(1)
            return (float(v.min()), float(v.max()))
        if op == "Add":
            (a, b), (c, d) = iv(0), iv(1)
            return (a + c, b + d)
        if op == "Sub":
            (a, b), (c, d) = iv(0), iv(1)
            return (a - d, b - c)
        if op == "Mod" and attrs.get("fmod", 0) == 0:
            dv = self._const(n.input[1])
            if dv is not None and dv.size and np.all(dv > 0) and np.issubdtype(dv.dtype, np.integer):
                return (0.0, float(dv.max()) - 1)  # integer Mod, positive divisor -> [0, d-1]
        return (-INF, INF)  # unknown op: no bound


def pad_mode(node):
    for a in node.attribute:
        if a.name == "mode":
            return helper.get_attribute_value(a).decode()
    return "constant"


def rewrite(model):
    G = Graph(model)
    records, new_nodes, extra_inits = [], [], []
    for node in G.g.node:
        if node.op_type != "Pad":
            new_nodes.append(node)
            continue
        rec = dict(name=node.name, mode=pad_mode(node))
        if rec["mode"] not in ("constant", "edge"):
            raise Stop(f"{node.name}: mode={rec['mode']} (not constant or edge)")
        if len(node.input) > 3 and node.input[3]:
            raise Stop(f"{node.name}: has an axes input (not handled)")
        pads_name = node.input[1]
        _, dyn = G.ancestors(pads_name)
        if dyn:
            lo, hi = G.interval(pads_name)
            rec.update(kind="dynamic", bound=[lo, hi])
            if lo < 0:
                raise Stop(f"{node.name}: dynamic pads, lower bound {lo} not provably >= 0")
            rec["action"] = "untouched (dynamic, proven >= 0)"
            new_nodes.append(node)
            records.append(rec)
            continue
        pads = G.eval_static(pads_name).astype(np.int64).ravel()
        rec.update(kind="static", pads=pads.tolist())
        r = pads.size // 2
        begin, end = pads[:r], pads[r:]
        if (pads >= 0).all():
            rec["action"] = "untouched (static, >= 0)"
            new_nodes.append(node)
            records.append(rec)
            continue
        if rec["mode"] == "edge":
            raise Stop(f"{node.name}: mode=edge with negative pads {pads.tolist()}")
        # Slice for the negative part.
        axes = [i for i in range(r) if begin[i] < 0 or end[i] < 0]
        starts = [int(-begin[i]) if begin[i] < 0 else 0 for i in axes]
        ends = [int(end[i]) if end[i] < 0 else int(INT64_MAX) for i in axes]
        base = node.name.strip("/").replace("/", "_") + "_neg2slice"
        names = {k: f"{base}_{k}" for k in ("starts", "ends", "axes", "steps")}
        for k, v in (("starts", starts), ("ends", ends), ("axes", axes), ("steps", [1] * len(axes))):
            extra_inits.append(numpy_helper.from_array(np.array(v, np.int64), names[k]))
        positive = (pads > 0).any()
        slice_out = f"{base}_out" if positive else node.output[0]
        new_nodes.append(helper.make_node(
            "Slice", [node.input[0], names["starts"], names["ends"], names["axes"], names["steps"]],
            [slice_out], name=f"{node.name}_neg2slice"))
        rec.update(slice=dict(starts=starts, ends=ends, axes=axes))
        if positive:
            pos = np.maximum(pads, 0)
            pname = f"{base}_pos_pads"
            extra_inits.append(numpy_helper.from_array(pos.astype(np.int64), pname))
            ins = [slice_out, pname] + ([node.input[2]] if len(node.input) > 2 and node.input[2] else [])
            new_nodes.append(helper.make_node("Pad", ins, list(node.output), name=f"{node.name}_pospart",
                                              mode="constant"))
            rec.update(action="Slice + Pad", pad=pos.tolist())
        else:
            rec["action"] = "Slice"
        records.append(rec)
    del G.g.node[:]
    G.g.node.extend(new_nodes)
    G.g.initializer.extend(extra_inits)
    return records


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src", type=Path)
    ap.add_argument("dst", type=Path)
    ap.add_argument("--report", type=Path)
    a = ap.parse_args()
    model = onnx.load(str(a.src))
    try:
        records = rewrite(model)
    except Stop as e:
        print("STOP:", e, file=sys.stderr)
        sys.exit(2)
    onnx.checker.check_model(model, full_check=True)
    a.dst.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, str(a.dst))
    info = dict(src=a.src.name, src_sha256=sha256(a.src), dst=a.dst.name, dst_sha256=sha256(a.dst),
                dst_size=a.dst.stat().st_size, tool="rewrite_pad.py", tool_sha256=sha256(Path(__file__)),
                python=sys.version.split()[0], onnx=onnx.__version__, numpy=np.__version__, pads=records)
    for r in records:
        print(f"  {r['name']:<45} {r['kind']:<7} {r.get('pads', r.get('bound'))} -> {r['action']}")
    print(f"{a.src.name} -> {a.dst} sha256 {info['dst_sha256']}")
    if a.report:
        a.report.write_text(json.dumps(info, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
