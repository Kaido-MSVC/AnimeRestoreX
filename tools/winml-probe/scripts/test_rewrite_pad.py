"""Self-test for rewrite_pad.py: rewrite cases must match the original on ORT CPU
bit-for-bit, and the cases it must refuse must STOP (exit 2).

  python -I test_rewrite_pad.py <scratch dir>
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from onnx import TensorProto, helper, numpy_helper

HERE = Path(__file__).resolve().parent


def model(path, nodes, inits, mode=None):
    x = helper.make_tensor_value_info("x", TensorProto.FLOAT, [1, 3, "h", "w"])
    y = helper.make_tensor_value_info("y", TensorProto.FLOAT, ["n", "c", "oh", "ow"])
    m = helper.make_model(helper.make_graph(nodes, path.stem, [x], [y], inits),
                          opset_imports=[helper.make_opsetid("", 17)], ir_version=8)
    onnx.save(m, str(path))


def i64(name, v):
    return numpy_helper.from_array(np.array(v, np.int64), name)


def main():
    d = Path(sys.argv[1])
    d.mkdir(parents=True, exist_ok=True)
    pad = lambda **kw: helper.make_node("Pad", ["x", "pads"] + kw.pop("extra", []), ["y"], **kw)
    cases = {
        # name: (nodes, inits, expected: "ok" | "stop")
        "neg_const": ([pad(mode="constant")], [i64("pads", [0, 0, -6, -5, 0, 0, -6, 0])], "ok"),
        "neg_cast": ([helper.make_node("Cast", ["pf"], ["pads"], to=TensorProto.INT64), pad(mode="constant")],
                     [numpy_helper.from_array(np.array([0, 0, -6, -6, 0, 0, -6, -6], np.float32), "pf")], "ok"),
        "mixed_cv": ([pad(mode="constant", extra=["cv"])],
                     [i64("pads", [0, 0, -3, 4, 0, 0, 2, -7]),
                      numpy_helper.from_array(np.array(0.25, np.float32), "cv")], "ok"),
        "reflect": ([pad(mode="reflect")], [i64("pads", [0, 0, -2, -2, 0, 0, -2, -2])], "stop"),
        "dyn_neg": ([helper.make_node("Shape", ["x"], ["s"]),
                     helper.make_node("Gather", ["s", "i2"], ["h1"], axis=0),
                     helper.make_node("Sub", ["zero", "h1"], ["nh"]),
                     helper.make_node("Mod", ["nh", "six"], ["m"]),
                     helper.make_node("Sub", ["zero", "m"], ["nm"]),
                     helper.make_node("Concat", ["z6", "nm", "z1"], ["pads"], axis=0),
                     pad(mode="constant")],
                    [i64("i2", [2]), i64("zero", [0]), i64("six", [6]), i64("z6", [0] * 6),
                     i64("z1", [0])], "stop"),
    }
    x = np.random.default_rng(1).random((1, 3, 37, 41), dtype=np.float32)
    bad = 0
    for name, (nodes, inits, expect) in cases.items():
        src, dst = d / f"{name}.onnx", d / f"{name}_rw.onnx"
        model(src, nodes, inits)
        r = subprocess.run([sys.executable, "-I", str(HERE / "rewrite_pad.py"), str(src), str(dst)],
                           capture_output=True, text=True)
        if expect == "stop":
            ok = r.returncode == 2 and "STOP" in r.stderr
            print(f"{name:<10} expect STOP -> exit {r.returncode} {r.stderr.strip()} {'PASS' if ok else 'FAIL'}")
        else:
            a = ort.InferenceSession(str(src), providers=["CPUExecutionProvider"]).run(None, {"x": x})[0]
            b = ort.InferenceSession(str(dst), providers=["CPUExecutionProvider"]).run(None, {"x": x})[0]
            ops = [n.op_type for n in onnx.load(str(dst)).graph.node if n.op_type in ("Pad", "Slice")]
            ok = r.returncode == 0 and a.shape == b.shape and a.tobytes() == b.tobytes()
            print(f"{name:<10} expect rewrite -> exit {r.returncode} ops {ops} shape {a.shape} "
                  f"bitwise={'yes' if a.tobytes() == b.tobytes() else 'NO'} {'PASS' if ok else 'FAIL'}")
        bad += not ok
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
