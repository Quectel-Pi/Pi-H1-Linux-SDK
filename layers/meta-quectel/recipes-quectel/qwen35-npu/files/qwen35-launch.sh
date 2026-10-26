#!/bin/sh
# qwen35-launch.sh -- start the resident Qwen3.5-0.8B server (QNN/HTP on the
# Hexagon NPU of the QCS6490).
#
# This replaces the two wrappers the upstream installer generated
# (`qwen-python` + `launch.sh`) with a single entry point that systemd can run,
# and it materialises the two small shims that runtime.json refers to:
#
#   $LIB_DIR/qwen-python  <- paths["python"]          (python + our environment)
#   $LIB_DIR/engine.sh    <- paths["decoder_engine"]  (exec qwen_text_engine)
#
# Layout (fixed by the Yocto recipes, see PORT_NOTES.md):
#   /usr/share/qwen35/    model assets (embedding, tokenizer, onnx+bin pairs)
#   /usr/lib/qwen35/      this script, qwen_server.py, qwen_text_engine,
#                         runtime.json, the shims, the QNN libraries
#   /var/lib/qwen35/      writable state: scratch/ and logs/
#
# Everything is overridable through the environment so the same script can be
# run by hand out of a build directory:
#   QWEN35_DIR   (default /usr/lib/qwen35)      program root
#   QWEN35_STATE (default /var/lib/qwen35)      scratch/logs root
#   QWEN35_PORT  (default 8000)                 HTTP port
#   QWEN35_PYTHON (default /usr/bin/python3)
#   QWEN35_CPUS  (default 4-7)                  big-core affinity, like upstream
#   QWEN35_QNN_DIR (default $QWEN35_DIR/qnn)     HTP backend/EP libs, host side
#   QWEN35_SKEL_DIR (default /usr/lib/dsp/cdsp)  DSP-visible skel directory
#   QWEN35_RUN_DIR (default /run)                single-instance lock location
#   QWEN35_CDSP_TIMEOUT (default 60)             s to keep retrying the CDSP
#   QWEN35_READY_TIMEOUT (default 120)           s to wait for /v1/models
#   QWEN35_OK (default /run/qwen35.ok)           readiness marker path
#
# Nothing here starts at boot: qwen35.service ships disabled and is started by
# hand or by npu-chat when the user selects qwen3.5.
set -eu

LIB_DIR="${QWEN35_DIR:-/usr/lib/qwen35}"
STATE_DIR="${QWEN35_STATE:-/var/lib/qwen35}"
PORT="${QWEN35_PORT:-8000}"
PYTHON="${QWEN35_PYTHON:-/usr/bin/python3}"
CPUS="${QWEN35_CPUS:-4-7}"
QNN_DIR="${QWEN35_QNN_DIR:-$LIB_DIR/qnn}"
# Where the DSP domain can see libQnnHtpV68Skel.so. On this board image the
# DSP-visible trees are /usr/lib/dsp/cdsp (CDSP) and /usr/lib/dsp/adsp (ADSP);
# /usr/lib/rfsa/adsp is the *host* view. Qwen3.5 HTP runs on the CDSP
# (QNN logs "Effective cdsp_id is: 3").
SKEL_DIR="${QWEN35_SKEL_DIR:-/usr/lib/dsp/cdsp}"
RUNTIME_JSON="$LIB_DIR/runtime.json"
ENGINE="$LIB_DIR/qwen_text_engine"
SHIM_PYTHON="$LIB_DIR/qwen-python"
SHIM_ENGINE="$LIB_DIR/engine.sh"
RUN_DIR="${QWEN35_RUN_DIR:-/run}"
LOCK="${QWEN35_LOCK:-$RUN_DIR/qwen35.lock}"

log() { echo "qwen35: $*" >&2; }
die() { log "$*"; exit 1; }

[ -f "$RUNTIME_JSON" ] || die "missing $RUNTIME_JSON (run qwen35-mkconfig.py at build time)"
[ -x "$ENGINE" ] || die "missing native engine $ENGINE"
[ -x "$PYTHON" ] || die "missing interpreter $PYTHON"
[ -f "$LIB_DIR/qwen_server.py" ] || die "missing $LIB_DIR/qwen_server.py"

mkdir -p "$STATE_DIR/scratch" "$STATE_DIR/logs"

# ---------------------------------------------------------- DSP bring-up ----
# QNN HTP runs on the CDSP and libcdsprpc needs /dev/fastrpc-cdsp, which the
# fastrpc driver creates only after a300000.remoteproc completed its PAS
# handshake.
#
# This script must NOT write /sys/class/remoteproc/*/state.  That node has a
# single owner: qcom-remoteproc-recover.service, and qwen35.service is ordered
# after it (After=/Wants=).  Writing 'start' from here as well put a second
# writer on the same 5 s handshake -- and since it is the *failed* attempt that
# actually resets the DSP, two interleaved attempts never give it a clean reset.
# Measured on this board: one boot logged 21 "powering up a300000.remoteproc"
# attempts from three different writers (recovery, this script, systemd restart)
# and never came up; running the recovery alone brought the DSP up on attempt 4.
#
# So: only wait for the node the recovery is supposed to produce, and fail with
# a message that points at the recovery unit instead of duplicating it.
CDSP_TIMEOUT="${QWEN35_CDSP_TIMEOUT:-120}"

if [ -e /dev/fastrpc-cdsp ]; then
    log "CDSP ready: /dev/fastrpc-cdsp present"
else
    log "waiting up to ${CDSP_TIMEOUT}s for /dev/fastrpc-cdsp (the DSP start belongs to qcom-remoteproc-recover)"
    _started=$(date +%s)
    _deadline=$((_started + CDSP_TIMEOUT))
    while [ ! -e /dev/fastrpc-cdsp ] && [ "$(date +%s)" -lt "$_deadline" ]; do
        sleep 1
    done
    if [ -e /dev/fastrpc-cdsp ]; then
        log "CDSP ready: /dev/fastrpc-cdsp present after $(( $(date +%s) - _started ))s"
    else
        # Do NOT start the server without the node. It used to warn and carry
        # on, which loaded 258 MB of weights, burned ~5 s of CPU and then died
        # inside libcdsprpc with a wall of 'domain_deinit for domain N: dev -1'
        # lines that say nothing about the real cause.
        log "ERROR /dev/fastrpc-cdsp did not appear within ${CDSP_TIMEOUT}s."
        log "ERROR This script does not drive remoteproc any more -- qcom-remoteproc-recover"
        log "ERROR owns the CDSP start, and a second writer races its PAS reset."
        log "ERROR Refusing to start the server -- it would only fail inside libcdsprpc."
        log "ERROR   systemctl status qcom-remoteproc-recover"
        log "ERROR   journalctl -u qcom-remoteproc-recover -b"
        log "ERROR   dmesg | grep a300000        (look for 'start timed out' / -110)"
        log "ERROR   systemctl status cdsprpcd"
        exit 75
    fi
fi


# ---------------------------------------------------------------- shims -----
# qwen_server.py requires paths["python"] and paths["decoder_engine"] to be
# real files; the recipe does not install them because they are three lines of
# shell that depend on the paths above. Writing them here keeps runtime.json
# path-independent and the launch self-contained.
if [ ! -x "$SHIM_PYTHON" ]; then
    cat >"$SHIM_PYTHON" <<'SHIM'
#!/bin/sh
set -eu
export PYTHONDONTWRITEBYTECODE=1
exec "${QWEN35_PYTHON:-/usr/bin/python3}" "$@"
SHIM
    chmod 0755 "$SHIM_PYTHON"
fi
if [ ! -x "$SHIM_ENGINE" ]; then
    cat >"$SHIM_ENGINE" <<'SHIM'
#!/bin/sh
# The whole server tree is already pinned to the big cores by launch.sh; the
# extra taskset only matters when the engine is started by hand. Upstream's
# trailing "arena_disjoint" argument is preserved.
set -eu
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if command -v taskset >/dev/null 2>&1; then
    exec taskset -c "${QWEN35_CPUS:-4-7}" "$DIR/qwen_text_engine" "$@" arena_disjoint
fi
exec "$DIR/qwen_text_engine" "$@" arena_disjoint
SHIM
    chmod 0755 "$SHIM_ENGINE"
fi

# ------------------------------------------------------------ environment ---
# The QNN EP plugin and libQnnHtp*.so come from the onnxruntime-qnn wheels; the
# ORT capi soname alias (libonnxruntime.so.1) lives in $LIB_DIR/lib.
LD_LIBRARY_PATH="$LIB_DIR:$QNN_DIR:$LIB_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH

# ';'-separated DSP search path -- SEMICOLON, never ':'. A ':'-separated value
# is silently treated as one long (nonexistent) directory name: QNN then reports
# "Failed to load skel, error: 1002" followed by "QnnDevice_create 14001".
# Only DSP-visible trees belong here: /usr/lib/rfsa/adsp is the host view and
# must NOT be listed (it used to ship a QAIRT 2.47 skel next to a 2.40 backend,
# which crashes). Order: the dir we pin the skel in first, then /dsp.
ADSP_LIBRARY_PATH="$SKEL_DIR"
for d in /usr/lib/dsp/cdsp /usr/lib/dsp/adsp /dsp; do
    case ";$ADSP_LIBRARY_PATH;" in
        *";$d;"*) ;;
        *) ADSP_LIBRARY_PATH="$ADSP_LIBRARY_PATH;$d" ;;
    esac
done
if [ -n "${QWEN35_ADSP_EXTRA:-}" ]; then
    ADSP_LIBRARY_PATH="$ADSP_LIBRARY_PATH;$QWEN35_ADSP_EXTRA"
fi
export ADSP_LIBRARY_PATH

# ------------------------------------------------- QNN pairing self-check ---
# A QNN backend and its skeleton must come from ONE QAIRT release: a 2.40
# backend loading a 2.47 skel (or vice versa) aborts inside QnnDevice_create.
# The image historically carried both (/usr/lib/rfsa/adsp = 2.47.0,
# /usr/lib/npu-llm = 2.40.1), so report what the DSP will actually load at
# every start. Best effort: QNN libs embed their version as a 4-part string.
skel=""
for cand in "$SKEL_DIR/libQnnHtpV68Skel.so" "$SKEL_DIR/libQnnHtpSkel.so" \
            "$QNN_DIR/libQnnHtpV68Skel.so"; do
    [ -f "$cand" ] && { skel="$cand"; break; }
done
if [ -z "$skel" ]; then
    log "WARNING no libQnnHtpV68Skel.so in $SKEL_DIR -- the DSP will not find the skeleton"
    log "WARNING (the recipes must copy the onnxruntime-qnn wheel's skel there)"
else
    ver() { grep -aoE '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]{9,12}' "$1" 2>/dev/null | head -n1; }
    skel_v=$(ver "$skel")
    back_v=$(ver "$QNN_DIR/libQnnHtp.so")
    log "skel    $skel ${skel_v:+($skel_v)}"
    log "backend $QNN_DIR/libQnnHtp.so ${back_v:+($back_v)}"
    if [ -n "$skel_v" ] && [ -n "$back_v" ] && [ "$skel_v" != "$back_v" ]; then
        log "ERROR QAIRT version mismatch: backend $back_v vs skel $skel_v"
        log "ERROR QnnDevice_create will fail (14001) or crash; fix the package, do not mix wheels"
    fi
    case "$QNN_DIR" in
        /usr/lib/npu-llm*|/usr/lib/rfsa/adsp*)
            log "WARNING backend resolves into $QNN_DIR, a known foreign QAIRT install;"
            log "WARNING qwen35 must use the onnxruntime-qnn wheel's own copy in $LIB_DIR/qnn"
            ;;
    esac
fi

export QWEN35_DIR="$LIB_DIR" QWEN35_STATE="$STATE_DIR" QWEN35_PORT="$PORT"
export PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 TOKENIZERS_PARALLELISM=false
# keep the HF/tokenizers caches off the (read-only) program directory
export HOME="${HOME:-$STATE_DIR}" XDG_CACHE_HOME="${XDG_CACHE_HOME:-$STATE_DIR/.cache}"

# ------------------------------------------------------------------ run -----
# A resident server keeps a few thousand file descriptors open (one per state
# transport stream per in-flight chunk); the default 1024 soft limit is not
# enough.
ulimit -Sn 4096 2>/dev/null || log "could not raise the open-file limit to 4096"

cd "$LIB_DIR"

# --------------------------------------------------------- readiness mark ---
# /run/qwen35.ok is the cheap "serving, not merely spawned" signal npu-chat
# reads (QWEN35_OK). The NPU upload takes ~4 s after exec, so a child polls
# /v1/models and writes the marker on the first 200; the unit deletes it in
# ExecStopPost. A marker left by a previous run is dropped here first.
OK_MARKER="${QWEN35_OK:-/run/qwen35.ok}"
rm -f "$OK_MARKER" 2>/dev/null || true
(
    _d=$(( $(date +%s) + ${QWEN35_READY_TIMEOUT:-120} ))
    while [ "$(date +%s)" -lt "$_d" ]; do
        if "$PYTHON" -c 'import sys, urllib.request
urllib.request.urlopen("http://127.0.0.1:%s/v1/models" % sys.argv[1], timeout=2).read()' \
                "$PORT" >/dev/null 2>&1; then
            echo "ready port=$PORT at=$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
                >"$OK_MARKER" 2>/dev/null || exit 0
            exit 0
        fi
        sleep 1
    done
    log "WARNING $OK_MARKER not written: /v1/models did not answer within ${QWEN35_READY_TIMEOUT:-120}s"
) &

if command -v taskset >/dev/null 2>&1; then
    PIN="taskset -c $CPUS"
else
    log "taskset not found; running unpinned"
    PIN=""
fi

log "starting server: $LIB_DIR/qwen_server.py --config $RUNTIME_JSON (port $PORT, cpus $CPUS)"
log "ADSP_LIBRARY_PATH=$ADSP_LIBRARY_PATH"
# -n: fail instead of queueing a second instance; the systemd unit restarts.
# /run is writable for root (RuntimeDirectory=qwen35 in the unit); fall back to
# /tmp when the script is run by an unprivileged user.
if ! ( : >>"$LOCK" ) 2>/dev/null; then
    LOCK="/tmp/qwen35-$(id -u).lock"
    log "cannot write $QWEN35_RUN_DIR/qwen35.lock, using $LOCK"
fi

# shellcheck disable=SC2086
exec flock -n "$LOCK" $PIN "$SHIM_PYTHON" -B "$LIB_DIR/qwen_server.py" --config "$RUNTIME_JSON"
