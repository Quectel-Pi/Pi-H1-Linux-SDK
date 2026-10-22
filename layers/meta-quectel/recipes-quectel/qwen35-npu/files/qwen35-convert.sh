#!/bin/sh
# qwen35-convert — turn Qwen3.5-0.8B (Qwen3_5ForConditionalGeneration) into a
# QNN HTP v68 context binary, offline, on an x86 host.
#
# Everything here runs on the build host; the board only needs
# /usr/bin/qnn-net-run plus the firmware libQnnHtp.so. See README.md for why the
# current result is a validated NPU execution but not yet a shippable model.
#
# usage: qwen35-convert <model-dir> <work-dir>
set -eu

MODEL=${1:?model dir with config.json/model.safetensors/tokenizer.json}
WORK=${2:?work dir}
QAIRT=${QAIRT:-/opt/qairt/2.45.0.260326}
QENV=${QENV:-$WORK/venv310}
GEN=$QAIRT/bin/x86_64-linux-clang

mkdir -p "$WORK"
cd "$WORK"

# --- 1. host toolchain ------------------------------------------------------
# The converter is python 3.10 bytecode with native modules; numpy must be < 2
# (numpy 2 breaks the Reduce axis reader) and onnx < 1.19 (onnx.version removed).
if [ ! -x "$QENV/bin/python" ]; then
    python3.10 -m venv "$QENV"
    "$QENV/bin/pip" install -q "numpy<2" "onnx==1.17.0" pyyaml protobuf packaging pandas
fi

# --- 2. export the decode graph --------------------------------------------
# Not shipped: the export itself is model-specific (24 layers, 18 gated-DeltaNet,
# 6 full attention, 52 inputs / 49 outputs, states fed back by name). Export it
# with a fixed-shape single-token decode graph whose graph inputs are
#   hidden_in, rope_cos, rope_sin, attn_mask,
#   layer_XX_conv_state_in | layer_XX_rec_state_in | layer_XX_past_k_in/_past_v_in
# and whose graph outputs are the matching *_out tensors plus logits.
[ -f "$WORK/qwen35_decode.onnx" ] || {
    echo "missing $WORK/qwen35_decode.onnx (see README.md, step 2)"; exit 1; }

# --- 3. closed-loop calibration --------------------------------------------
# Zeros for the 48 states make half the activations identically zero and the
# converter then emits scale=0 ("swish_16_opts: Bad range"). The samples must
# come from actually stepping the model. Raws must be float32: the converter
# reads them as float32 no matter what, so fp16 raws become garbage.
PYTHONPATH=$QAIRT/lib/python "$QENV/bin/python" calib_gen.py \
    --onnx "$WORK/qwen35_decode.onnx" --embed "$WORK/emb_f16.npy" \
    --steps 32 --raw_dtype float32 --out calib

# --- 4. quantize + convert --------------------------------------------------
# act 16 / weights 8 / bias 32 per-channel is the only combination HTP v68
# accepts here. 16-bit *weights* are rejected by the skel:
#   <E> QnnBackend_validateOpConfig failed 3110 (FullyConnected)
# and --float_bitwidth 16 builds a FLOAT_16 context that the V68 skel refuses to
# load (error 5005) because V68 has no fp16 arithmetic.
PYTHONPATH=$QAIRT/lib/python "$QENV/bin/python" "$GEN/qnn-onnx-converter" \
    -i "$WORK/qwen35_decode.onnx" -o conv/qwen35.cpp \
    --act_bitwidth 16 --weights_bitwidth 8 --bias_bitwidth 32 \
    --use_per_channel_quantization --input_list calib/input_list.txt

PYTHONPATH=$QAIRT/lib/python "$QENV/bin/python" "$GEN/qnn-model-lib-generator" \
    -c conv/qwen35.cpp -b conv/qwen35.bin -o conv/lib -l qwen35 -t x86_64-linux-clang

# --- 5. HTP context binary --------------------------------------------------
# vtcm_mb must be 2: the board rejects 4 MB with
#   <E> Request feature vtcm size with value 4194304 unsupported
# The graph name must be the real one from the generated cpp ("qwen35" here),
# "*" does not match. Do not add a "devices" block: it makes Transpose
# validation fail (0xc26).
cat > cfg/htp.json <<'JSON'
{"graphs": [{"graph_names": ["qwen35"], "vtcm_mb": 2, "O": 3}],
 "context": {"properties": [{"weight_sharing_enabled": true}]}}
JSON
cat > cfg/backend.json <<JSON
{"backend_extensions": {
  "shared_library_path": "$QAIRT/lib/x86_64-linux-clang/libQnnHtpNetRunExtensions.so",
  "config_file_path": "$WORK/cfg/htp.json"}}
JSON

LD_LIBRARY_PATH=$QAIRT/lib/x86_64-linux-clang "$GEN/qnn-context-binary-generator" \
    --backend "$QAIRT/lib/x86_64-linux-clang/libQnnHtp.so" \
    --model conv/lib/x86_64-linux-clang/libqwen35.so \
    --binary_file qwen35.bin --output_dir ctx --config_file cfg/backend.json

ls -l ctx/qwen35.bin.bin
echo "done: $WORK/ctx/qwen35.bin.bin"
