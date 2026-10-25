#!/bin/sh
# =============================================================================
# qwen35-verify.sh -- board-side acceptance check for the Qwen3.5-0.8B NPU
# server on the Quectel PI H1 (QCS6490 / Hexagon HTP v68).
#
# Run it ON the board as root:
#
#     /usr/lib/qwen35/qwen35-verify.sh            # full run, ~2-3 min
#     /usr/lib/qwen35/qwen35-verify.sh --quick    # checks 1-7 only (no chat/perf)
#
# Output: one [PASS] / [FAIL] / [WARN] line per check plus an evidence line.
# Exit code: 0 when no check FAILed, 1 when at least one did.  It never stops
# early (set -u, no set -e): a broken check 3 must not hide the state of 11.
#
# Why the checks are shaped this way -- each one is a board-side failure we already hit:
#
#   * ADSP_LIBRARY_PATH is SEMICOLON separated.  With ':' QNN treats the whole
#     string as ONE directory name, then reports "Failed to load skel, error:
#     1002" followed by "QnnDevice_create ... 14001" -- an error that reads
#     like a memory allocation failure and costs an afternoon.
#   * The QNN backend (libQnnHtp.so) and the DSP skeleton (libQnnHtpV68Skel.so)
#     must come from ONE QAIRT release.  This image historically shipped
#     QAIRT 2.47.0 in /usr/lib/rfsa/adsp next to a 2.40.1 backend in
#     /usr/lib/npu-llm; mixing them aborts inside QnnDevice_create.
#   * The skeleton must be in a directory the CDSP can see: /usr/lib/dsp/cdsp.
#     /usr/lib/rfsa/adsp is the *host* view of the DSP filesystem, not the
#     DSP's view, so a skel that is only there is invisible to the HTP.
#   * /run/qwen35.ok is NOT written by this design any more (CLEANUP.md): the
#     readiness signal is GET /health.  A missing marker is therefore a WARN,
#     not a FAIL -- but it is still checked because npu-chat's launcher reads it.
#
# Nothing here is hardcoded to a board address: the server is contacted at
# http://127.0.0.1:8000 (override with QWEN35_VERIFY_URL if you really need to).
# =============================================================================
set -u

BASE_URL="${QWEN35_VERIFY_URL:-http://127.0.0.1:8000}"
ASSET_DIR="${QWEN35_ASSET_DIR:-/usr/share/qwen35}"
LIB_DIR="${QWEN35_DIR:-/usr/lib/qwen35}"
STATE_DIR="${QWEN35_STATE:-/var/lib/qwen35}"
SKEL_DIR="${QWEN35_SKEL_DIR:-/usr/lib/dsp/cdsp}"
QNN_DIR="$LIB_DIR/qnn"
UNIT="${QWEN35_UNIT:-qwen35}"
QUICK=0

# Radxa Dragon Q6A reference for the same SoC and the same 2K release.
RADXA_DECODE_TPS=7.4

usage() {
	cat <<EOF
usage: $0 [--quick]

  (no option)  run every check: assets, QNN pairing, DSP skel, ADSP path,
               service state, endpoints, a real 17*23 round trip, 3x decode
               throughput, memory/temperature, crash evidence.
  --quick      run checks 1-7 only.  No inference, no performance run:
               finishes in a few seconds, usable in a boot smoke test.
  -h, --help   this text.

Environment overrides: QWEN35_VERIFY_URL, QWEN35_ASSET_DIR, QWEN35_DIR,
QWEN35_STATE, QWEN35_SKEL_DIR, QWEN35_UNIT.
EOF
}

for arg in "$@"; do
	case "$arg" in
	--quick | -q) QUICK=1 ;;
	--full) QUICK=0 ;;
	-h | --help) usage; exit 0 ;;
	*)
		printf 'unknown argument: %s\n\n' "$arg" >&2
		usage >&2
		exit 2
		;;
	esac
done

WORK=$(mktemp -d "${TMPDIR:-/tmp}/qwen35-verify.XXXXXX") || {
	printf 'cannot create a temporary directory\n' >&2
	exit 2
}
# shellcheck disable=SC2064  # $WORK must be expanded now, not at trap time
trap "rm -rf '$WORK'" EXIT INT TERM HUP

PASS_N=0
FAIL_N=0
WARN_N=0

ok() { PASS_N=$((PASS_N + 1)); printf '[PASS] %s\n' "$*"; }
bad() { FAIL_N=$((FAIL_N + 1)); printf '[FAIL] %s\n' "$*"; }
wrn() { WARN_N=$((WARN_N + 1)); printf '[WARN] %s\n' "$*"; }
info() { printf '       %s\n' "$*"; }
section() { printf '\n== %s ==\n' "$*"; }

# float compare: ge A B  -> true when A >= B
ge() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a + 0 >= b + 0) }'; }
# bytes -> "3.01 GiB"
hbytes() {
	awk -v b="${1:-0}" 'BEGIN {
		split("B KiB MiB GiB TiB", u, " "); i = 1; v = b + 0
		while (v >= 1024 && i < 5) { v /= 1024; i++ }
		printf "%.2f %s", v, u[i]
	}'
}
is_uint() { case "${1:-}" in '' | *[!0-9]*) return 1 ;; *) return 0 ;; esac; }

# --- HTTP -------------------------------------------------------------------
# http <method> <path> <outfile> <max-time> [request-body-file]
# prints the HTTP status code ("000" when curl could not even connect).
http() {
	_method=$1 _path=$2 _out=$3 _tmo=$4 _data=${5:-}
	if [ -n "$_data" ]; then
		_code=$(curl -sS --max-time "$_tmo" -o "$_out" -w '%{http_code}' \
			-X "$_method" -H 'Content-Type: application/json' \
			--data-binary "@$_data" "$BASE_URL$_path" 2>"$WORK/curl.err")
	else
		_code=$(curl -sS --max-time "$_tmo" -o "$_out" -w '%{http_code}' \
			-X "$_method" "$BASE_URL$_path" 2>"$WORK/curl.err")
	fi
	[ -n "$_code" ] || _code=000
	printf '%s' "$_code"
}

# --- QAIRT version ----------------------------------------------------------
# QNN libraries embed their release as a 4-part string with a long build id in
# the last field, e.g. 2.40.1.250526103500.  'strings' alone is 4 GB of reads on
# this image; grep -a stops at the first match and head -n1 closes the pipe.
qairt_version() {
	[ -f "$1" ] || return 1
	_v=$(grep -aoE '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]{9,12}' "$1" 2>/dev/null | head -n 1)
	if [ -z "$_v" ] && command -v strings >/dev/null 2>&1; then
		_v=$(strings "$1" 2>/dev/null |
			grep -oE '[0-9]+\.[0-9]+\.[0-9]+\.[0-9]{9,12}' | head -n 1)
	fi
	[ -n "$_v" ] || return 1
	printf '%s' "$_v"
}

check_file() { # <path> <expected size> <label> <required|optional>
	if [ ! -f "$1" ]; then
		if [ "$4" = optional ]; then
			wrn "$3: $1 is missing (optional group not installed)"
		else
			bad "$3: $1 is missing"
		fi
		return
	fi
	_got=$(wc -c <"$1" | tr -d ' \t')
	if [ "$_got" = "$2" ]; then
		ok "$3: $1 ($(hbytes "$_got"))"
	else
		bad "$3: $1 is $_got B, expected $2 B ($(hbytes "$_got") vs $(hbytes "$2"))"
	fi
}

# =============================================================================
printf 'qwen35 verification -- %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
printf 'board      %s %s\n' "$(uname -m)" "$(uname -r)"
if [ -r /etc/os-release ]; then
	# shellcheck disable=SC1091
	. /etc/os-release
	printf 'os         %s\n' "${PRETTY_NAME:-unknown}"
fi
printf 'uptime     %s\n' "$(uptime | sed 's/^[[:space:]]*//')"
printf 'server     %s (unit %s)\n' "$BASE_URL" "$UNIT"
[ "$QUICK" -eq 1 ] && printf 'mode       --quick (checks 1-7 only)\n'
[ "$(id -u)" -eq 0 ] || wrn "not running as root: dmesg, /proc/<pid>/environ and
       journalctl checks degrade to WARN"

# -----------------------------------------------------------------------------
section "1. asset and program integrity"

# Sizes are the byte counts of the pinned HuggingFace release
# PrismPhi/Qwen3.5-0.8B-Radxa-Dragon-Q6A-QCS6490-QNN-NPU @ 05de67d2, i.e. what
# qwen35-npu-model.bb installs.  A wrong size means a truncated download or a
# stale copy, and the .onnx + _qnn.bin pair must match exactly.
while IFS='|' read -r _rel _size _label _req; do
	check_file "$ASSET_DIR/$_rel" "$_size" "$_label" "$_req"
done <<'ASSETS'
common/embedding_fp32.npy|1017118848|2k embedding_fp32.npy|required
common/tokenizer.json|12807982|2k tokenizer.json|required
models/2k/decode_ctx.onnx|222469|2k decode_ctx.onnx|required
models/2k/decode_ctx_qnn.bin|846712832|2k decode_ctx_qnn.bin|required
models/2k/prefill_ctx.onnx|20538|2k prefill_ctx.onnx|required
models/2k/assembly_report.json|16755|2k assembly_report.json|required
contracts/2k/rope_ctx2048_cos_sin_fp32.bin|1048576|2k rope cos/sin table|required
contracts/2k/tensor_contract.tsv|139720|2k tensor_contract.tsv|required
contracts/2k/advance_plan.tsv|169904|2k advance_plan.tsv|required
ASSETS

# The vision group (3 onnx+bin pairs, 167 MB) is skipped by QWEN35_VISION_IN_IMAGE=0.
# Count what is there first: all-absent is a configuration choice, half-present
# is a broken install (qwen_server.py validates the three .onnx paths at start).
_vision=0
for _f in models/vision/patch_ctx.onnx models/vision/patch_ctx_qnn.bin \
	models/vision/body_ctx.onnx models/vision/body_ctx_qnn.bin \
	models/vision/merger_ctx.onnx models/vision/merger_ctx_qnn.bin; do
	[ -f "$ASSET_DIR/$_f" ] && _vision=$((_vision + 1))
done
if [ "$_vision" -eq 0 ]; then
	wrn "vision group absent (QWEN35_VISION_IN_IMAGE=0): fine for text, but
       runtime/qwen_server.py validates vision_patch/body/merger .onnx at startup,
       so the unit only starts with that patch; 6 files / 167 MB if you need them"
else
	_req=required
	[ "$_vision" -eq 6 ] || info "vision group partially present ($_vision/6)"
	while IFS='|' read -r _rel _size _label; do
		check_file "$ASSET_DIR/$_rel" "$_size" "$_label" "$_req"
	done <<'VISION'
models/vision/patch_ctx.onnx|822|vision patch_ctx.onnx
models/vision/patch_ctx_qnn.bin|1712128|vision patch_ctx_qnn.bin
models/vision/body_ctx.onnx|823|vision body_ctx.onnx
models/vision/body_ctx_qnn.bin|159666176|vision body_ctx_qnn.bin
models/vision/merger_ctx.onnx|833|vision merger_ctx.onnx
models/vision/merger_ctx_qnn.bin|13000704|vision merger_ctx_qnn.bin
VISION
fi

# --- the three program-side files the brief calls out -----------------------
# qwen_server.py has no fixed size (it grows with the runtime), so assert the
# properties that actually matter: it is a plausible size, it parses, and it is
# the file that serves the API we are about to test.
if [ ! -f "$LIB_DIR/qwen_server.py" ]; then
	bad "runtime: $LIB_DIR/qwen_server.py is missing"
else
	_srv_bytes=$(wc -c <"$LIB_DIR/qwen_server.py" | tr -d ' ')
	_srv_py=$(ls "$LIB_DIR"/*.py 2>/dev/null | wc -l | tr -d ' ')
	if ! ge "$_srv_bytes" 20000; then
		bad "runtime: $LIB_DIR/qwen_server.py is only ${_srv_bytes} bytes -- truncated package"
	elif ! grep -q 'qwen35_metrics' "$LIB_DIR/qwen_server.py" 2>/dev/null; then
		bad "runtime: $LIB_DIR/qwen_server.py has no 'qwen35_metrics' key -- wrong or truncated build"
	else
		ok "runtime: $LIB_DIR/qwen_server.py ($(hbytes "$_srv_bytes"), $_srv_py python modules beside it)"
	fi
	if command -v python3 >/dev/null 2>&1; then
		# ast.parse only: py_compile would drop a __pycache__ into /usr/lib.
		if python3 -c 'import ast,sys; ast.parse(open(sys.argv[1], encoding="utf-8").read())' "$LIB_DIR/qwen_server.py" >/dev/null 2>&1; then
			ok "runtime: qwen_server.py compiles (no syntax error from a partial install)"
		else
			bad "runtime: qwen_server.py does not compile -- python3 -m py_compile $LIB_DIR/qwen_server.py"
		fi
	fi
fi

if [ ! -f "$LIB_DIR/qwen_text_engine" ]; then
	bad "engine: $LIB_DIR/qwen_text_engine is missing"
else
	_elf=$(head -c 4 "$LIB_DIR/qwen_text_engine" 2>/dev/null | od -An -c | tr -d ' \n')
	if [ ! -x "$LIB_DIR/qwen_text_engine" ]; then
		bad "engine: $LIB_DIR/qwen_text_engine is not executable"
	elif [ "$_elf" = '177ELF' ] || [ "$(head -c 4 "$LIB_DIR/qwen_text_engine" | od -An -tx1 | tr -d ' \n')" = '7f454c46' ]; then
		ok "engine: $LIB_DIR/qwen_text_engine is an ELF executable ($(hbytes "$(wc -c <"$LIB_DIR/qwen_text_engine" | tr -d ' ')"))"
	else
		bad "engine: $LIB_DIR/qwen_text_engine is not an ELF binary"
	fi
fi

for _f in "$LIB_DIR/qwen35-launch.sh" "$LIB_DIR/engine.sh" "$LIB_DIR/qwen-python"; do
	if [ -x "$_f" ]; then
		ok "launcher: $_f"
	elif [ -f "$_f" ]; then
		bad "launcher: $_f exists but is not executable"
	else
		# engine.sh / qwen-python are written by qwen35-launch.sh on first start
		case "$_f" in
		*launch.sh) bad "launcher: $_f is missing" ;;
		*) wrn "launcher: $_f not created yet (qwen35-launch.sh writes it on first start)" ;;
		esac
	fi
done

if [ ! -f "$LIB_DIR/runtime.json" ]; then
	bad "config: $LIB_DIR/runtime.json is missing (generated at build time by qwen35-mkconfig.py)"
else
	_rc=0
	python3 - "$LIB_DIR/runtime.json" >"$WORK/rt.txt" 2>"$WORK/rt.err" <<'PY' || _rc=$?
import json, os, sys

path = sys.argv[1]
try:
    cfg = json.load(open(path, encoding="utf-8"))
except Exception as exc:                                   # noqa: BLE001
    print("unreadable: %s" % exc)
    raise SystemExit(3)

paths = cfg.get("paths") or {}
if not isinstance(paths, dict) or not paths:
    print("no paths block")
    raise SystemExit(3)

print("model_id=%s" % cfg.get("model_id", "?"))
print("port=%s" % cfg.get("port", "?"))
for key in ("qnn_plugin", "qnn_backend", "scratch_root", "metrics_log"):
    if not paths.get(key):
        print("missing-key=%s" % key)

# Same contract qwen_server.py::check_files enforces, plus the state dir which
# is created at runtime (StateDirectory=/launch.sh) and so is not required here.
missing = []
for key, value in sorted(paths.items()):
    if key in ("scratch_root", "metrics_log"):
        continue
    if not isinstance(value, str) or not os.path.isfile(value):
        missing.append("%s=%s" % (key, value))
for item in missing:
    print("missing-path=%s" % item)
raise SystemExit(1 if missing else 0)
PY
	case "$_rc" in
	0) ok "config: runtime.json resolves ($(sed -n 's/^model_id=//p' "$WORK/rt.txt" | head -n1), port $(sed -n 's/^port=//p' "$WORK/rt.txt" | head -n1))" ;;
	3) bad "config: runtime.json is unusable -- $(head -n1 "$WORK/rt.err" "$WORK/rt.txt" 2>/dev/null | tr '\n' ' ')" ;;
	*) bad "config: runtime.json has paths that do not exist"
	   grep '^missing-path=' "$WORK/rt.txt" | sed 's/^missing-path=/       missing /' ;;
	esac
	if grep -q '^missing-key=' "$WORK/rt.txt" 2>/dev/null; then
		bad "config: runtime.json lacks a required key"
		grep '^missing-key=' "$WORK/rt.txt" | sed 's/^missing-key=/       missing /'
	fi
	if grep -q '"/usr/lib/npu-llm\|"/usr/lib/rfsa/adsp' "$LIB_DIR/runtime.json" 2>/dev/null; then
		bad "config: runtime.json points into a foreign QAIRT install (/usr/lib/npu-llm or /usr/lib/rfsa/adsp)"
	fi
fi

# -----------------------------------------------------------------------------
section "2. QNN backend / skel version pairing"

if [ ! -e "$QNN_DIR/libQnnHtp.so" ]; then
	bad "backend: $QNN_DIR/libQnnHtp.so is missing (the HTP implementation lives in the onnxruntime-qnn wheel)"
	_backend_v=""
else
	_backend_v=$(qairt_version "$QNN_DIR/libQnnHtp.so") || _backend_v=""
	if [ -n "$_backend_v" ]; then
		ok "backend: $QNN_DIR/libQnnHtp.so QAIRT $_backend_v ($(hbytes "$(wc -c <"$QNN_DIR/libQnnHtp.so" | tr -d ' ')"))"
	else
		wrn "backend: $QNN_DIR/libQnnHtp.so has no readable QAIRT version string"
	fi
fi

_skel=""
for _cand in "$SKEL_DIR/libQnnHtpV68Skel.so" "$QNN_DIR/libQnnHtpV68Skel.so" \
	/usr/lib/rfsa/adsp/libQnnHtpV68Skel.so; do
	[ -f "$_cand" ] && { _skel="$_cand"; break; }
done
if [ -z "$_skel" ]; then
	bad "skel: no libQnnHtpV68Skel.so found (looked in $SKEL_DIR, $QNN_DIR, /usr/lib/rfsa/adsp)"
	_skel_v=""
else
	_skel_v=$(qairt_version "$_skel") || _skel_v=""
	ok "skel: $_skel QAIRT ${_skel_v:-<unknown>} ($(hbytes "$(wc -c <"$_skel" | tr -d ' ')"))"
fi

if [ -n "${_backend_v:-}" ] && [ -n "${_skel_v:-}" ]; then
	if [ "$_backend_v" = "$_skel_v" ]; then
		ok "pairing: backend and skel are the same QAIRT release ($_backend_v)"
	else
		bad "pairing: backend $_backend_v vs skel $_skel_v -- QnnDevice_create will fail (14001) or abort; install both from one wheel"
	fi
else
	wrn "pairing: cannot compare, one of the two version strings is unreadable"
fi

for _f in libonnxruntime_providers_qnn.so libQnnSystem.so libQnnHtpV68Stub.so; do
	if [ -e "$QNN_DIR/$_f" ]; then
		ok "qnn dir: $_f present"
	else
		bad "qnn dir: $_f is missing from $QNN_DIR"
	fi
done
if [ -e "$LIB_DIR/lib/libonnxruntime.so.1" ] || [ -e "$LIB_DIR/lib/libonnxruntime.so" ]; then
	ok "ort: libonnxruntime.so.1 alias present in $LIB_DIR/lib"
else
	bad "ort: $LIB_DIR/lib has no libonnxruntime.so.1 (NEEDED of qwen_text_engine, RUNPATH is \$ORIGIN/lib:\$ORIGIN)"
fi

# ------------------------------------------------------------ NPU only ------
# THE RULE THIS PACKAGE EXISTS FOR: no model arithmetic may run on the CPU.
# Two independent ways it can be broken, both checked here:
#   a) a graph keeps ONNX nodes beside the pre-compiled partition, so ORT hands
#      them to the CPU EP;
#   b) the runtime allows a fallback (runtime.json / engine flag), so a failing
#      QNN node is silently recomputed on the CPU.
# (a) is structural and checkable without inference: every graph shipped is an
# EPContext model, i.e. ALL of its nodes must be domain "com.microsoft",
# op_type "EPContext".  The CPU EP has no kernel for that op, so a graph made
# only of EPContext nodes cannot execute anywhere but the HTP.
if command -v python3 >/dev/null 2>&1; then
	python3 - "$ASSET_DIR" "$WORK/npu-only.txt" <<'PY' >"$WORK/npu-only.log" 2>&1
import sys, glob, os
root, out = sys.argv[1], sys.argv[2]
def varint(b, i):
    r = sh = 0
    while True:
        x = b[i]; i += 1
        r |= (x & 0x7f) << sh; sh += 7
        if not x & 0x80:
            return r, i
def fields(b):
    i = 0
    while i < len(b):
        k, i = varint(b, i)
        f, wt = k >> 3, k & 7
        if wt == 0:
            v, i = varint(b, i); yield f, v
        elif wt == 2:
            n, i = varint(b, i); yield f, b[i:i+n]; i += n
        elif wt == 5:
            yield f, b[i:i+4]; i += 4
        elif wt == 1:
            yield f, b[i:i+8]; i += 8
        else:
            raise ValueError("wire type %d" % wt)
def strs(blob, f):
    return [v.decode("utf-8", "replace") for ff, v in fields(blob) if ff == f]
lines, bad_total = [], 0
for path in sorted(glob.glob(os.path.join(root, "models", "*", "*.onnx"))):
    try:
        data = open(path, "rb").read()
        graph = next(v for f, v in fields(data) if f == 7)
        nodes = [v for f, v in fields(graph) if f == 1]
    except Exception as exc:
        lines.append("UNREADABLE %s (%s)" % (path, exc)); bad_total += 1; continue
    offenders = []
    for n in nodes:
        ot = (strs(n, 4) or [""])[0]
        dom = (strs(n, 7) or [""])[0]
        if ot != "EPContext" or dom != "com.microsoft":
            offenders.append("%s[%s]" % (dom or "-", ot or "-"))
    rel = os.path.relpath(path, root)
    lines.append("%-34s nodes=%-3d epcontext=%-3d cpu_side=%d %s"
                 % (rel, len(nodes), len(nodes) - len(offenders), len(offenders),
                    " ".join(offenders[:6])))
    bad_total += len(offenders)
open(out, "w").write(chr(10).join(lines) + chr(10))
sys.exit(0 if bad_total == 0 else 3)
PY
	_rc=$?
	sed 's/^/       /' "$WORK/npu-only.log" 2>/dev/null | head -n 8
	if [ "$_rc" -eq 0 ]; then
		ok "npu-only: every shipped graph is 100% EPContext nodes (no CPU-executable node exists)"
	else
		bad "npu-only: at least one graph carries non-EPContext nodes -- ORT will place those on the CPU EP. See $WORK/npu-only.txt"
	fi
else
	wrn "npu-only: python3 missing, cannot verify the graphs are EPContext-only"
fi

# The engine is started with the "arena_disjoint" contract; the runtime must not
# have a CPU fallback path enabled either.
if [ -f "$LIB_DIR/runtime.json" ]; then
	if grep -Eq '"cpu_fallback"[[:space:]]*:[[:space:]]*true' "$LIB_DIR/runtime.json" ||
	   grep -Eq '"allow_cpu_fallback"[[:space:]]*:[[:space:]]*true' "$LIB_DIR/runtime.json"; then
		bad "npu-only: $LIB_DIR/runtime.json enables a CPU fallback for NPU partitions"
	else
		ok "npu-only: $LIB_DIR/runtime.json has no CPU fallback switch enabled"
	fi
fi

# -----------------------------------------------------------------------------
section "3. DSP-visible skel directory"

if [ -f "$SKEL_DIR/libQnnHtpV68Skel.so" ]; then
	ok "dsp: $SKEL_DIR/libQnnHtpV68Skel.so is in place"
else
	bad "dsp: $SKEL_DIR/libQnnHtpV68Skel.so is missing"
fi

# Only /usr/lib/dsp/{cdsp,adsp} and /dsp are the DSP's view of its own
# filesystem.  A skel that lives only in /usr/lib/rfsa/adsp (the host view) is
# invisible to the HTP: the run fails with "Failed to load skel" 1002.
case "$SKEL_DIR" in
/usr/lib/dsp/cdsp | /usr/lib/dsp/cdsp/ | /usr/lib/dsp/adsp | /usr/lib/dsp/adsp/ | /dsp | /dsp/)
	ok "dsp: $SKEL_DIR is a DSP-visible tree (HTP runs on the CDSP: 'Effective cdsp_id is: 3')"
	;;
/usr/lib/rfsa/adsp*)
	bad "dsp: QWEN35_SKEL_DIR=$SKEL_DIR is the HOST view of the DSP tree -- the DSP cannot see it (use /usr/lib/dsp/cdsp)"
	;;
*)
	wrn "dsp: $SKEL_DIR is not under /usr/lib/dsp/{cdsp,adsp} or /dsp -- verify the DSP can see it, or QnnDevice_create fails"
	;;
esac

for _d in /usr/lib/dsp/cdsp /usr/lib/dsp/adsp /dsp; do
	if [ -d "$_d" ]; then
		info "DSP-visible tree: $_d ($(ls "$_d" 2>/dev/null | wc -l | tr -d ' ') entries)"
	fi
done

# The trap from the old image: a QAIRT 2.47 skel in the host-view directory next
# to a 2.40 backend.  It is not in ADSP_LIBRARY_PATH any more, but say so loudly
# if the release differs -- someone will copy it there again.
_foreign=/usr/lib/rfsa/adsp/libQnnHtpV68Skel.so
if [ -f "$_foreign" ]; then
	_foreign_v=$(qairt_version "$_foreign") || _foreign_v=""
	if [ -n "${_backend_v:-}" ] && [ -n "${_foreign_v:-}" ] && [ "$_foreign_v" != "$_backend_v" ]; then
		wrn "dsp: $_foreign is QAIRT ${_foreign_v}, backend is ${_backend_v} -- this is the 2.47/2.40 mix that used to abort QnnDevice_create; keep it out of ADSP_LIBRARY_PATH"
	elif [ -n "${_foreign_v:-}" ]; then
		info "dsp: $_foreign is QAIRT $_foreign_v (same release as the backend, unused)"
	fi
fi

# -----------------------------------------------------------------------------
section "4. ADSP_LIBRARY_PATH format"

# Read it from the running process when possible: that is the value QNN actually
# sees, not what the unit file intends.
_pid=$(systemctl show "$UNIT" -p MainPID --value 2>/dev/null || printf '')
_adsp=""
if is_uint "${_pid:-}" && [ "$_pid" -gt 0 ] && [ -r "/proc/$_pid/environ" ]; then
	_adsp=$(tr '\0' '\n' <"/proc/$_pid/environ" 2>/dev/null |
		sed -n 's/^ADSP_LIBRARY_PATH=//p' | head -n 1)
	[ -n "$_adsp" ] && info "source: /proc/$_pid/environ (pid of the running server)"
fi
if [ -z "$_adsp" ]; then
	_adsp=$(systemctl show "$UNIT" -p Environment --value 2>/dev/null |
		tr ' ' '\n' | sed -n 's/^ADSP_LIBRARY_PATH=//p' | head -n 1)
	[ -n "$_adsp" ] && info "source: systemctl show $UNIT -p Environment (process not running)"
fi

if [ -z "$_adsp" ]; then
	bad "adsp: ADSP_LIBRARY_PATH is not set for $UNIT"
else
	info "value: $_adsp"
	case "$_adsp" in
	*';'*) ok "adsp: ADSP_LIBRARY_PATH is semicolon separated (the only form QNN understands)" ;;
	*) bad "adsp: ADSP_LIBRARY_PATH contains no ';' -- QNN will read it as one directory name (skel 1002 -> QnnDevice_create 14001)" ;;
	esac
	case "$_adsp" in
	*:*) bad "adsp: ADSP_LIBRARY_PATH contains ':' (path-list syntax) -- it must be ';', never ':'" ;;
	*) ok "adsp: no ':' in ADSP_LIBRARY_PATH" ;;
	esac
	case "$_adsp" in
	*/usr/lib/rfsa/adsp*) wrn "adsp: lists /usr/lib/rfsa/adsp, the host view of the DSP tree (a foreign QAIRT skel lives there)" ;;
	esac
	# Every element should be a readable directory; a typo here is silent.
	_missing_dirs=""
	_old_ifs=$IFS
	IFS=';'
	for _d in $_adsp; do
		[ -n "$_d" ] || continue
		[ -d "$_d" ] || _missing_dirs="$_missing_dirs $_d"
	done
	IFS=$_old_ifs
	if [ -n "$_missing_dirs" ]; then
		# Harmless: the DSP loader skips a path it cannot open, and /dsp is kept
		# for parity with upstream's installer (it exists on images that mount
		# the DSP partition).  Only an unexpected entry would be a problem.
		info "adsp: entries that do not exist (skipped by the DSP loader):$_missing_dirs"
	else
		ok "adsp: every ADSP_LIBRARY_PATH entry is an existing directory"
	fi
	case "$_adsp" in
	*"$SKEL_DIR"*) ok "adsp: contains the skel directory $SKEL_DIR" ;;
	*) wrn "adsp: does not contain $SKEL_DIR (the launch script normally prepends it)" ;;
	esac
fi

# -----------------------------------------------------------------------------
section "5. service state"

if ! command -v systemctl >/dev/null 2>&1; then
	bad "unit: systemctl is not available"
else
	_en=$(systemctl is-enabled "$UNIT" 2>/dev/null || true)
	if [ -z "$_en" ]; then
		bad "unit: $UNIT is not known to systemd (no unit file installed)"
		info "skipped is-active/NRestarts/ExecStart: there is nothing to query"
	else
		# On-demand by design.  The resident server pins four big cores, holds
		# ~490 MB RSS and owns the whole NPU context, so it must NOT come up at
		# boot and compete with everything else on the board; it is started by
		# 'systemctl start qwen35' or by npu-chat when qwen3.5 is selected.
		case "$_en" in
		disabled) ok "unit: $UNIT is-enabled -> disabled (on-demand only, starts when asked)" ;;
		static | indirect) ok "unit: $UNIT is-enabled -> $_en (not pulled in at boot)" ;;
		enabled | enabled-runtime) bad "unit: $UNIT is-enabled -> $_en -- it would start at boot and hold four big cores plus the NPU context" ;;
		masked) bad "unit: $UNIT is-enabled -> masked (cannot be started on demand either)" ;;
		*) wrn "unit: $UNIT is-enabled -> $_en (unexpected state)" ;;
		esac

		_act=$(systemctl is-active "$UNIT" 2>/dev/null || true)
		case "$_act" in
		active) ok "unit: $UNIT is-active -> active" ;;
		*) bad "unit: $UNIT is-active -> ${_act:-unknown}; see: journalctl -u $UNIT -b --no-pager | tail -n 50" ;;
		esac

		_nr=$(systemctl show "$UNIT" -p NRestarts --value 2>/dev/null || printf '')
		if is_uint "${_nr:-}"; then
			if [ "$_nr" -eq 0 ]; then
				ok "unit: NRestarts=0 (no crash-restart since boot)"
			elif [ "$_nr" -le 2 ]; then
				wrn "unit: NRestarts=$_nr -- if nobody restarted it by hand this is a crash; journalctl -u $UNIT --no-pager | tail -n 100"
			else
				bad "unit: NRestarts=$_nr -- restart loop (RestartSec=10); the server is dying on start"
			fi
		else
			wrn "unit: NRestarts not reported by systemd ($_nr)"
		fi

		_sub=$(systemctl show "$UNIT" -p SubState --value 2>/dev/null || true)
		_exec=$(systemctl show "$UNIT" -p ExecStart --value 2>/dev/null |
			sed -n 's/.*path=\([^ ;]*\).*/\1/p' | head -n 1)
		info "substate=${_sub:-?} exec=${_exec:-?}"
		case "${_exec:-}" in
		*"$LIB_DIR/qwen35-launch.sh") ok "unit: ExecStart points at $LIB_DIR/qwen35-launch.sh" ;;
		"") wrn "unit: could not read ExecStart" ;;
		*) bad "unit: ExecStart is $_exec, expected $LIB_DIR/qwen35-launch.sh" ;;
		esac
	fi
fi

for _d in "$STATE_DIR" "$STATE_DIR/scratch" "$STATE_DIR/logs"; do
	if [ -d "$_d" ]; then
		ok "state: $_d exists ($(ls "$_d" 2>/dev/null | wc -l | tr -d ' ') entries)"
	else
		if [ "$_d" = "$STATE_DIR" ]; then
			bad "state: $_d is missing (StateDirectory=qwen35 creates it)"
		else
			wrn "state: $_d is missing (created on first start)"
		fi
	fi
done

# stage_worker.py only creates its vision warmup directory below
# "<program dir>/scratch" and exits otherwise ("warmup must stay inside the new
# layered scratch directory"), while qwen_server.py hands it
# paths["scratch_root"]/warmup_vision_<pid>.  The recipe therefore installs
# $LIB_DIR/scratch as a symlink into the writable state root; with that link
# missing the server dies during startup and never listens on $BASE_URL.
if [ -L "$LIB_DIR/scratch" ]; then
	_src=$(readlink -f "$LIB_DIR/scratch" 2>/dev/null || true)
	_dst=$(readlink -f "$STATE_DIR/scratch" 2>/dev/null || true)
	if [ -n "$_src" ] && [ "$_src" = "$_dst" ]; then
		ok "state: $LIB_DIR/scratch -> $(readlink "$LIB_DIR/scratch") (worker guard resolves here)"
	else
		bad "state: $LIB_DIR/scratch resolves to ${_src:-?}, expected $STATE_DIR/scratch"
	fi
elif [ -e "$LIB_DIR/scratch" ]; then
	bad "state: $LIB_DIR/scratch is a real directory, not a symlink to $STATE_DIR/scratch"
else
	bad "state: $LIB_DIR/scratch is missing -- stage_worker.py refuses to start without it"
fi

# -----------------------------------------------------------------------------
section "6. readiness marker /run/qwen35.ok"

if [ -f /run/qwen35.ok ]; then
	ok "/run/qwen35.ok present: $(tr '\n' ' ' </run/qwen35.ok | cut -c1-100)"
elif [ "$(systemctl is-active qwen35 2>/dev/null)" = "active" ]; then
	bad "/run/qwen35.ok missing while the unit is active -- qwen35-launch.sh pumps it
       from /v1/models; npu-chat reads it as QWEN35_OK"
else
	info "/run/qwen35.ok absent -- the unit is not running (start it with
       'systemctl start qwen35'); check 7 is the authoritative readiness test"
fi
if [ -f /run/qwen35/qwen35.lock ]; then
	ok "single-instance lock /run/qwen35/qwen35.lock exists (launch.sh is running under flock)"
else
	wrn "no /run/qwen35/qwen35.lock -- the server was not started by qwen35-launch.sh"
fi

# -----------------------------------------------------------------------------
section "7. HTTP endpoints"

if ! command -v curl >/dev/null 2>&1; then
	bad "http: curl is not installed, cannot probe $BASE_URL (add 'curl' to the image)"
	MODEL_ID="qwen3.5-0.8b-q6a-2k"
else
	_code=$(http GET /health "$WORK/health.json" 10)
	if [ "$_code" = 200 ]; then
		ok "http: GET /health -> 200"
		if grep -Eq '"ready"[[:space:]]*:[[:space:]]*true' "$WORK/health.json"; then
			ok "http: /health reports ready=true"
		else
			bad "http: /health reports ready=false ($(head -c 160 "$WORK/health.json"))"
		fi
		if grep -Eq '"status"[[:space:]]*:[[:space:]]*"ok"' "$WORK/health.json"; then
			ok "http: /health status=ok"
		else
			wrn "http: /health status is not \"ok\""
		fi

		# The server's own account of where the model runs.  That report is the
		# authoritative placement statement, so assert on its structure instead
		# of one magic string: every declared placement must name QNN and the
		# HTP, none may name the CPU, the CPU-fallback switch must be off, the
		# vision frontend must show zero CPU-assigned nodes, and every vision
		# component must have at least one QNN node.
		_hs_ok=1
		python3 - "$WORK/health.json" >"$WORK/health-npu.txt" 2>&1 <<'PY' || _hs_ok=0
import json, sys

bad = 0
def chk(cond, msg):
    global bad
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond:
        bad += 1

try:
    d = json.load(open(sys.argv[1]))
except Exception as exc:
    print("FAIL /health is not valid JSON (%s)" % exc)
    raise SystemExit(1)

b = d.get("backend") or {}
place = {k: b[k] for k in ("vision", "layers_0_19", "layers_20_23", "decode")
         if isinstance(b.get(k), str)}
chk(len(place) == 4, "backend declares all 4 placements (%d found)" % len(place))
for k in sorted(place):
    v = place[k]
    up = v.upper()
    chk("QNN" in up and "HTP" in up and "CPU" not in up,
        "backend.%-12s -> %s" % (k, v))
chk(b.get("cpu_fallback_for_npu_partitions") is False,
    "backend.cpu_fallback_for_npu_partitions=false")
chk(b.get("language_prefill_full24_npu") is True,
    "backend.language_prefill_full24_npu=true (prefill is not split to the CPU)")

st = d.get("persistent_stages") or {}
g = st.get("vision_startup_gates") or {}
chk(g.get("fallback_disabled") is True,
    "vision_startup_gates.fallback_disabled=true")
chk(g.get("cpu_assigned_nodes_zero_in_every_component") is True,
    "vision_startup_gates.cpu_assigned_nodes_zero_in_every_component=true")
chk(g.get("only_qnn_provider_in_every_profile") is True,
    "vision_startup_gates.only_qnn_provider_in_every_profile=true")
cpu = st.get("vision_cpu_assigned_nodes") or {}
for name in sorted(cpu):
    chk(cpu[name] == 0, "vision_cpu_assigned_nodes.%-8s = %s" % (name, cpu[name]))
qnn = st.get("vision_qnn_assigned_nodes") or {}
for name in sorted(qnn):
    chk(isinstance(qnn[name], int) and qnn[name] > 0,
        "vision_qnn_assigned_nodes.%-8s = %s" % (name, qnn[name]))
raise SystemExit(1 if bad else 0)
PY
		if [ "$_hs_ok" -eq 1 ]; then
			ok "npu-only: /health puts every stage on QNN/HTP ($(sed -n 's/^PASS backend\.\([a-z_0-9]*\)  *-> */\1 /p' "$WORK/health-npu.txt" | tr '\n' ' '))"
		else
			bad "npu-only: /health reports a stage that is not on the QNN/HTP:"
			sed -n 's/^FAIL /       /p' "$WORK/health-npu.txt" | head -n 6
		fi
	else
		bad "http: GET /health -> $_code $(head -c 200 "$WORK/curl.err" 2>/dev/null | tr '\n' ' ')"
	fi

	MODEL_ID=$(sed -n 's/.*"model"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$WORK/health.json" 2>/dev/null | head -n 1)
	[ -n "${MODEL_ID:-}" ] || MODEL_ID="qwen3.5-0.8b-q6a-2k"
	info "model_id=$MODEL_ID  context=$(sed -n 's/.*"context_length"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' "$WORK/health.json" 2>/dev/null | head -n 1)"

	_code=$(http GET /v1/models "$WORK/models.json" 10)
	if [ "$_code" = 200 ] && grep -q "\"$MODEL_ID\"" "$WORK/models.json" 2>/dev/null; then
		ok "http: GET /v1/models -> 200 and lists $MODEL_ID"
	elif [ "$_code" = 200 ]; then
		wrn "http: GET /v1/models -> 200 but does not list $MODEL_ID ($(head -c 200 "$WORK/models.json"))"
	else
		bad "http: GET /v1/models -> $_code"
	fi

	# /metrics is not part of the OpenAI surface but is the cheapest way to see
	# whether an earlier request crashed the decoder (requests_failed).
	_code=$(http GET /metrics "$WORK/metrics.json" 10)
	if [ "$_code" = 200 ]; then
		_ok_n=$(sed -n 's/.*"requests_completed"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' "$WORK/metrics.json" | head -n 1)
		_bad_n=$(sed -n 's/.*"requests_failed"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' "$WORK/metrics.json" | head -n 1)
		info "metrics: completed=${_ok_n:-?} failed=${_bad_n:-?} uptime=$(sed -n 's/.*"uptime_seconds"[[:space:]]*:[[:space:]]*\([0-9.]*\).*/\1/p' "$WORK/metrics.json" | head -n 1)s"
		if is_uint "${_bad_n:-}" && [ "$_bad_n" -eq 0 ]; then
			ok "metrics: no failed requests since start"
		elif is_uint "${_bad_n:-}"; then
			wrn "metrics: $_bad_n failed request(s) since start (the decoder survived, but see journalctl)"
		fi
	else
		wrn "http: GET /metrics -> $_code (not fatal, diagnostic endpoint only)"
	fi
fi

# -----------------------------------------------------------------------------
if [ "$QUICK" -eq 1 ]; then
	section "8-11. skipped (--quick)"
	info "chat round trip, decode throughput, memory/temperature and crash evidence
       are only run in a full pass: drop --quick"
else

	section "8. real inference round trip (17*23)"

	_answered=0
	if ! command -v curl >/dev/null 2>&1; then
		bad "chat: curl missing, cannot run inference"
	elif [ ! -s "$WORK/health.json" ]; then
		bad "chat: $BASE_URL did not answer /health, skipping inference"
	else
		cat >"$WORK/ask.json" <<JSON
{
  "model": "${MODEL_ID:-qwen3.5-0.8b-q6a-2k}",
  "messages": [
    {"role": "user", "content": "What is 17*23? Reply with the number and nothing else."}
  ],
  "max_tokens": 96,
  "temperature": 0,
  "stream": false
}
JSON
		# 420 s is the unit's decoder_timeout_seconds: a cold start also pays the
		# ~20 s context load, so do not use a tighter client timeout.
		_code=$(http POST /v1/chat/completions "$WORK/chat.json" 420 "$WORK/ask.json")
		if [ "$_code" != 200 ]; then
			bad "chat: POST /v1/chat/completions -> $_code $(head -c 300 "$WORK/chat.json" 2>/dev/null | tr '\n' ' ')"
		else
			_answered=1
			ok "chat: POST /v1/chat/completions -> 200"
			_rc=0
			python3 - "$WORK/chat.json" >"$WORK/chat.txt" 2>"$WORK/chat.err" <<'PY' || _rc=$?
import json, sys, unicodedata

try:
    body = json.load(open(sys.argv[1], encoding="utf-8"))
except Exception as exc:                                  # noqa: BLE001
    print("parse_error=%s" % exc)
    raise SystemExit(3)

try:
    text = body["choices"][0]["message"]["content"]
except Exception as exc:                                  # noqa: BLE001
    print("shape_error=%s" % exc)
    raise SystemExit(3)

text = text or ""
n = len(text)
usage = body.get("usage") or {}
metrics = body.get("qwen35_metrics") or {}

# Garbage detection.  A broken decoder does not usually raise: it emits U+FFFD
# (invalid UTF-8), control characters, one character repeated forever, or a short
# token cycle ("391391391...", "the the the").  Character runs alone miss the
# cycle case, so also measure how much of the answer is periodic.
replacement = text.count("\ufffd")
control = sum(1 for ch in text if unicodedata.category(ch) in ("Cc", "Cn", "Co", "Cs") and ch not in "\n\t")
longest = 1
run = 1
for i in range(1, n):
    if text[i] == text[i - 1]:
        run += 1
        longest = max(longest, run)
    else:
        run = 1

# Longest tail that is explained by a period of 1..12 characters, as a fraction
# of the answer.  "391" * 10 gives 1.0 with period 3; prose stays below ~0.2.
periodic = 0
for period in range(1, 13):
    k = 0
    i = n - 1
    while i - period >= 0 and text[i] == text[i - period]:
        k += 1
        i -= 1
    covered = k + period if k else 0
    if covered > periodic:
        periodic = covered

print("n_chars=%d" % n)
print("replacement=%d" % replacement)
print("control=%d" % control)
print("longest_run=%d" % longest)
print("printable_ratio=%.4f" % ((n - replacement - control) / n if n else 0.0))
print("repeat_ratio=%.4f" % (longest / n if n else 0.0))
print("periodic_ratio=%.4f" % (periodic / n if n else 0.0))
print("has_answer=%d" % (1 if "391" in text.replace(",", "").replace(" ", "") else 0))
print("prompt_tokens=%s" % usage.get("prompt_tokens", "?"))
print("completion_tokens=%s" % usage.get("completion_tokens", "?"))
print("ttft_seconds=%s" % (metrics.get("ttft_seconds", "?")))
print("request_seconds=%s" % (metrics.get("request_seconds", "?")))
print("decode_tps=%s" % (metrics.get("decode_tokens_per_second", "?")))
print("finish_reason=%s" % (body["choices"][0].get("finish_reason", "?")))
print("text=%s" % text.replace("\n", "\\n")[:200])
PY
			if [ "$_rc" -ne 0 ]; then
				bad "chat: response is not a usable chat completion -- $(head -c 200 "$WORK/chat.txt" "$WORK/chat.err" 2>/dev/null | tr '\n' ' ')"
			else
				_n=$(sed -n 's/^n_chars=//p' "$WORK/chat.txt" | head -n 1)
				_rep=$(sed -n 's/^replacement=//p' "$WORK/chat.txt" | head -n 1)
				_ctl=$(sed -n 's/^control=//p' "$WORK/chat.txt" | head -n 1)
				_prd=$(sed -n 's/^printable_ratio=//p' "$WORK/chat.txt" | head -n 1)
				_rr=$(sed -n 's/^repeat_ratio=//p' "$WORK/chat.txt" | head -n 1)
				_pr=$(sed -n 's/^periodic_ratio=//p' "$WORK/chat.txt" | head -n 1)
				_has=$(sed -n 's/^has_answer=//p' "$WORK/chat.txt" | head -n 1)
				info "answer=\"$(sed -n 's/^text=//p' "$WORK/chat.txt" | head -n 1)\""
				info "completion_tokens=$(sed -n 's/^completion_tokens=//p' "$WORK/chat.txt" | head -n 1) finish_reason=$(sed -n 's/^finish_reason=//p' "$WORK/chat.txt" | head -n 1) ttft=$(sed -n 's/^ttft_seconds=//p' "$WORK/chat.txt" | head -n 1)s"

				if [ "${_has:-0}" = 1 ]; then
					ok "chat: the answer contains 391 (17*23 is computed correctly, not parroted)"
				else
					bad "chat: the answer does not contain 391 -- the model is not actually reasoning"
				fi
				if [ "${_rep:-1}" -eq 0 ] && [ "${_ctl:-1}" -eq 0 ]; then
					ok "chat: no U+FFFD and no control characters (valid UTF-8 text)"
				else
					bad "chat: garbled output (${_rep:-?} replacement chars, ${_ctl:-?} control chars) -- the decode state is corrupt"
				fi
				if ! ge "${_prd:-0}" 0.98; then
					bad "chat: printable ratio ${_prd:-?} -- non-text bytes in the answer"
				elif ge "${_rr:-1}" 0.35 || ge "${_pr:-0}" 0.80; then
					bad "chat: the answer is repetitive (char run ${_rr:-?}, periodic ${_pr:-?}) -- token repeat loop, the decode state is stuck"
				else
					ok "chat: output shape is sane (printable ${_prd:-?}, char run ${_rr:-?}, periodic ${_pr:-?})"
				fi
				if is_uint "${_n:-}" && [ "$_n" -ge 1 ]; then
					ok "chat: non-empty answer ($_n characters)"
				else
					bad "chat: empty answer"
				fi
			fi
		fi
	fi

	# -------------------------------------------------------------------------
	section "9. decode throughput (3 runs)"

	: >"$WORK/perf.txt"
	if [ "$_answered" -eq 0 ]; then
		wrn "perf: skipped, check 8 produced no successful completion"
	else
		cat >"$WORK/perf.json" <<JSON
{
  "model": "${MODEL_ID:-qwen3.5-0.8b-q6a-2k}",
  "messages": [
    {"role": "user", "content": "Explain in three sentences why the sky looks blue during the day."}
  ],
  "max_tokens": 96,
  "temperature": 0,
  "seed": 0,
  "stream": false
}
JSON
		_i=1
		while [ "$_i" -le 3 ]; do
			_code=$(http POST /v1/chat/completions "$WORK/perf-$_i.json" 420 "$WORK/perf.json")
			if [ "$_code" != 200 ]; then
				wrn "perf: run $_i -> HTTP $_code, dropped"
			else
				python3 - "$WORK/perf-$_i.json" >>"$WORK/perf.txt" 2>/dev/null <<'PY'
import json, sys

body = json.load(open(sys.argv[1], encoding="utf-8"))
usage = body.get("usage") or {}
metrics = body.get("qwen35_metrics") or {}

ctok = usage.get("completion_tokens")
ttft = metrics.get("ttft_seconds")
request_s = metrics.get("request_seconds")
tps = metrics.get("decode_tokens_per_second")

# decode-only rate when the engine reports it; otherwise completion time minus
# TTFT (a full-request rate would silently include prefill).
if tps is None and ctok and request_s and ttft is not None:
    span = request_s - ttft
    tps = ctok / span if span > 0 else None
if tps is None and ctok and request_s:
    tps = ctok / request_s

print("%s %s %s %s" % (tps if tps is not None else "nan", ctok or 0,
                       ttft if ttft is not None else "nan",
                       request_s if request_s is not None else "nan"))
PY
				info "run $_i: $(tail -n 1 "$WORK/perf.txt" | awk '{printf "%s tok/s, %s tokens, ttft %ss, wall %ss", $1, $2, $3, $4}')"
			fi
			_i=$((_i + 1))
		done

		if [ ! -s "$WORK/perf.txt" ]; then
			bad "perf: no successful timed run"
		else
			awk -v ref="$RADXA_DECODE_TPS" '
				$1 ~ /^[0-9.]+$/ {
					n++; s += $1
					if (n == 1 || $1 < mn) mn = $1
					if (n == 1 || $1 > mx) mx = $1
				}
				END {
					if (n == 0) { print "0 0 0 0 0"; exit }
					printf "%d %.2f %.2f %.2f %s\n", n, mn, s / n, mx, (s / n >= ref ? "ref" : "below")
				}' "$WORK/perf.txt" >"$WORK/perf.sum"
			read -r _n _min _avg _max _tag <"$WORK/perf.sum"
			if [ "${_n:-0}" -eq 0 ]; then
				bad "perf: no numeric decode rate in any run"
			else
				info "runs=$_n  min/avg/max = $_min / $_avg / $_max tok/s (reference: Radxa Dragon Q6A, same SoC and 2K release = $RADXA_DECODE_TPS tok/s)"
				if ge "$_avg" "$RADXA_DECODE_TPS"; then
					ok "perf: avg decode $_avg tok/s >= the Radxa reference ($RADXA_DECODE_TPS tok/s)"
				elif ge "$_avg" 7.0; then
					ok "perf: avg decode $_avg tok/s is within 6% of the Radxa reference"
				elif ge "$_avg" 5.0; then
					wrn "perf: avg decode $_avg tok/s is below the Radxa reference ($RADXA_DECODE_TPS) -- check CPUAffinity=4-7, thermal throttling and that prefill is on the NPU"
				else
					bad "perf: avg decode $_avg tok/s is far below the Radxa reference ($RADXA_DECODE_TPS) -- something is running on the CPU (see 'decode' in /health backend block)"
				fi
				# Run-to-run spread is what tells thermal throttling from noise.
				_spread=$(awk -v a="$_max" -v b="$_min" 'BEGIN { print (b + 0 > 0 ? a / b : 1) }')
				if ge "$_spread" 1.5; then
					wrn "perf: max/min spread is ${_spread}x ($_min -> $_max tok/s) -- suspect thermal throttling or a busy core"
				else
					ok "perf: run-to-run spread is stable (${_spread}x, $_min .. $_max tok/s)"
				fi
			fi
		fi
	fi

	# -------------------------------------------------------------------------
	section "10. memory and temperature"

	_cur=$(systemctl show "$UNIT" -p MemoryCurrent --value 2>/dev/null || printf '')
	_max=$(systemctl show "$UNIT" -p MemoryMax --value 2>/dev/null || printf '')
	_peak=$(systemctl show "$UNIT" -p MemoryPeak --value 2>/dev/null || printf '')
	if is_uint "${_cur:-}" && is_uint "${_max:-}" && [ "$_max" -gt 0 ]; then
		info "MemoryCurrent=$(hbytes "$_cur")  MemoryMax=$(hbytes "$_max")$(is_uint "${_peak:-}" && printf '  MemoryPeak=%s' "$(hbytes "$_peak")")"
		if [ "$_cur" -gt "$_max" ]; then
			bad "mem: MemoryCurrent exceeds MemoryMax -- the cgroup is at its ceiling and the next allocation is an OOM kill"
		elif ge "$(awk -v a="$_cur" -v b="$_max" 'BEGIN { print a / b }')" 0.90; then
			wrn "mem: MemoryCurrent is above 90% of MemoryMax (1.8 GB embedding mmap + ~850 MB QNN context is expected; a leak also looks like this)"
		else
			ok "mem: MemoryCurrent is $(awk -v a="$_cur" -v b="$_max" 'BEGIN { printf "%.0f", 100 * a / b }')% of MemoryMax"
		fi
	elif [ "$_cur" = "[not set]" ]; then
		wrn "mem: the unit has no cgroup memory accounting (MemoryAccounting= off?)"
	else
		wrn "mem: MemoryCurrent/MemoryMax not readable ('$_cur' / '$_max')"
	fi

	_avail=$(sed -n 's/^MemAvailable:[[:space:]]*\([0-9]*\).*/\1/p' /proc/meminfo | head -n 1)
	if is_uint "${_avail:-}"; then
		info "MemAvailable=$(hbytes $((_avail * 1024))) of $(hbytes "$(awk '/^MemTotal:/ { print $2 * 1024 }' /proc/meminfo)")"
		if [ "$_avail" -lt 262144 ]; then
			bad "mem: less than 256 MiB MemAvailable -- the next large allocation will trigger the OOM killer"
		elif [ "$_avail" -lt 524288 ]; then
			wrn "mem: less than 512 MiB MemAvailable"
		else
			ok "mem: $(hbytes $((_avail * 1024))) available"
		fi
	fi

	_hot=0
	_hotz=""
	for _z in /sys/class/thermal/thermal_zone*/temp; do
		[ -r "$_z" ] || continue
		_t=$(cat "$_z" 2>/dev/null || printf '')
		is_uint "${_t:-}" || continue
		if [ "$_t" -gt "$_hot" ]; then
			_hot=$_t
			_hotz=${_z%/temp}
		fi
	done
	if [ "$_hot" -gt 0 ]; then
		_type=$(cat "$_hotz/type" 2>/dev/null || printf '?')
		_c=$((_hot / 1000))
		info "hottest zone: $_type = ${_c} C (${_hotz})"
		if [ "$_c" -ge 95 ]; then
			bad "thermal: ${_c} C -- the SoC is throttling or about to; decode rate will collapse"
		elif [ "$_c" -ge 85 ]; then
			wrn "thermal: ${_c} C -- close to the throttle point"
		else
			ok "thermal: ${_c} C (below the 85 C throttle margin)"
		fi
		# The big cluster is where the sampler and the state copies run.
		for _z in /sys/class/thermal/thermal_zone*; do
			_case=$(cat "$_z/type" 2>/dev/null || printf '')
			case "$_case" in
			*cpu7* | *cpu-7* | *cpuss* | *big* | *cluster*)
				info "  $_case = $(( $(cat "$_z/temp" 2>/dev/null || printf 0) / 1000 )) C"
				;;
			esac
		done
	else
		wrn "thermal: no readable thermal zone"
	fi

	# -------------------------------------------------------------------------
	section "11. crash evidence"

	if dmesg >"$WORK/dmesg.txt" 2>"$WORK/dmesg.err"; then
		# Things that mean "this boot already had a bad time".  SSR and fastrpc
		# timeouts are the DSP-side signature of a QNN session that died.
		_re='Kernel panic|Oops|BUG: |Unable to handle kernel|Call trace:|Internal error|subsys_(notif|err)|SSR |watchdog|fastrpc.*(error|fail|timeout|crash)|qcom_scm.*(error|fail)|arm-smmu.*(fault|error)'
		_n=$(grep -ciE "$_re" "$WORK/dmesg.txt" 2>/dev/null || printf 0)
		if [ "${_n:-0}" -eq 0 ]; then
			ok "dmesg: no panic/oops/SSR/fastrpc error since boot"
		else
			wrn "dmesg: $_n line(s) matching panic/oops/SSR/fastrpc error -- last 5:"
			grep -inE "$_re" "$WORK/dmesg.txt" | tail -n 5 | sed 's/^/       /' | cut -c1-200
		fi
		if grep -qiE 'Kernel panic|Oops|Unable to handle kernel' "$WORK/dmesg.txt"; then
			bad "dmesg: this boot contains a kernel panic/oops -- the board is not stable"
		fi
		if grep -qiE 'fastrpc.*(fail|error|timeout)|subsys_(notif|err)|SSR ' "$WORK/dmesg.txt"; then
			wrn "dmesg: DSP (fastrpc/SSR) errors present -- a QNN session died; re-run the perf check and watch NRestarts"
		fi
	else
		wrn "dmesg: not readable ($(head -n1 "$WORK/dmesg.err" 2>/dev/null)) -- run as root"
	fi

	if [ -d /var/log/journal ]; then
		ok "journal: /var/log/journal exists (persistent journal is configured)"
	else
		wrn "journal: /var/log/journal is missing -- journald is volatile (/run/log/journal),
       so a crash loses its log. This is the image default (persistent storage is a
       whole-system trade-off, not something this package changes). To keep crash
       logs: cp /usr/share/doc/qwen35-npu/journald.conf.example /etc/systemd/journald.conf.d/"
	fi
	if command -v journalctl >/dev/null 2>&1; then
		_boots=$(journalctl --list-boots -q 2>/dev/null | wc -l | tr -d ' \t')
		is_uint "${_boots:-}" || _boots=0
		if [ "$_boots" -ge 2 ]; then
			ok "journal: $_boots boots recorded (previous boots survive a reboot)"
		elif [ "$_boots" -eq 1 ]; then
			wrn "journal: only the current boot is recorded -- either this is the first boot after flashing or the journal is volatile"
		else
			bad "journal: journalctl --list-boots is empty -- no journal at all"
		fi
		info "journal disk usage: $(journalctl --disk-usage 2>/dev/null | tr '\n' ' ')"
		# A previous boot that ends in a panic is the most valuable artefact here.
		if [ "$_boots" -ge 2 ] &&
			journalctl -b -1 --no-pager 2>/dev/null | grep -qiE 'Kernel panic|Oops|BUG: |watchdog|SSR '; then
			wrn "journal: the previous boot ends with a panic/oops/watchdog line -- capture it: journalctl -b -1 --no-pager | tail -n 200"
		fi
	else
		wrn "journal: journalctl not available"
	fi

	# pstore/ramoops survives a reboot even when the journal does not.
	if [ -d /sys/fs/pstore ]; then
		_p=$(ls -A /sys/fs/pstore 2>/dev/null | tr '\n' ' ')
		if [ -n "$_p" ]; then
			wrn "pstore: crash dump from a previous boot present: $_p -- read it, then clear /sys/fs/pstore"
		else
			ok "pstore: empty (no ramoops dump from a previous crash)"
		fi
	fi

	_failed=$(systemctl --failed --no-legend --plain 2>/dev/null | wc -l | tr -d ' \t')
	if is_uint "${_failed:-}" && [ "$_failed" -eq 0 ]; then
		ok "units: no failed systemd units"
	else
		wrn "units: $_failed failed systemd unit(s):"
		systemctl --failed --no-legend --plain 2>/dev/null | head -n 10 | sed 's/^/       /'
	fi
fi

# =============================================================================
printf '\n== summary ==\n'
printf 'PASS %d   FAIL %d   WARN %d\n' "$PASS_N" "$FAIL_N" "$WARN_N"
if [ "$FAIL_N" -gt 0 ]; then
	printf 'RESULT: FAIL -- %d check(s) failed\n' "$FAIL_N"
	exit 1
fi
printf 'RESULT: PASS'
[ "$WARN_N" -gt 0 ] && printf ' (with %d warning(s))' "$WARN_N"
printf '\n'
exit 0
