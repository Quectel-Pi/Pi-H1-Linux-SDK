#!/bin/sh
# qwen35-model-install.sh -- put the Qwen3.5-0.8B artifacts where the service
# expects them, then restart it.
#
# qwen35-npu ships the runner, not the model: the int8 ONNX graph is 720 MB (or
# 764 MB for the QNN context binary) and the fp16 embedding table is 508 MB, so
# they travel separately (USB stick, scp, the build tree) instead of inside the
# image.  Either model file is accepted; which one the unit loads is decided by
# QWEN35_BACKEND in /usr/lib/systemd/system/qwen35.service.
#
#   qwen35-model-install.sh /run/media/sda1/qwen35
#   qwen35-model-install.sh /tmp            # also checks /tmp/qwen35
#   qwen35-model-install.sh                 # search the usual places
#
# The files are copied, not moved, and the copy is verified before the service
# is touched.  A file that is already correct is left alone.
set -eu

DEST="${QWEN35_DIR:-/var/lib/qwen35}"
TOOLS="${QWEN35_TOOLS:-/usr/share/qwen35-npu/tools}"
# the model file: default is the int8 ONNX the ORT backend runs
MODEL="${QWEN35_MODEL_FILE:-qwen08b_i8.onnx}"
# the batched-prefill graph: optional.  Without it the prompt goes through one
# token at a time (132 ms each instead of 15 ms); with it the runner picks it up
# at start, so a missing file only costs prompt latency.
PFILL=qwen08b_pf64_i8.onnx
EMB=emb08_f16.npy
# sanity floor only: a truncated copy must be caught before the engine tries to
# load it, but the exact sizes depend on the export, so do not pin them
MODEL_MIN=700000000
EMB_MIN=500000000

usage() {
    # the block comment at the top of the file is the help text
    sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

[ "${1:-}" = "-h" ] && usage 0
[ "${1:-}" = "--help" ] && usage 0

srcdirs=""
if [ $# -gt 0 ]; then
    for d in "$@"; do
        srcdirs="$srcdirs $d $d/qwen35"
    done
else
    for d in /run/media/*/* /media/* /mnt/* /tmp /root /home/* \
             /var/lib/qwen35/incoming /usr/share/qwen35-npu/model; do
        srcdirs="$srcdirs $d"
    done
fi

find_one() {
    # $1: file name; prints the first directory that holds it
    for d in $srcdirs; do
        [ -f "$d/$1" ] || continue
        size=$(stat -c %s "$d/$1")
        min=$EMB_MIN
        [ "$1" != "$EMB" ] && min=$MODEL_MIN
        if [ "$size" -lt "$min" ]; then
            echo "qwen35: $d/$1 is only $size bytes, looks truncated" >&2
            continue
        fi
        echo "$d/$1"
        return 0
    done
    return 1
}

mkdir -p "$DEST"
src_model=$(find_one "$MODEL") || {
    echo "qwen35: cannot find $MODEL in:" >&2
    for d in $srcdirs; do echo "  $d" >&2; done
    echo "qwen35: pass the directory as an argument" >&2
    exit 1
}
src_emb=$(find_one "$EMB") || {
    echo "qwen35: cannot find $EMB" >&2
    exit 1
}

copy() {
    src=$1
    dst=$2
    if [ -f "$dst" ] && [ "$(stat -c %s "$dst")" = "$(stat -c %s "$src")" ]; then
        echo "qwen35: $dst already present, skipping"
        return 0
    fi
    echo "qwen35: copying $src -> $dst ($(du -h "$src" | cut -f1))"
    # a partial file must never be mistaken for a good one
    rm -f "$dst.part"
    cp "$src" "$dst.part"
    mv "$dst.part" "$dst"
}

copy "$src_model" "$DEST/$MODEL"
copy "$src_emb" "$DEST/$EMB"

# optional: a firmware build ships it, a hand copy may not have it
if src_pf=$(find_one "$PFILL"); then
    copy "$src_pf" "$DEST/$PFILL"
else
    echo "qwen35: no $PFILL, prompts will be pre-filled one token at a time" >&2
fi

echo "qwen35: installed into $DEST:"
ls -l "$DEST/$MODEL" "$DEST/$EMB" "$DEST/$PFILL" 2>/dev/null

if [ -n "${QWEN35_NO_RESTART:-}" ]; then
    exit 0
fi

if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload 2>/dev/null || true
    systemctl restart qwen35 || {
        echo "qwen35: restart failed, see: journalctl -u qwen35 -n 50" >&2
        exit 1
    }
    i=0
    while [ $i -lt 120 ]; do
        # Probe the server, not /run/qwen35.ok: the marker is root-owned and may
        # be left over from a previous run. The self-test plus the context load
        # take about 20 s.
        if python3 "$TOOLS/qwen35-ready.py" 1 2>/dev/null; then
            echo "qwen35: ready on port ${QWEN35_PORT:-8000}"
            sed -n 's/^quality: /qwen35: accuracy: /p' /run/qwen35.selftest 2>/dev/null || true
            exit 0
        fi
        systemctl is-active --quiet qwen35 || {
            echo "qwen35: unit stopped, see: journalctl -u qwen35 -n 50" >&2
            exit 1
        }
        i=$((i + 1))
        sleep 1
    done
    echo "qwen35: server did not become ready in 120 s" >&2
    exit 1
fi

echo "qwen35: no systemd here, start it by hand:"
echo "  ADSP_LIBRARY_PATH=/usr/lib/rfsa/adsp:/dsp \\"
echo "      /usr/share/qwen35-npu/tools/qwen35-launch.sh"
