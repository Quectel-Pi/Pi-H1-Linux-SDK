#!/bin/sh
# qwen35-run -- thin wrapper around qwen35-run.py, kept so the entry point
# stays a shell command. All the state handling lives in the python file.
#
# usage: qwen35-run <ctx.bin> <work-dir> [steps]
set -eu
CTX=${1:?context binary}
WORK=${2:?work dir with s000/{hidden_in,rope_cos,rope_sin,attn_mask}.raw}
STEPS=${3:-1}
export ADSP_LIBRARY_PATH=${ADSP_LIBRARY_PATH:-/usr/lib/rfsa/adsp:/dsp}
exec python3 "$(dirname "$0")/qwen35-run.py" --ctx "$CTX" --work "$WORK" --steps "$STEPS"
