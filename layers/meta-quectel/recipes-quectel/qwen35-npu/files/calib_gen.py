"""Generate calibration raws for the QNN converter from a closed-loop CPU run.

The decode graph carries 48 state tensors; feeding zeros for all of them makes
half the activations identically zero, and the converter then emits scale=0 for
those branches ("swish_16_opts: Bad range for input, scale is 0.000000").
So the samples must come from actually stepping the model.
"""
import argparse
import os

import numpy as np
import onnx
import onnxruntime as ort

NP = {1: np.float32, 10: np.float16, 11: np.float64, 6: np.int32, 7: np.int64}


def cos_sin(pos, theta=10000000.0, rot=64):
    inv = 1.0 / (theta ** (np.arange(0, rot, 2, dtype=np.float64) / rot))
    th = pos * inv
    c = np.cos(th).astype(np.float32)
    s = np.sin(th).astype(np.float32)
    return np.concatenate([c, c]), np.concatenate([s, s])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", default="08b_f16.onnx")
    ap.add_argument("--embed", default="emb08_f16.npy")
    ap.add_argument("--prompt", default="760,6511,314,9338,369")
    ap.add_argument("--steps", type=int, default=16)
    ap.add_argument("--ctx", type=int, default=2048)
    ap.add_argument("--out", default="calib")
    ap.add_argument("--raw_dtype", default="float16", choices=["float16", "float32"])
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    m = onnx.load(args.onnx, load_external_data=False)
    spec = {}
    for t in m.graph.input:
        tt = t.type.tensor_type
        spec[t.name] = ([d.dim_value for d in tt.shape.dim], NP[tt.elem_type])
    out_dt = {}
    for t in m.graph.output:
        out_dt[t.name] = NP[t.type.tensor_type.elem_type]

    so = ort.SessionOptions()
    so.log_severity_level = 3
    import os as _os
    if _os.environ.get("ORT_NOOPT"):
        so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    dis = [x for x in _os.environ.get("ORT_DISABLE_FUSIONS", "").split(",") if x]
    sess = ort.InferenceSession(args.onnx, sess_options=so,
                                providers=["CPUExecutionProvider"],
                                disabled_optimizers=dis or None)
    in_names = [i.name for i in sess.get_inputs()]
    out_names = [o.name for o in sess.get_outputs()]
    emb = np.load(args.embed, mmap_mode="r")

    # output -> input pairing by name (the fp graph uses layer_XX_..._out/_in)
    carry = {}
    for o in out_names:
        if o.endswith("_out") and o[:-4] + "_in" in in_names:
            carry[o] = o[:-4] + "_in"

    states = {n: np.zeros(spec[n][0], np.float32) for n in in_names
              if not any(k in n for k in ("hidden_in", "rope_cos", "rope_sin", "mask"))}
    tok = [int(x) for x in args.prompt.split(",")]
    lines = []
    for s in range(args.steps):
        pos = s
        t = tok[pos] if pos < len(tok) else prev
        cos, sin = cos_sin(pos)
        mask = np.full((1, 1, 1, args.ctx), -64.0, np.float32)
        mask[..., args.ctx - min(pos, args.ctx - 1) - 1:] = 0.0
        feed = {}
        for n in in_names:
            shape, dt = spec[n]
            if n == "hidden_in" or "hidden" in n:
                x = np.asarray(emb[t], np.float32).reshape(shape)
            elif "rope_cos" in n:
                x = cos.reshape(shape)
            elif "rope_sin" in n:
                x = sin.reshape(shape)
            elif "mask" in n:
                x = mask.reshape(shape)
            else:
                x = states.get(n, np.zeros(shape, np.float32))
            rdt = np.float32 if args.raw_dtype == "float32" else dt
            feed[n] = np.ascontiguousarray(x, rdt)

        d = os.path.join(args.out, "s%03d" % s)
        os.makedirs(d, exist_ok=True)
        files = []
        for n in in_names:
            fn = os.path.join(d, n + ".raw")
            feed[n].tofile(fn)
            files.append(fn)
        lines.append(" ".join(files))

        gfeed = {k: np.ascontiguousarray(v, spec[k][1]) for k, v in feed.items()}
        outs = sess.run(None, gfeed)
        lg = next(np.asarray(o, np.float32) for n, o in zip(out_names, outs) if "logits" in n)
        prev = int(lg.reshape(-1).argmax())
        for name, o in zip(out_names, outs):
            tgt = carry.get(name)
            if tgt:
                states[tgt] = np.asarray(o, np.float32).reshape(spec[tgt][0])
        print(f"step {s} pos {pos} tok {t} -> next {prev}", flush=True)

    with open(os.path.join(args.out, "input_list.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {len(lines)} samples to {args.out}/input_list.txt")


if __name__ == "__main__":
    main()
