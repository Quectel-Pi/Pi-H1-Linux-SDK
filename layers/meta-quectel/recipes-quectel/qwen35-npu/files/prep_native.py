"""Turn float32 calibration samples into qnn-net-run native inputs.

The converter emits tensors in NHWC ("native") layout: axis 1 moves to the end,
so ONNX dims [d0,d1,...,dn-1] become [d0,d2,...,dn-1,d1].
Quantization is scale/offset per tensor from the context binary info JSON.
"""
import argparse
import json
import os

import numpy as np


def onnx_dims(native):
    """Invert the NCHW->NHWC permutation."""
    return [native[0], native[-1]] + list(native[1:-1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ctx_json", required=True)
    ap.add_argument("--sample_dir", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--raw", default="uint16", choices=["uint16", "float32"])
    ap.add_argument("--all_samples", action="store_true",
                    help="sample_dir holds sNNN/<tensor>.raw subdirs")
    args = ap.parse_args()

    d = json.load(open(args.ctx_json))
    g = d["info"]["graphs"][0]["info"]
    os.makedirs(args.out, exist_ok=True)

    samples = [args.sample_dir]
    if args.all_samples:
        samples = sorted(os.path.join(args.sample_dir, s) for s in os.listdir(args.sample_dir)
                         if os.path.isdir(os.path.join(args.sample_dir, s)))

    lines = []
    for si, sdir in enumerate(samples):
        odir = os.path.join(args.out, "s%03d" % si)
        os.makedirs(odir, exist_ok=True)
        files = []
        for t in g["graphInputs"]:
            i = t["info"]
            name, native = i["name"], i["dimensions"]
            q = i["quantizeParams"]["scaleOffset"]
            a = onnx_dims(native)
            x = np.fromfile(os.path.join(sdir, name + ".raw"), dtype=np.float32).reshape(a)
            axes = ([0] + list(range(2, x.ndim)) + [1]) if not os.environ.get('NO_LAYOUT') else list(range(x.ndim))
            xn = np.ascontiguousarray(np.transpose(x, axes))
            if not os.environ.get('NO_LAYOUT'):
                assert list(xn.shape) == native, (name, xn.shape, native)
            if args.raw == "float32":
                qx = np.ascontiguousarray(xn, np.float32)
            else:
                qx = np.clip(np.round(xn / q["scale"] - q["offset"]), 0, 65535).astype(np.uint16)
            fn = os.path.join(odir, name + ".raw")
            qx.tofile(fn)
            files.append(fn)
        lines.append(" ".join(files))
    with open(os.path.join(args.out, "input_list.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {len(samples)} sample(s) of {len(files)} tensors to {args.out}")


if __name__ == "__main__":
    main()
