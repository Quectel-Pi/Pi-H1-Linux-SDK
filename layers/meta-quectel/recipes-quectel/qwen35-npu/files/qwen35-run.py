#!/usr/bin/env python3
"""qwen35-run.py -- drive one decode step at a time through qnn-net-run.

qnn-net-run executes one pass over an input list and cannot feed a graph's
outputs back into its own inputs, so the recurrent state is shuttled by hand
here.  A shell loop would reload the 764 MB context for every token (~0.9 s);
this keeps the bookkeeping in one process and reports the graph time separately
from the harness overhead.

Two measured layout facts this depends on (see mklayout.py):
  * item N of the input list feeds graph input N -- qnn-net-run matches by
    position, never by name, so the list must follow layout.json's "order";
  * state tensors are 16-bit in the graph.  They are carried here as raw
    bytes and written back untouched, and qnn-net-run is told so with
    --use_native_input_files.  Round-tripping them through float32 costs
    ~100 ms/token (measured: 375 -> 276 ms) and is pointless when the
    quantisation encoding is unchanged, which it is because every state
    output feeds the same state input;
  * a QNN input is moveaxis(onnx, 1, -1) and a QNN output is whatever the
    context declares, which is *not* uniformly native: conv_state stays in ONNX
    order, and the offloaded KV slice is native for k but ONNX for v.  When
    qnn_in == qnn_out the bytes are passed straight through instead of being
    transposed twice.

usage:
  qwen35-run.py --ctx ctx.bin --work DIR --steps 16
    DIR/s000/{hidden_in,rope_cos,rope_sin,attn_mask}.raw   (ONNX order, float32)
    later steps read DIR/sNNN if it exists, else reuse s000 (same prompt token)
  Put DIR on /dev/shm: the state alone is 35 MB per step and the harness writes
  it as float32.
"""
import argparse
import json
import os
import subprocess
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
FRESH = ("hidden_in", "rope_cos", "rope_sin", "attn_mask")


def name_is_f32(spec):
    return "FLOAT" in (spec.get("qnn_dtype") or "") or "float" in (spec.get("qnn_dtype") or "")


def native_dims(onnx_dims):
    """move axis 1 to the end -- what the converter does to a graph input."""
    return [onnx_dims[0]] + onnx_dims[2:] + [onnx_dims[1]]


class Runner(object):
    def __init__(self, ctx, work, qnn, backend, out_root):
        self.ctx, self.work, self.qnn, self.backend = ctx, work, qnn, backend
        self.out_root = out_root
        self.layout = json.load(open(os.path.join(HERE, "layout.json")))
        mp = os.path.join(HERE, "kv_manifest.json")
        self.manifest = json.load(open(mp)) if os.path.exists(mp) else {}

        self.ordered = sorted(((k, v) for k, v in self.layout.items()
                               if v.get("order") is not None),
                              key=lambda kv: kv[1]["order"])
        self.kv_of_input = {}
        for layer, kinds in self.manifest.items():
            for kind, s in kinds.items():
                self.kv_of_input[s["past_in"]] = (layer, kind)
        self.kv_of_output = {}
        for layer, kinds in self.manifest.items():
            for kind, s in kinds.items():
                native = native_dims(s["new_dims"])
                self.kv_of_output[s["new_out"]] = ((layer, kind), native)

        self.state = {}     # name -> raw native bytes, for qnn_in == qnn_out
        self.kv = {}        # (layer, kind) -> native float32 cache
        self.graph_ms = 0.0
        self.steps = 0

    # ---------------------------------------------------------------- inputs
    def seed_kv(self):
        for layer, kinds in self.manifest.items():
            for kind, s in kinds.items():
                dt = np.uint16 if "16" in (s.get("past_dtype") or "16") else np.float32
                self.kv[(layer, kind)] = np.zeros(native_dims(s["past_dims"]), dt)

    def fresh_dir(self, step):
        d = os.path.join(self.work, "s%03d" % step)
        return d if os.path.isdir(d) else os.path.join(self.work, "s000")

    def write_inputs(self, step, out):
        files = []
        d = self.fresh_dir(step)
        for name, spec in self.ordered:
            dst = os.path.join(out, "_in_%s.raw" % name.replace("/", "_"))
            if name in FRESH:
                src = os.path.join(d, name + ".raw")
                if not os.path.exists(src):
                    sys.exit("missing %s" % src)
                x = np.fromfile(src, np.float32).reshape(spec["onnx"])
                x = np.moveaxis(x, 1, -1)
                assert list(x.shape) == list(spec["qnn_in"]), (name, x.shape)
                sc, of = spec.get("scale"), spec.get("offset")
                if sc is None:
                    np.ascontiguousarray(x, np.float32).tofile(dst)
                else:
                    np.clip(np.round(x / sc - of), 0, 65535).astype(
                        np.uint16).tofile(dst)
            elif name in self.kv_of_input:
                self._bytes(self.kv[self.kv_of_input[name]], spec).tofile(dst)
            elif name in self.state:
                self._bytes(self.state[name], spec).tofile(dst)
            else:
                np.zeros(spec["qnn_in"], self.dtype_of(spec)).tofile(dst)
            files.append(dst)
        return files

    @staticmethod
    def dtype_of(spec):
        return np.uint16 if "16" in (spec.get("qnn_dtype") or "") else np.float32

    def _bytes(self, arr, spec):
        """Raw native bytes for an input; only float32 needs conversion."""
        if name_is_f32(spec):
            return np.ascontiguousarray(arr, np.float32)
        if arr.dtype != np.uint16:
            arr = np.ascontiguousarray(arr, np.uint16)
        return arr

    # --------------------------------------------------------------- outputs
    def read_outputs(self, res_dir):
        logits = None
        new_kv = {}
        for name, spec in self.layout.items():
            if not spec.get("qnn_out"):
                continue
            f = os.path.join(res_dir, name.replace("/", "_") + ".raw")
            if not os.path.exists(f):
                continue
            if name_is_f32(spec):
                x = np.fromfile(f, np.float32).reshape(spec["qnn_out"])
            else:
                x = np.fromfile(f, np.uint16).reshape(spec["qnn_out"])
            if name == "logits":
                # real = scale * (q + offset)
                sc, of = spec.get("out_scale"), spec.get("out_offset")
                logits = x.ravel().astype(np.float64)
                if sc is not None:
                    logits = sc * (logits + of)
            elif name in self.kv_of_output:
                key, native = self.kv_of_output[name]
                if list(x.shape) != native:
                    x = np.moveaxis(x, 1, -1)      # ONNX order -> native
                assert list(x.shape) == native, (name, x.shape, native)
                new_kv[key] = x
            else:
                # a state is declared in one layout on the way out and possibly
                # another on the way in.  rec_state is symmetric; conv_state is
                # out [1,6144,3] but in [1,3,6144], i.e. moveaxis(out, 1, -1).
                # Copying the integer codes is valid because the encoding is
                # shared; the layout still has to be corrected.
                want = self.layout.get(name.replace("_out", "_in"), {}).get("qnn_in")
                if want is None or list(x.shape) == list(want):
                    self.state[name] = np.ascontiguousarray(x)
                else:
                    y = np.moveaxis(x, 1, -1)
                    assert list(y.shape) == list(want), (name, y.shape, want)
                    self.state[name] = np.ascontiguousarray(y)
        # append the new position and drop the oldest -- exactly the Slice the
        # graph used to perform on a 2 MB tensor
        for key, x in new_kv.items():
            buf = self.kv[key]
            self.kv[key] = np.concatenate([buf[:, 1:], x], axis=1)
        return logits

    # ------------------------------------------------------------------ loop
    def run(self, steps, topk=5):
        self.seed_kv()
        tokens = []
        for step in range(steps):
            out = os.path.join(self.out_root, "s%03d" % step)
            os.makedirs(out, exist_ok=True)
            files = self.write_inputs(step, out)
            lst = os.path.join(out, "input_list.txt")
            with open(lst, "w") as f:
                f.write(" ".join(files) + "\n")
            t0 = time.time()
            r = subprocess.run([self.qnn, "--backend", self.backend,
                                "--retrieve_context", self.ctx,
                                "--input_list", lst, "--output_dir", out],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            dt = (time.time() - t0) * 1000
            self.graph_ms += dt
            self.steps += 1
            if r.returncode != 0:
                sys.stderr.write(r.stdout.decode("utf-8", "replace")[-1500:])
                sys.exit("qnn-net-run failed at step %d (rc=%d)" % (step, r.returncode))
            logits = self.read_outputs(os.path.join(out, "Result_0"))
            if logits is None:
                sys.exit("step %d produced no logits" % step)
            top = np.argsort(-logits)[:topk]
            tokens.append(int(top[0]))
            print("step %2d  %6.0f ms  top1=%-6d %s" %
                  (step, dt, top[0],
                   [(int(i), round(float(logits[i]), 2)) for i in top]))
        print("total %.0f ms over %d steps (%.0f ms/step)"
              % (self.graph_ms, self.steps, self.graph_ms / max(self.steps, 1)))
        return tokens


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ctx", required=True)
    ap.add_argument("--work", required=True)
    ap.add_argument("--steps", type=int, default=1)
    ap.add_argument("--topk", type=int, default=5)
    ap.add_argument("--qnn", default="/usr/bin/qnn-net-run")
    ap.add_argument("--backend", default="/usr/lib/libQnnHtp.so")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    os.environ.setdefault("ADSP_LIBRARY_PATH", "/usr/lib/rfsa/adsp:/dsp")
    out = a.out or os.path.join(a.work, "out")
    Runner(a.ctx, a.work, a.qnn, a.backend, out).run(a.steps, a.topk)


if __name__ == "__main__":
    main()
