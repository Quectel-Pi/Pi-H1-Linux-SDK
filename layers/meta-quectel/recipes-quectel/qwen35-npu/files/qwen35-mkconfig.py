#!/usr/bin/env python3
"""Generate runtime.json for the Qwen3.5-0.8B QNN/HTP server (Yocto build time).

This is a port of the `cfg['paths']` block of the upstream installer
(`install.py` of radxa-dragon-q6a-qwen3.5-0.8b-qcs6490-qnn-npu, Apache-2.0),
rewritten so that it runs on the *build host* and emits a fixed Yocto layout
instead of installing into an arbitrary user directory on the board:

    /usr/share/qwen35/<role path>     model assets  (role -> path is taken from
                                      assets/manifest.json, groups common+2k)
    /usr/lib/qwen35/                  python runtime, native engine, shims,
                                      runtime.json
    /var/lib/qwen35/                  writable state: scratch/ and logs/

Differences from upstream install.py (see PORT_NOTES.md for the full list):
  * no aarch64 / package-version / binary-sha256 / CPU-affinity checks: the
    image build guarantees those;
  * no g++ invocation and no --sampler-self-test: the recipe cross-compiles
    qwen_text_engine and the board runs the self-test;
  * `python` and `decoder_engine` point at the two shims (qwen-python,
    engine.sh) that upstream install.py wrote into the install directory;
    this script writes them next to runtime.json (qwen35-launch.sh recreates
    them if they are ever missing);
  * QNN library paths are resolved dynamically through onnxruntime_qnn when
    that module is importable, otherwise from explicit arguments / a search of
    the candidate QNN directories, because at recipe install time the wheel is
    not importable by the build host interpreter;
  * a plugin path inside a known foreign QAIRT install (/usr/lib/npu-llm,
    /usr/lib/rfsa/adsp) is reported as a warning: the backend and the HTP skel
    must come from one release, and mixing 2.40 with 2.47 aborts the session.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

DEFAULT_ASSETS_ROOT = Path("/usr/share/qwen35")
DEFAULT_LIB_ROOT = Path("/usr/lib/qwen35")
DEFAULT_STATE_ROOT = Path("/var/lib/qwen35")
DEFAULT_PORT = 8000

# Values of assets/manifest.json "roles" for the groups we ship (common + 2k),
# copied verbatim from the pinned release
# PrismPhi/Qwen3.5-0.8B-Radxa-Dragon-Q6A-QCS6490-QNN-NPU @ 05de67d2.
# A real manifest can be supplied with --manifest; it wins over this table.
MANIFEST_ROLES = {
    "common": {
        "embedding": "common/embedding_fp32.npy",
        "tokenizer": "common/tokenizer.json",
        "vision_patch_model": "models/vision/patch_ctx.onnx",
        "vision_body_model": "models/vision/body_ctx.onnx",
        "vision_merger_model": "models/vision/merger_ctx.onnx",
    },
    "2k": {
        "decoder_model": "models/2k/decode_ctx.onnx",
        "prefill_model": "models/2k/prefill_ctx.onnx",
        "full_npu_tail_assembly_report": "models/2k/assembly_report.json",
        "rope": "contracts/2k/rope_ctx2048_cos_sin_fp32.bin",
        "tensor_contract": "contracts/2k/tensor_contract.tsv",
        "advance_plan": "contracts/2k/advance_plan.tsv",
    },
}

# Program-side paths, mirroring install.py's second cfg['paths'].update({...}).
LIB_NAMES = {
    "frontend_script": "request_frontend.py",
    "persistent_stage_worker": "stage_worker.py",
    "still_frontend_helper": "image_frontend.py",
    "vlm_bundle_helper": "prompt_bundle.py",
    # shims created by qwen35-launch.sh (upstream wrote them at install time)
    "python": "qwen-python",
    "decoder_engine": "engine.sh",
}

QNN_PLUGIN_GLOBS = (
    # What the onnxruntime-qnn wheel actually ships, and what the recipe keeps.
    # It is *not* named libonnxruntime_qnn.so, which is what upstream's install.py
    # used to look for (that name only exists in the Qualcomm "QNN EP" tarballs).
    "libonnxruntime_providers_qnn.so",
    "libonnxruntime_qnn.so",
    "libonnxruntime_qnn*.so*",
)
QNN_BACKEND_GLOBS = ("libQnnHtp.so", "libQnnHtpV68*.so", "libQnnHtp*.so")

# Directories that hold a *different* QAIRT release on this board image. Picking
# the backend from one of them while the skel comes from the wheel (or the other
# way round) is the 14001 / crash we already hit once.
FOREIGN_QAIRT_PREFIXES = ("/usr/lib/npu-llm", "/usr/lib/rfsa/adsp")

# The two three-line shims upstream install.py wrote into the install directory.
# qwen_server.py requires paths["python"] / paths["decoder_engine"] to be real
# files; qwen35-launch.sh can create them too, but shipping them here keeps
# /usr/lib/qwen35 read-only at runtime (see the unit's ProtectSystem=).
SHIM_PYTHON = '''#!/bin/sh
set -eu
export PYTHONDONTWRITEBYTECODE=1
exec "${QWEN35_PYTHON:-/usr/bin/python3}" "$@"
'''

SHIM_ENGINE = '''#!/bin/sh
# The whole server tree is already pinned to the big cores by launch.sh; the
# extra taskset only matters when the engine is started by hand. Upstream's
# trailing "arena_disjoint" argument is preserved.
set -eu
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if command -v taskset >/dev/null 2>&1; then
    exec taskset -c "${QWEN35_CPUS:-4-7}" "$DIR/qwen_text_engine" "$@" arena_disjoint
fi
exec "$DIR/qwen_text_engine" "$@" arena_disjoint
'''


class ConfigError(RuntimeError):
    pass


def find_first(directory: Path, patterns: tuple[str, ...]) -> Path | None:
    for pattern in patterns:
        hits = sorted(p for p in directory.glob(pattern) if p.is_file())
        if hits:
            return hits[0]
    return None


def search_dirs(args: argparse.Namespace) -> list[Path]:
    dirs: list[Path] = []
    dirs.extend(args.qnn_lib_dir or [])
    for env_name in ("QWEN35_QNN_DIR", "QNN_LIB_DIR"):
        value = os.environ.get(env_name)
        if value:
            dirs.extend(Path(p) for p in value.split(os.pathsep) if p)
    dirs.append(args.lib_root / "qnn")
    dirs.append(args.lib_root / "lib")
    dirs.append(Path("/usr/lib/qwen35/qnn"))
    # Debian/Qualcomm system locations, last resort
    dirs.append(Path("/usr/lib/aarch64-linux-gnu"))
    out: list[Path] = []
    for d in dirs:
        if d not in out:
            out.append(d)
    return out


def resolve_qnn(args: argparse.Namespace) -> tuple[Path, Path, str]:
    """Return (qnn_plugin, qnn_backend, source-description)."""
    if args.qnn_plugin and args.qnn_backend:
        return args.qnn_plugin, args.qnn_backend, "explicit arguments"

    env_plugin = os.environ.get("QWEN35_QNN_PLUGIN")
    env_backend = os.environ.get("QWEN35_QNN_HTP")
    if env_plugin and env_backend:
        return Path(env_plugin), Path(env_backend), "environment"

    try:  # the board-facing PyPI wheel; also importable in a staging venv
        import onnxruntime_qnn as qnn  # type: ignore[import-not-found]

        plugin = Path(qnn.get_library_path())
        backend = Path(qnn.get_qnn_htp_path())
        return plugin, backend, f"onnxruntime_qnn {getattr(qnn, '__version__', '?')}"
    except Exception:  # noqa: BLE001 - not importable on the build host
        pass

    for directory in search_dirs(args):
        if not directory.is_dir():
            continue
        plugin = find_first(directory, QNN_PLUGIN_GLOBS)
        backend = find_first(directory, QNN_BACKEND_GLOBS)
        if plugin is not None and backend is not None:
            return plugin, backend, f"directory search ({directory})"

    raise ConfigError(
        "cannot locate the QNN libraries (libonnxruntime_qnn*.so / libQnnHtp*.so).\n"
        "onnxruntime_qnn is not importable here; pass --qnn-plugin/--qnn-backend,\n"
        "set QWEN35_QNN_PLUGIN/QWEN35_QNN_HTP, or point --qnn-lib-dir at the directory\n"
        "the onnxruntime-qnn recipe installs into.\n"
        "searched: " + os.pathsep.join(str(d) for d in search_dirs(args))
    )


def build_config(args: argparse.Namespace) -> dict:
    cfg = json.loads(args.config.read_text())

    if args.manifest is not None:
        manifest = json.loads(args.manifest.read_text())
        role_map = {**manifest["roles"]["common"], **manifest["roles"][args.profile]}
    else:
        role_map = dict(MANIFEST_ROLES["common"])
        role_map.update(MANIFEST_ROLES[args.profile])

    paths: dict[str, str] = {
        key: str(args.assets_root / value) for key, value in role_map.items()
    }
    paths.update({key: str(args.lib_root / name) for key, name in LIB_NAMES.items()})
    paths["scratch_root"] = str(args.state_root / "scratch")
    paths["metrics_log"] = str(args.state_root / "logs" / "requests.jsonl")

    plugin, backend, source = resolve_qnn(args)
    paths["qnn_plugin"] = str(plugin)
    paths["qnn_backend"] = str(backend)
    for label, value in (("qnn_plugin", plugin), ("qnn_backend", backend)):
        for prefix in FOREIGN_QAIRT_PREFIXES:
            if str(value).startswith(prefix):
                print(f"qwen35-mkconfig: WARNING {label}={value} resolves into a foreign\n"
                      f"  QAIRT install ({prefix}). The HTP backend and its V68 skel must\n"
                      f"  come from ONE release; use the onnxruntime-qnn wheel's own copy\n"
                      f"  (expected under /usr/lib/qwen35/qnn).", file=sys.stderr)

    cfg["paths"] = paths
    cfg["port"] = args.port
    cfg.setdefault("host", "127.0.0.1")
    cfg.setdefault("model_id", "qwen3.5-0.8b-q6a-" + args.profile)
    if not 1024 <= cfg["port"] <= 65535:
        raise ConfigError(f"port {cfg['port']} is outside 1024..65535")

    if args.verbose:
        print(f"qwen35-mkconfig: qnn_plugin  = {plugin}  [{source}]", file=sys.stderr)
        print(f"qwen35-mkconfig: qnn_backend = {backend}", file=sys.stderr)
    return cfg


def write_shims(lib_root: Path) -> list[Path]:
    written = []
    for name, body in (("qwen-python", SHIM_PYTHON), ("engine.sh", SHIM_ENGINE)):
        path = lib_root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(body)
        path.chmod(0o755)
        written.append(path)
    return written


def check_files(cfg: dict) -> list[str]:
    skip = {"scratch_root", "metrics_log"}
    absent = []
    for key, value in sorted(cfg["paths"].items()):
        if key in skip:
            continue
        if not Path(value).is_file():
            absent.append(f"{key}={value}")
    return absent


def main(argv: list[str] | None = None) -> int:
    here = Path(__file__).resolve().parent
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--config", type=Path, default=here / "configs/2k.json",
                   help="upstream config template (default: <script dir>/configs/2k.json)")
    p.add_argument("--profile", choices=("2k", "4k"), default="2k")
    p.add_argument("--manifest", type=Path, default=None,
                   help="optional upstream assets/manifest.json (role map source)")
    p.add_argument("--assets-root", type=Path, default=DEFAULT_ASSETS_ROOT,
                   help=f"asset root, default {DEFAULT_ASSETS_ROOT}")
    p.add_argument("--lib-root", type=Path, default=DEFAULT_LIB_ROOT,
                   help=f"program root, default {DEFAULT_LIB_ROOT}")
    p.add_argument("--state-root", type=Path, default=DEFAULT_STATE_ROOT,
                   help=f"writable state root (scratch+logs), default {DEFAULT_STATE_ROOT}")
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("--qnn-plugin", type=Path, default=None,
                   help="libonnxruntime_qnn*.so (overrides onnxruntime_qnn lookup)")
    p.add_argument("--qnn-backend", type=Path, default=None,
                   help="libQnnHtp*.so (overrides onnxruntime_qnn lookup)")
    p.add_argument("--qnn-lib-dir", type=Path, action="append", default=None,
                   help="extra directory to search for the QNN libraries (repeatable)")
    p.add_argument("--out", type=Path, default=None,
                   help="output file (default: <lib-root>/runtime.json)")
    p.add_argument("--no-shims", action="store_true",
                   help="do not write qwen-python / engine.sh next to runtime.json")
    p.add_argument("--check-files", action="store_true",
                   help="fail when a configured file is absent (board-side check)")
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args(argv)

    try:
        cfg = build_config(args)
    except ConfigError as exc:
        print(f"qwen35-mkconfig: {exc}", file=sys.stderr)
        return 2

    if args.check_files:
        absent = check_files(cfg)
        if absent:
            print("qwen35-mkconfig: missing files:\n  " + "\n  ".join(absent), file=sys.stderr)
            return 3

    out = args.out or (args.lib_root / "runtime.json")
    payload = json.dumps(cfg, indent=2) + "\n"
    json.loads(payload)  # cheap self-check: never write unparsable JSON
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(payload)
    if not args.no_shims:
        for path in write_shims(args.lib_root):
            if args.verbose:
                print(f"qwen35-mkconfig: wrote {path}", file=sys.stderr)
    if args.verbose:
        print(f"qwen35-mkconfig: wrote {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
