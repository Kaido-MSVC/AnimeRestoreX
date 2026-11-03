"""Print opset / IR / I/O signature / external-data use of nunif ONNX models.

Usage: python -I inspect_models.py <onnx_models dir> [out.json]
Reads the models only (no inference). Paths are passed in; nothing is imported
from the model directory.
"""
import json
import sys
from pathlib import Path

import onnx
from onnx.external_data_helper import uses_external_data

ELEM = {v: k for k, v in onnx.TensorProto.DataType.items()}


def dims(t):
    out = []
    for d in t.type.tensor_type.shape.dim:
        out.append(d.dim_param if d.dim_param else d.dim_value)
    return out


def describe(path: Path):
    m = onnx.load(str(path), load_external_data=False)
    ext = any(uses_external_data(t) for t in m.graph.initializer)
    ops = sorted({n.op_type for n in m.graph.node})
    init_names = {t.name for t in m.graph.initializer}
    return {
        "file": path.as_posix(),
        "ir_version": m.ir_version,
        "opset": {o.domain or "ai.onnx": o.version for o in m.opset_import},
        "producer": f"{m.producer_name} {m.producer_version}".strip(),
        "inputs": [{"name": i.name, "dtype": ELEM[i.type.tensor_type.elem_type], "shape": dims(i)}
                   for i in m.graph.input if i.name not in init_names],
        "outputs": [{"name": o.name, "dtype": ELEM[o.type.tensor_type.elem_type], "shape": dims(o)}
                    for o in m.graph.output],
        "external_data": ext,
        "initializer_dtypes": sorted({ELEM[t.data_type] for t in m.graph.initializer}),
        "node_count": len(m.graph.node),
        "op_types": ops,
    }


def main():
    root = Path(sys.argv[1])
    files = sorted(root.glob("swin_unet/art/*.onnx")) + sorted(root.glob("utils/*.onnx"))
    result = []
    for f in files:
        d = describe(f)
        d["file"] = f.relative_to(root).as_posix()
        result.append(d)
        ins = "; ".join(f"{i['name']}:{i['dtype']}{i['shape']}" for i in d["inputs"])
        outs = "; ".join(f"{o['name']}:{o['dtype']}{o['shape']}" for o in d["outputs"])
        print(f"{d['file']:<40} IR{d['ir_version']} opset{d['opset']} ext={d['external_data']} "
              f"init={d['initializer_dtypes']} in=[{ins}] out=[{outs}] nodes={d['node_count']}")
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text(json.dumps(result, indent=1), encoding="utf-8")
    union = sorted({op for d in result if d["file"].startswith("swin_unet") for op in d["op_types"]})
    print("swin_unet op types:", ", ".join(union))
    print("producer(s):", sorted({d["producer"] for d in result}))


if __name__ == "__main__":
    main()
