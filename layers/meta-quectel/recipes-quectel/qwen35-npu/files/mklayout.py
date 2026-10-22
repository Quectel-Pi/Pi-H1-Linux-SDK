#!/usr/bin/env python3
"""Build layout.json: ONNX tensor order vs the layout QNN actually declares.

The converter rewrites tensors to its native order, and the rule is *almost*
"move axis 1 to the end" -- but not always, and getting it wrong is silent:
qnn-net-run pairs the Nth file in the input list with the Nth graph tensor and
never looks at names, so a wrong order feeds a [1,1,2048] mask into a [1,1,1024]
input and segfaults.

So the rule is measured, not assumed: read the dimensions the context binary
declares (qnn-context-binary-utility --json_file) and compare them against the
ONNX shapes.  Observed exceptions in this model:

  * layer_XX_conv_state_out stays in ONNX order even though rec_state_out is
    native, so "transpose every output" is wrong;
  * the offloaded KV new-slice is native for k ([1,1,256,2]) and ONNX order for
    v ([1,2,1,256]) because one path ends in a Concat and the other a Reshape.

The quantisation scale/offset of every graph input is recorded too: with
--use_native_input_files qnn-net-run stops converting float32 for you, so a
float input (hidden_in, rope, mask) has to be quantised by the caller as
q = round(x / scale - offset).

usage: mklayout.py <model.onnx> <context.json> [layout.json]
"""
import json
import sys

import onnx


def qnn_name(n):
    # the generated cpp sanitises tensor names: "/Concat_6_output_0" becomes
    # "_Concat_6_output_0" -- the leading slash is replaced, not kept
    return n.replace("/", "_")


def dims(t):
    return [d.dim_value for d in t.type.tensor_type.shape.dim]


def main():
    model_path, ctx_path = sys.argv[1], sys.argv[2]
    out_path = sys.argv[3] if len(sys.argv) > 3 else "layout.json"

    model = onnx.load(model_path, load_external_data=False)
    ctx = json.load(open(ctx_path))["info"]["graphs"]
    if len(ctx) != 1:
        sys.exit("expected one graph, got %d" % len(ctx))
    graph = ctx[0]["info"]

    def table(key):
        out = {}
        for t in graph[key]:
            i = t["info"]
            so = i.get("quantizeParams", {}).get("scaleOffset", {})
            out[i["name"]] = {"dims": i["dimensions"],
                              "dtype": i["dataType"],
                              "scale": so.get("scale"),
                              "offset": so.get("offset")}
        return out

    qin, qout = table("graphInputs"), table("graphOutputs")

    layout = {}
    order = 0
    for t in model.graph.input:
        name = qnn_name(t.name)
        layout[t.name] = {
            "qnn_name": name,
            "onnx": dims(t),
            "qnn_in": qin[name]["dims"] if name in qin else None,
            "qnn_out": None,
            "order": order,
            "qnn_dtype": qin[name]["dtype"] if name in qin else None,
            "scale": qin[name]["scale"] if name in qin else None,
            "offset": qin[name]["offset"] if name in qin else None,
        }
        order += 1
    for t in model.graph.output:
        name = qnn_name(t.name)
        e = layout.setdefault(t.name, {"qnn_name": name, "onnx": dims(t),
                                       "qnn_in": None, "order": None})
        e["qnn_out"] = qout[name]["dims"] if name in qout else None
        e["qnn_dtype"] = qout[name]["dtype"] if name in qout else None
        e["out_dtype"] = qout[name]["dtype"] if name in qout else None
        e["out_scale"] = qout[name]["scale"] if name in qout else None
        e["out_offset"] = qout[name]["offset"] if name in qout else None

    missing = [k for k, v in layout.items()
               if v.get("qnn_in") is None and v.get("qnn_out") is None]
    json.dump(layout, open(out_path, "w"), indent=1)
    print("wrote %s: %d tensors, %d graph inputs, %d unmatched %s"
          % (out_path, len(layout), order, len(missing), missing[:4]))


main()
