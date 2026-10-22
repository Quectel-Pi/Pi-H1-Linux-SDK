#!/usr/bin/env python3
"""Move the KV ring buffer out of the QNN graph.

The exported decode graph updates the KV cache with

    past_k_out = Cast(Slice(Concat(past_k_in, new_k), start=1))

which is semantically a one-position shift.  On the HTP that shift is the single
most expensive thing in the graph: the KV tensor is 2 MB for Qwen3.5-0.8B
(2047 x 256 x 2, 16-bit) and VTCM on this SoC is 2 MB, so the Concat and the
Slice each become a tiled DDR round trip.  Measured on a 12-layer half-graph
(254 MB of weights):

    outputs = residual only                 62 ms
    + 6 KV ring buffers (12 MB written)    +449 ms
    + 18 DeltaNet states (5 MB written)    +198 ms

So 72% of the runtime is one memmove that the CPU can do in ~2 ms.

This script rewrites the graph to output the *new slice* (1 KB per tensor) and
feeds the unchanged cache back in.  The caller keeps the cache:

    buf = np.concatenate([buf[1:], new_slice], axis=pos_axis)

End to end on the board (24 layers, 764 MB context, 2048 context length):

    original, float32 outputs              1364 ms/token
    original, --use_native_output_files     801 ms/token
    KV offloaded, float32 outputs           532 ms/token
    KV offloaded, native outputs            375 ms/token

usage: offload_kv.py <in.onnx> <out.onnx> [manifest.json]
"""
import json
import sys

import onnx
from onnx.utils import extract_model


def main():
    src, dst = sys.argv[1], sys.argv[2]
    manifest_path = sys.argv[3] if len(sys.argv) > 3 else "kv_manifest.json"

    model = onnx.load(src, load_external_data=False)
    graph = model.graph
    producer = {}
    for node in graph.node:
        for out in node.output:
            producer[out] = node

    inputs = [t.name for t in graph.input]
    outputs = [t.name for t in graph.output]
    kv_out = [n for n in outputs if "past_k_out" in n or "past_v_out" in n]
    if not kv_out:
        sys.exit("no past_k_out/past_v_out in %s" % src)

    replaced = {}
    manifest = {}
    for name in kv_out:
        cast = producer[name]                       # Cast
        sl = producer[cast.input[0]]                # Slice
        cc = producer[sl.input[0]]                  # Concat(past_*_in, new)
        if cast.op_type != "Cast" or sl.op_type != "Slice" or cc.op_type != "Concat":
            sys.exit("unexpected chain for %s: %s/%s/%s"
                     % (name, cast.op_type, sl.op_type, cc.op_type))
        fresh = [i for i in cc.input if "past_" not in i]
        if len(fresh) != 1:
            sys.exit("cannot identify the new slice for %s: %s" % (name, cc.input))
        past = [i for i in cc.input if "past_" in i][0]
        replaced[name] = fresh[0]
        # layer_03_past_k_out -> layer_03, past_k_in / new_k
        layer = name.split("_past_")[0]
        kind = "k" if "past_k" in name else "v"
        manifest.setdefault(layer, {})[kind] = {
            "past_in": past,
            "new_out": fresh[0],
        }

    outs = [o for o in outputs if o not in kv_out] + [replaced[o] for o in kv_out]
    extract_model(src, dst, inputs, outs, check_model=False)

    # The manifest describes what the caller has to reimplement.  Shapes are
    # taken from the ONNX side; the QNN side needs mklayout.py, because the
    # native layout differs per tensor (the k slice comes out native
    # [1,1,256,2], the v slice keeps ONNX order [1,2,1,256]).
    out = onnx.load(dst, load_external_data=False)
    vimap = {t.name: [d.dim_value for d in t.type.tensor_type.shape.dim]
             for t in list(out.graph.input) + list(out.graph.output)}
    for layer, kinds in manifest.items():
        for kind, spec in kinds.items():
            spec["past_dims"] = vimap[spec["past_in"]]
            spec["new_dims"] = vimap[spec["new_out"]]
    json.dump(manifest, open(manifest_path, "w"), indent=1)

    print("kv tensors offloaded: %d" % len(kv_out))
    print("wrote %s (in %d, out %d) and %s"
          % (dst, len(out.graph.input), len(out.graph.output), manifest_path))


main()
