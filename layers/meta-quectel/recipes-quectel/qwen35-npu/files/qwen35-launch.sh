#!/bin/sh
# qwen35-launch.sh -- start the resident Qwen3.5-0.8B server.
#
# Two backends, selected with QWEN35_BACKEND (ort is the default):
#
#   ort   qwen08b_i8.onnx -- int8 weights, fp32 activations, on the CPU.
#         Accurate (logits cosine 0.9925 against fp32) and 7.2 tok/s.
#   htp   kvc.bin -- the W8A16 QNN context binary on the Hexagon DSP.
#         5.6 tok/s, but 16-bit per-tensor activations over the whole graph
#         only reach cosine 0.9688, which is not enough to keep the top-1
#         token: the text comes out as noise.  Kept for measurement only.
#
# Both need emb08_f16.npy (the fp16 token embedding table) in QWEN35_DIR
# (default /var/lib/qwen35).
#
# The marker /run/qwen35.ok is written only after a self-test passes, because
# npu-chat uses it to decide whether to offer this model.
set -eu

DIR="${QWEN35_DIR:-/var/lib/qwen35}"
TOOLS="${QWEN35_TOOLS:-/usr/share/qwen35-npu/tools}"
PORT="${QWEN35_PORT:-8000}"
HOST="${QWEN35_HOST:-127.0.0.1}"
MARKER="${QWEN35_OK:-/run/qwen35.ok}"
LOG="${QWEN35_LOG:-/var/log/qwen35.log}"
BACKEND="${QWEN35_BACKEND:-ort}"
THREADS="${QWEN35_THREADS:-4}"

case "$BACKEND" in
    ort) MODEL="$DIR/qwen08b_i8.onnx" ; MODEL_OPT=--onnx ; NEED="qwen08b_i8.onnx" ;;
    htp) MODEL="$DIR/kvc.bin" ; MODEL_OPT=--ctx ; NEED="kvc.bin" ;;
    *) echo "qwen35: unknown QWEN35_BACKEND=$BACKEND (ort|htp)" >&2 ; exit 1 ;;
esac

# Only the DSP path needs the ADSP search path; exporting it is harmless.
export ADSP_LIBRARY_PATH="${ADSP_LIBRARY_PATH:-/usr/lib/rfsa/adsp:/dsp}"
# qwen35-ready.py reads these
export QWEN35_HOST="$HOST" QWEN35_PORT="$PORT"
cd "$TOOLS"

# the model directory is not packaged (it holds 1.2 GB of artifacts); create it
# so the error below is about the files, not about a missing directory
mkdir -p "$DIR" 2>/dev/null || true

if [ ! -f "$MODEL" ] || [ ! -f "$DIR/emb08_f16.npy" ]; then
    echo "qwen35: model artifacts missing in $DIR" >&2
    echo "qwen35: backend $BACKEND needs $NEED and emb08_f16.npy" >&2
    echo "qwen35: (see $TOOLS/README.md)" >&2
    exit 1
fi

SERVE="python3 qwen35-serve.py --backend $BACKEND --threads $THREADS $MODEL_OPT $MODEL --emb $DIR/emb08_f16.npy --tok $TOOLS/tokenizer.json.gz"

# a marker left over from a previous boot or backend must not advertise a server
# that is not up yet
rm -f "$MARKER" 2>/dev/null || true

# 1. self-test on a throw-away server instance
if $SERVE --selftest >"/run/qwen35.selftest" 2>&1; then
    :
else
    echo "qwen35: self-test failed" >&2
    cat "/run/qwen35.selftest" >&2 || true
    rm -f "$MARKER"
    exit 1
fi
grep -q '^mechanical: ok' /run/qwen35.selftest || {
    echo "qwen35: mechanical self-test did not pass" >&2
    cat /run/qwen35.selftest >&2
    rm -f "$MARKER"
    exit 1
}

# 2. record the accuracy verdict instead of hiding it
sed -n 's/^quality: //p' /run/qwen35.selftest > /run/qwen35.quality || true
[ -s /run/qwen35.quality ] || echo "not verified" > /run/qwen35.quality

# 3. run the server; the marker appears once it is accepting requests
$SERVE --host "$HOST" --port "$PORT" &
pid=$!
trap 'kill $pid 2>/dev/null || true' INT TERM

i=0
while [ $i -lt 120 ]; do
    # probe over HTTP: the server owns the socket, so this is the only honest
    # readiness signal (a stale /run/qwen35.ok from a previous boot would lie)
    if python3 "$TOOLS/qwen35-ready.py" 1 2>/dev/null; then
        date > "$MARKER"
        break
    fi
    kill -0 $pid 2>/dev/null || { echo "qwen35: server exited" >&2; exit 1; }
    i=$((i + 1))
    sleep 1
done

wait $pid
