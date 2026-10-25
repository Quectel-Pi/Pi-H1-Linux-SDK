#!/usr/bin/env python3
"""Resident Vision/frontend worker for the OpenAI service.

The accepted runtime scripts remain the implementation of each stage.  This worker
keeps their Python imports, the three-stage all-QNN Vision chain, and the
all-QNN Chunk16 prefill session resident, then replays each script's CLI main
function with request-local arguments and logs.  The R30 full-NPU mode omits
the four legacy CPU-tail sessions because Layers20-23 are inside prefill.
"""

from __future__ import annotations

import argparse
import contextlib
import cProfile
import pstats
import resource
import importlib.util
import json
import os
import shutil
import sys
import time
import traceback
from pathlib import Path
from types import ModuleType
from typing import Any

import numpy as np


def load_module(name: str, path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


_PROTOCOL = None

def emit(value: dict[str, Any]) -> None:
    print(json.dumps(value, ensure_ascii=False, separators=(",", ":")), file=_PROTOCOL, flush=True)


def main() -> int:
    global _PROTOCOL
    _PROTOCOL = os.fdopen(os.dup(1), "w", buffering=1, encoding="utf-8")
    os.dup2(2, 1)
    os.environ["QWEN35_MEMOIZE_HASHES"] = "1"
    os.environ["QWEN35_STATE_HANDOFF"] = "1"
    from state_handoff import clear_arrays, pending_count
    os.environ.setdefault("QWEN35_RUNTIME_FAST_NPZ", "1")
    parser = argparse.ArgumentParser()
    parser.add_argument("--frontend-script", type=Path, required=True)
    parser.add_argument("--tokenizer", type=Path, required=True)
    parser.add_argument("--prefill-model", type=Path, required=True)
    parser.add_argument("--embedding", type=Path, required=True)
    parser.add_argument("--rope", type=Path, required=True)
    parser.add_argument(
        "--full-npu-tail-assembly-report", type=Path
    )
    parser.add_argument("--full-npu-prefill", action="store_true")
    parser.add_argument("--prefill-warmup-dir", type=Path, required=True)
    parser.add_argument("--vision-warmup-dir", type=Path, required=True)
    parser.add_argument("--vision-patch-model", type=Path, required=True)
    parser.add_argument("--vision-body-model", type=Path, required=True)
    parser.add_argument("--vision-merger-model", type=Path, required=True)
    parser.add_argument("--threads", type=int, default=4)
    args = parser.parse_args()
    for path in (
        args.frontend_script,
        args.tokenizer,
        args.prefill_model,
        args.embedding,
        args.rope,
        args.vision_patch_model,
        args.vision_body_model,
        args.vision_merger_model,
    ):
        if not path.is_file():
            raise FileNotFoundError(path)
    if args.full_npu_prefill:
        if (
            args.full_npu_tail_assembly_report is None
            or not args.full_npu_tail_assembly_report.is_file()
        ):
            raise FileNotFoundError(
                "full-NPU prefill requires its tail assembly report"
            )
    elif args.full_npu_tail_assembly_report is not None:
        raise ValueError(
            "--full-npu-tail-assembly-report requires --full-npu-prefill"
        )
    if args.threads < 1:
        raise ValueError("threads must be positive")

    startup = time.perf_counter()
    frontend = load_module("worker_frontend", args.frontend_script)
    tokenizer_started = time.perf_counter()
    frontend.load_tokenizer(args.tokenizer)
    tokenizer_create = time.perf_counter() - tokenizer_started
    vision_warmup_dir = args.vision_warmup_dir.resolve()
    allowed = (Path(__file__).resolve().parent / "scratch").resolve()
    if not vision_warmup_dir.is_relative_to(allowed) or not vision_warmup_dir.name.startswith("warmup_vision_"):
        raise ValueError("warmup must stay inside the new layered scratch directory")
    vision_warmup_dir.mkdir(parents=True, exist_ok=False)
    _, vision_cache_hit, vision_startup = frontend.load_qnn_vision_sessions(
        args.vision_patch_model,
        args.vision_body_model,
        args.vision_merger_model,
        vision_warmup_dir,
    )
    if vision_cache_hit or vision_startup.get("status") != "pass":
        raise RuntimeError("persistent QNN Vision startup cache contract failed")

    modules = {"frontend": frontend}
    from host_memory import release_startup_heap
    heap_release = release_startup_heap()
    emit(
        {
            "type": "ready",
            "startup_heap_release": heap_release,
            "stages": sorted(modules),
            "startup_seconds": time.perf_counter() - startup,
            "tokenizer_create_seconds": tokenizer_create,
            "tail_session_create_seconds": {},
            "full_npu_prefill": args.full_npu_prefill,
            "tail_backend": (
                "qnn_htp_inside_full24_prefill"
                if args.full_npu_prefill
                else "cpu_fp32_four_sessions"
            ),
            "vision_backend": vision_startup.get("backend"),
            "vision_session_create_seconds": vision_startup.get(
                "session_create_seconds"
            ),
            "vision_warmup_run_seconds": vision_startup.get(
                "warmup_run_seconds"
            ),
            "vision_provider_counts": vision_startup.get("provider_counts"),
            "vision_cpu_assigned_nodes": vision_startup.get(
                "cpu_assigned_nodes"
            ),
            "vision_qnn_assigned_nodes": vision_startup.get(
                "qnn_assigned_nodes"
            ),
            "vision_startup_gates": vision_startup.get("gates"),
            "prefill_owner": "resident_native_decoder",
        }
    )

    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        request_id = None
        try:
            command = json.loads(raw)
            request_id = command.get("id")
            if command.get("command") == "shutdown":
                emit({"type": "shutdown", "id": request_id, "status": "ok"})
                return 0
            stage = command.get("stage")
            if stage not in modules:
                raise ValueError(f"unsupported persistent stage: {stage}")
            argv = command.get("argv")
            if not isinstance(argv, list) or not all(isinstance(value, str) for value in argv):
                raise ValueError("argv must be an array of strings")
            stdout_path = Path(command["stdout_path"])
            stderr_path = Path(command["stderr_path"])
            started = time.perf_counter()

            if stage in ("frontend", "npu_prefill"):
                clear_arrays()
            previous_argv = sys.argv
            try:
                sys.argv = [str(getattr(modules[stage], "__file__", stage)), *argv]
                with stdout_path.open("x", encoding="utf-8", newline="\n") as stdout, stderr_path.open(
                    "x", encoding="utf-8", newline="\n"
                ) as stderr, contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                    profiler = cProfile.Profile() if os.environ.get("QWEN35_FRONTEND_PROFILE") == "1" else None
                    cpu_started = time.process_time()
                    wall_started = time.perf_counter()
                    if profiler is not None:
                        profiler.enable()
                    try:
                        returncode = int(modules[stage].main())
                    finally:
                        if profiler is not None:
                            profiler.disable()
                            elapsed = time.perf_counter() - wall_started
                            cpu_elapsed = time.process_time() - cpu_started
                            stats = pstats.Stats(profiler).stats
                            profile_rows = [{"file": key[0], "line": key[1], "function": key[2],
                                             "primitive_calls": v[0], "calls": v[1],
                                             "self_seconds": v[2], "cumulative_seconds": v[3]}
                                            for key, v in stats.items()]
                            profile_rows.sort(key=lambda row: -row["self_seconds"])
                            payload = {"stage": stage, "cpu_seconds": cpu_elapsed,
                                       "wall_seconds": elapsed, "cpu_core_equivalent": cpu_elapsed / max(elapsed, 1e-9),
                                       "fast_npz": os.environ.get("QWEN35_RUNTIME_FAST_NPZ"),
                                       "top_functions": profile_rows[:80]}
                            profile_root = Path(os.environ["QWEN35_FRONTEND_PROFILE_DIR"])
                            allowed_profile_root = (Path(__file__).resolve().parent / "evidence").resolve()
                            if not profile_root.resolve().is_relative_to(allowed_profile_root):
                                raise ValueError("profile output outside Frontend evidence")
                            profile_root.mkdir(parents=True, exist_ok=True)
                            with (profile_root / (str(time.time_ns()) + "_" + stage + ".json")).open("x", encoding="utf-8") as profile_file:
                                json.dump(payload, profile_file, indent=2)
                                profile_file.write("\n")
            finally:
                sys.argv = previous_argv
                if stage == "seed_build":
                    clear_arrays()
            emit(
                {
                    "type": "result",
                    "id": request_id,
                    "stage": stage,
                    "state_handoff_pending": pending_count(),
                    "returncode": returncode,
                    "elapsed_seconds": time.perf_counter() - started,
                }
            )
        except Exception as exc:
            clear_arrays()
            try:
                if isinstance(command, dict) and command.get("stderr_path"):
                    with Path(command["stderr_path"]).open(
                        "a", encoding="utf-8", newline="\n"
                    ) as stderr:
                        traceback.print_exc(file=stderr)
            except Exception:
                pass
            emit(
                {
                    "type": "result",
                    "id": request_id,
                    "returncode": 2,
                    "error_type": type(exc).__name__,
                    "error": str(exc),
                }
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
