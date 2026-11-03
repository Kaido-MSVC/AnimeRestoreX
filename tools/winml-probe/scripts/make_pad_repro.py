"""Minimal repro models for the DML negative-pad problem (opset 17, FP32).

  python -I make_pad_repro.py <out dir>

pad_neg_const.onnx : y = Pad(x, pads=initializer [0,0,-6,-6,0,0,-6,-6]), mode=constant
pad_neg_dyn.onnx   : same, but pads come from Cast(float initializer -> int64), the
                     pattern used by the exported swin_unet graph (/unet/Cast -> /unet/Pad)
slice_crop.onnx    : the equivalent crop written as Slice(starts=6, ends=-6, axes=[2,3])
pad_pos_const.onnx : positive pads [0,0,6,6,0,0,6,6] (control)
Input x: [1,3,256,256] float32. Reference with numpy is trivial (crop / pad).
"""
import sys
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper


def save(path, nodes, inits, out_shape):
    x = helper.make_tensor_value_info("x", TensorProto.FLOAT, [1, 3, 256, 256])
    y = helper.make_tensor_value_info("y", TensorProto.FLOAT, out_shape)
    g = helper.make_graph(nodes, path.stem, [x], [y], inits)
    m = helper.make_model(g, opset_imports=[helper.make_opsetid("", 17)], ir_version=8)
    onnx.checker.check_model(m)
    onnx.save(m, str(path))


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    neg = np.array([0, 0, -6, -6, 0, 0, -6, -6], np.int64)
    save(out / "pad_neg_const.onnx",
         [helper.make_node("Pad", ["x", "pads"], ["y"], mode="constant")],
         [numpy_helper.from_array(neg, "pads")], [1, 3, 244, 244])
    save(out / "pad_neg_dyn.onnx",
         [helper.make_node("Cast", ["padsf"], ["pads"], to=TensorProto.INT64),
          helper.make_node("Pad", ["x", "pads"], ["y"], mode="constant")],
         [numpy_helper.from_array(neg.astype(np.float32), "padsf")], [1, 3, 244, 244])
    save(out / "slice_crop.onnx",
         [helper.make_node("Slice", ["x", "st", "en", "ax"], ["y"])],
         [numpy_helper.from_array(np.array([6, 6], np.int64), "st"),
          numpy_helper.from_array(np.array([-6, -6], np.int64), "en"),
          numpy_helper.from_array(np.array([2, 3], np.int64), "ax")], [1, 3, 244, 244])
    save(out / "pad_pos_const.onnx",
         [helper.make_node("Pad", ["x", "pads"], ["y"], mode="constant")],
         [numpy_helper.from_array(-neg, "pads")], [1, 3, 268, 268])
    print("wrote", sorted(p.name for p in out.glob("*.onnx")))


if __name__ == "__main__":
    main()
