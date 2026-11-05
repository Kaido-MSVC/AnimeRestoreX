"""Self-test for fix_reshape_allowzero.py: every case that passes must match the original on
ORT CPU bit-for-bit, a graph with nothing to clear must come out byte-identical, and a shape with
both 0 and -1 must STOP (exit 2) without writing anything.

  python -I test_fix_reshape_allowzero.py <scratch dir>
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from onnx import TensorProto, helper, numpy_helper

HERE = Path(__file__).resolve().parent


def i64(name, v):
    return numpy_helper.from_array(np.array(v, np.int64), name)


def save(path, nodes, inits):
    x = helper.make_tensor_value_info("x", TensorProto.FLOAT, [2, 3, 4])
    y = helper.make_tensor_value_info("y", TensorProto.FLOAT, ["a", "b"])
    m = helper.make_model(helper.make_graph(nodes, path.stem, [x], [y], inits),
                          opset_imports=[helper.make_opsetid("", 20)], ir_version=10)
    onnx.save(m, str(path))


def allowzero_values(path):
    return [next((a.i for a in n.attribute if a.name == "allowzero"), 0)
            for n in onnx.load(str(path)).graph.node if n.op_type == "Reshape"]


def main():
    d = Path(sys.argv[1])
    d.mkdir(parents=True, exist_ok=True)
    reshape = lambda shape="s", **kw: helper.make_node("Reshape", ["x", shape], ["y"], **kw)
    dyn = [helper.make_node("Shape", ["x"], ["sx"]), helper.make_node("Gather", ["sx", "i0"], ["n"], axis=0),
           helper.make_node("Unsqueeze", ["n", "a0"], ["n1"]), helper.make_node("Concat", ["n1", "m1"], ["s"], axis=0)]
    cases = {
        # name: (nodes, inits, expected result, expected allowzero after the step)
        "static_minus1": ([reshape(allowzero=1)], [i64("s", [2, -1])], "ok", [0]),
        "static_positive": ([reshape(allowzero=1)], [i64("s", [4, 6])], "same", [1]),
        "no_attribute": ([reshape()], [i64("s", [2, -1])], "same", [0]),
        "allowzero_0": ([reshape(allowzero=0)], [i64("s", [2, -1])], "same", [0]),
        "dynamic_kept": (dyn + [reshape(allowzero=1)], [i64("i0", 0), i64("a0", [0]), i64("m1", [-1])], "same", [1]),
        "zero_and_minus1": ([reshape(allowzero=1)], [i64("s", [0, -1])], "stop", None),
    }
    x = np.arange(24, dtype=np.float32).reshape(2, 3, 4)
    failed = 0
    for name, (nodes, inits, want, az) in cases.items():
        src, dst = d / f"{name}.onnx", d / f"{name}.out.onnx"
        dst.unlink(missing_ok=True)
        save(src, nodes, inits)
        code = subprocess.run([sys.executable, "-I", str(HERE / "fix_reshape_allowzero.py"), str(src), str(dst)],
                              capture_output=True, text=True).returncode
        if want == "stop":
            ok = code == 2 and not dst.exists()
        else:
            ok = code == 0 and allowzero_values(dst) == az
            if ok and want == "same":
                ok = src.read_bytes() == dst.read_bytes()
            if ok:
                a = ort.InferenceSession(str(src), providers=["CPUExecutionProvider"]).run(None, {"x": x})[0]
                b = ort.InferenceSession(str(dst), providers=["CPUExecutionProvider"]).run(None, {"x": x})[0]
                ok = a.shape == b.shape and a.tobytes() == b.tobytes()
        print(f"{name:<18} {'PASS' if ok else 'FAIL'} (exit {code})")
        failed += not ok
    print(f"{len(cases) - failed}/{len(cases)} passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
