#!/usr/bin/env python3
"""Persistent fixed-1024x576 QNN Vision runtime for the Q6A OpenAI server.

The public runtime uses three standard ONNX A16W8 QDQ graphs:

    pixel_values -> patch -> body12 -> merger -> image_embeddings

Every session disables CPU fallback.  The first load runs a zero-input startup
canary with ORT profiling enabled and refuses readiness unless every profiled
model node is assigned to QNNExecutionProvider.
"""

from __future__ import annotations

import json
import os
import time
from collections import Counter
from pathlib import Path
from typing import Any

import numpy as np
import onnxruntime as ort


PATCHES = 2304
PATCH_WIDTH = 1536
VISION_WIDTH = 768
IMAGE_TOKENS = 576
HIDDEN = 1024

_SESSION_KEY: tuple[str, str, str] | None = None
_SESSIONS: dict[str, ort.InferenceSession] | None = None
_STARTUP_REPORT: dict[str, Any] | None = None


def _resolved_key(
    patch_model: Path, body_model: Path, merger_model: Path
) -> tuple[str, str, str]:
    return tuple(
        str(path.resolve()) for path in (patch_model, body_model, merger_model)
    )


def _profile_counts(path: Path) -> dict[str, int]:
    counts: Counter[str] = Counter()
    events = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(events, list):
        raise RuntimeError(f"ORT profile is not an event array: {path}")
    for event in events:
        if not isinstance(event, dict):
            continue
        provider = (event.get("args") or {}).get("provider")
        if provider:
            counts[str(provider)] += 1
    return dict(sorted(counts.items()))


def _io_contract(session: ort.InferenceSession) -> dict[str, Any]:
    return {
        "inputs": [
            {"name": value.name, "shape": value.shape, "type": value.type}
            for value in session.get_inputs()
        ],
        "outputs": [
            {"name": value.name, "shape": value.shape, "type": value.type}
            for value in session.get_outputs()
        ],
        "providers": session.get_providers(),
    }


def _create_session(
    model: Path,
    component: str,
    profile_dir: Path,
    devices: list[Any],
    provider_options: dict[str, str],
) -> tuple[ort.InferenceSession, float]:
    options = ort.SessionOptions()
    options.log_severity_level = 2
    options.enable_cpu_mem_arena = False
    options.enable_mem_pattern = False
    options.intra_op_num_threads = 1
    options.inter_op_num_threads = 1
    options.add_session_config_entry("session.intra_op.allow_spinning", "0")
    options.enable_profiling = True
    options.profile_file_prefix = str(
        (profile_dir / f"qwen35_vision_{component}_profile").resolve()
    )
    options.add_session_config_entry("session.disable_cpu_ep_fallback", "1")
    options.add_session_config_entry("ep.context_enable", "0")
    options.add_session_config_entry("ep.context_embed_mode", "0")
    options.add_provider_for_devices(devices, provider_options)
    started = time.perf_counter()
    session = ort.InferenceSession(
        str(model.resolve()),
        sess_options=options,
        enable_fallback=False,
    )
    return session, time.perf_counter() - started


def load_qnn_vision_sessions(
    patch_model: Path,
    body_model: Path,
    merger_model: Path,
    profile_dir: Path,
) -> tuple[dict[str, ort.InferenceSession], bool, dict[str, Any]]:
    """Load and startup-validate the one allowed persistent Vision chain."""

    global _SESSION_KEY
    global _SESSIONS
    global _STARTUP_REPORT

    key = _resolved_key(patch_model, body_model, merger_model)
    if _SESSIONS is not None:
        if key != _SESSION_KEY or _STARTUP_REPORT is None:
            raise RuntimeError(
                "persistent QNN Vision session contract differs from this request"
            )
        return _SESSIONS, True, dict(_STARTUP_REPORT)

    models = {
        "patch": Path(key[0]),
        "body12": Path(key[1]),
        "merger": Path(key[2]),
    }
    for path in models.values():
        if not path.is_file():
            raise FileNotFoundError(path)
    profile_dir.mkdir(parents=True, exist_ok=True)

    import onnxruntime_qnn as oq

    os.environ["ADSP_LIBRARY_PATH"] = (
        f"{Path(oq.get_library_path()).parent};"
        "/usr/lib/dsp/cdsp;/usr/lib/dsp/adsp;/dsp"
    )
    try:
        ort.register_execution_provider_library(
            oq.get_ep_name(), oq.get_library_path()
        )
        register_status = "registered"
    except Exception as exc:
        # The persistent language-prefill loader may already have registered
        # the same provider in this process.  Device discovery below remains
        # the authoritative success gate.
        register_status = f"register_exception:{exc!r}"
    devices = [
        device
        for device in ort.get_ep_devices()
        if getattr(device, "ep_name", None) == oq.get_ep_name()
    ]
    if not devices:
        raise RuntimeError("no QNN OrtEpDevice for persistent Vision")
    provider_options = {
        "backend_path": str(oq.get_qnn_htp_path()),
        "htp_performance_mode": "burst",
    }

    sessions: dict[str, ort.InferenceSession] = {}
    create_seconds: dict[str, float] = {}
    for component, model in models.items():
        session, elapsed = _create_session(
            model, component, profile_dir, devices, provider_options
        )
        sessions[component] = session
        create_seconds[component] = elapsed

    contracts = {
        component: _io_contract(session)
        for component, session in sessions.items()
    }
    expected_contracts = {
        "patch": (
            [{"name": "pixel_values", "shape": [PATCHES, PATCH_WIDTH]}],
            [{"name": "patch_embed", "shape": [PATCHES, VISION_WIDTH]}],
        ),
        "body12": (
            [{"name": "patch_embed", "shape": [PATCHES, VISION_WIDTH]}],
            [{"name": "block11_hidden", "shape": [PATCHES, VISION_WIDTH]}],
        ),
        "merger": (
            [{"name": "vision_hidden", "shape": [PATCHES, VISION_WIDTH]}],
            [{"name": "image_embeddings", "shape": [IMAGE_TOKENS, HIDDEN]}],
        ),
    }
    io_exact = True
    for component, (expected_inputs, expected_outputs) in expected_contracts.items():
        contract = contracts[component]
        actual_inputs = [
            {"name": value["name"], "shape": value["shape"]}
            for value in contract["inputs"]
        ]
        actual_outputs = [
            {"name": value["name"], "shape": value["shape"]}
            for value in contract["outputs"]
        ]
        io_exact = (
            io_exact
            and actual_inputs == expected_inputs
            and actual_outputs == expected_outputs
        )

    warmup_seconds: dict[str, float] = {}
    started = time.perf_counter()
    patch = sessions["patch"].run(
        ["patch_embed"],
        {"pixel_values": np.zeros((PATCHES, PATCH_WIDTH), dtype=np.float32)},
    )[0]
    warmup_seconds["patch"] = time.perf_counter() - started
    started = time.perf_counter()
    hidden = sessions["body12"].run(
        ["block11_hidden"],
        {"patch_embed": np.ascontiguousarray(patch, dtype=np.float32)},
    )[0]
    warmup_seconds["body12"] = time.perf_counter() - started
    started = time.perf_counter()
    embeddings = sessions["merger"].run(
        ["image_embeddings"],
        {"vision_hidden": np.ascontiguousarray(hidden, dtype=np.float32)},
    )[0]
    warmup_seconds["merger"] = time.perf_counter() - started

    profiles: dict[str, str] = {}
    provider_counts: dict[str, dict[str, int]] = {}
    for component, session in sessions.items():
        profile = Path(session.end_profiling()).resolve()
        profiles[component] = str(profile)
        provider_counts[component] = _profile_counts(profile)
    cpu_assigned_nodes = {
        component: counts.get("CPUExecutionProvider", 0)
        for component, counts in provider_counts.items()
    }
    qnn_assigned_nodes = {
        component: counts.get("QNNExecutionProvider", 0)
        for component, counts in provider_counts.items()
    }
    gates = {
        "three_qnn_sessions": len(sessions) == 3,
        "static_io_contract_exact": io_exact,
        "fallback_disabled": True,
        "qnn_node_present_in_every_component": all(
            qnn_assigned_nodes[component] > 0
            for component in ("patch", "body12", "merger")
        ),
        "cpu_assigned_nodes_zero_in_every_component": all(
            cpu_assigned_nodes[component] == 0
            for component in ("patch", "body12", "merger")
        ),
        "only_qnn_provider_in_every_profile": all(
            set(provider_counts[component]) <= {"QNNExecutionProvider"}
            for component in ("patch", "body12", "merger")
        ),
        "warmup_shapes_exact": (
            list(patch.shape) == [PATCHES, VISION_WIDTH]
            and list(hidden.shape) == [PATCHES, VISION_WIDTH]
            and list(embeddings.shape) == [IMAGE_TOKENS, HIDDEN]
        ),
        "warmup_outputs_finite": bool(
            np.isfinite(patch).all()
            and np.isfinite(hidden).all()
            and np.isfinite(embeddings).all()
        ),
    }
    gates["all_pass"] = all(gates.values())
    startup_report = {
        "schema": "qwen35_q6a_persistent_vision_startup_v1",
        "status": "pass" if gates["all_pass"] else "fail",
        "backend": "qnn_htp_fixed1024x576_three_stage",
        "register_status": register_status,
        "provider_options": provider_options,
        "models": {
            component: {
                "path": str(model),
                "bytes": model.stat().st_size,
            }
            for component, model in models.items()
        },
        "session_create_seconds": {
            **create_seconds,
            "total": sum(create_seconds.values()),
        },
        "warmup_run_seconds": {
            **warmup_seconds,
            "total": sum(warmup_seconds.values()),
        },
        "io_contracts": contracts,
        "profiles": profiles,
        "provider_counts": provider_counts,
        "cpu_assigned_nodes": cpu_assigned_nodes,
        "qnn_assigned_nodes": qnn_assigned_nodes,
        "gates": gates,
    }
    if not gates["all_pass"]:
        raise RuntimeError(
            "persistent QNN Vision startup validation failed: "
            + json.dumps(gates, sort_keys=True)
        )

    _SESSION_KEY = key
    _SESSIONS = sessions
    _STARTUP_REPORT = startup_report
    return sessions, False, dict(startup_report)


def run_qnn_vision_chain(
    sessions: dict[str, ort.InferenceSession],
    pixel_values: np.ndarray,
) -> tuple[np.ndarray, dict[str, float]]:
    """Run one fixed-1024x576 image through the validated three-stage chain."""

    pixels = np.ascontiguousarray(pixel_values, dtype=np.float32)
    if pixels.shape != (PATCHES, PATCH_WIDTH) or not np.isfinite(pixels).all():
        raise RuntimeError("QNN Vision input violates the fixed1024x576 contract")
    timings: dict[str, float] = {}
    started = time.perf_counter()
    patch = sessions["patch"].run(
        ["patch_embed"], {"pixel_values": pixels}
    )[0]
    timings["patch"] = time.perf_counter() - started
    started = time.perf_counter()
    hidden = sessions["body12"].run(
        ["block11_hidden"],
        {"patch_embed": np.ascontiguousarray(patch, dtype=np.float32)},
    )[0]
    timings["body12"] = time.perf_counter() - started
    started = time.perf_counter()
    embeddings = sessions["merger"].run(
        ["image_embeddings"],
        {"vision_hidden": np.ascontiguousarray(hidden, dtype=np.float32)},
    )[0]
    timings["merger"] = time.perf_counter() - started
    timings["total"] = sum(timings.values())
    result = np.ascontiguousarray(embeddings, dtype="<f4")
    if result.shape != (IMAGE_TOKENS, HIDDEN) or not np.isfinite(result).all():
        raise RuntimeError("QNN Vision output violates the image embedding contract")
    return result, timings
