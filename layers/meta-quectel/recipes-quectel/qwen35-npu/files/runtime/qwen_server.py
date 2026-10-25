#!/usr/bin/env python3
"""Local OpenAI-compatible Text/Image server for the Q6A Frontend runtime.

The three-stage Vision network, all24 language transformer layers, and the
resident P5 suffix/decoder/head use the standard QNN HTP execution provider.
Tokenization, token embedding lookup, image preprocessing and state transport
are host preparation; no ONNX model operator falls back to the CPU provider.
Requests are serialized. Transient uncompressed state files live in private
tmpfs; request metrics remain in the project. The configured image input order
and optional default instruction are reported, and explicit system messages
are preserved.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import gc
import hmac
import json
import os
import queue
import re
import shutil
import signal
import subprocess
import threading
import time
import tempfile
import traceback
import uuid
from dataclasses import dataclass
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable

import numpy as np
from tokenizers import Tokenizer

from request_frontend import IMAGE_TOKEN_ID, render_messages


CONTEXT = 2048
CHUNK = 16
STOP_IDS = (248044, 248046)
DATA_IMAGE_RE = re.compile(
    r"^data:(image/(?:png|jpeg|jpg|webp));base64,(.+)$", re.IGNORECASE | re.DOTALL
)


class RequestError(Exception):
    def __init__(
        self,
        message: str,
        code: str = "invalid_request",
        status: int = HTTPStatus.BAD_REQUEST,
        param: str | None = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.status = int(status)
        self.param = param


class PipelineError(RuntimeError):
    def __init__(self, stage: str, message: str) -> None:
        super().__init__(f"{stage}: {message}")
        self.stage = stage


class ClientDisconnected(RuntimeError):
    """Raised when an SSE consumer closes before generation completes."""


class PersistentStageWorker:
    STAGES = frozenset({"frontend"})

    def __init__(
        self,
        python: Path,
        worker_script: Path,
        paths: dict[str, Path],
        threads: int,
        timeout: int,
        full_npu_prefill: bool,
    ) -> None:
        self.timeout = timeout
        self.lock = threading.Lock()
        self.output_queue: queue.Queue[str | None] = queue.Queue()
        argv = [
            str(python),
            str(worker_script),
            "--frontend-script",
            str(paths["frontend_script"]),
            "--tokenizer",
            str(paths["tokenizer"]),
            "--prefill-model",
            str(paths["prefill_model"]),
            "--embedding",
            str(paths["embedding"]),
            "--rope",
            str(paths["rope"]),
            "--prefill-warmup-dir",
            str(paths["scratch_root"] / f"warmup_prefill_{os.getpid()}"),
            "--vision-warmup-dir",
            str(paths["scratch_root"] / f"warmup_vision_{os.getpid()}"),
            "--vision-patch-model",
            str(paths["vision_patch_model"]),
            "--vision-body-model",
            str(paths["vision_body_model"]),
            "--vision-merger-model",
            str(paths["vision_merger_model"]),
            "--threads",
            str(threads),
        ]
        if full_npu_prefill:
            argv.extend(
                [
                    "--full-npu-prefill",
                    "--full-npu-tail-assembly-report",
                    str(paths["full_npu_tail_assembly_report"]),
                ]
            )
        self.process = subprocess.Popen(
            argv,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        assert self.process.stdin is not None and self.process.stdout is not None

        def reader() -> None:
            try:
                for line in self.process.stdout:
                    self.output_queue.put(line)
            finally:
                self.output_queue.put(None)

        self.reader_thread = threading.Thread(target=reader, daemon=True)
        self.reader_thread.start()
        ready = self._next_record(timeout)
        if ready.get("type") != "ready" or set(ready.get("stages", [])) != self.STAGES:
            self.close(force=True)
            raise RuntimeError(f"persistent stage worker failed readiness: {ready}")
        self.startup = ready

    def _next_record(self, timeout: float) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("persistent stage worker response timed out")
            try:
                line = self.output_queue.get(timeout=remaining)
            except queue.Empty as exc:
                raise TimeoutError("persistent stage worker response timed out") from exc
            if line is None:
                raise RuntimeError(
                    f"persistent stage worker exited with code {self.process.poll()}"
                )
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                # QNN/FastRPC native libraries can write diagnostics directly to
                # the inherited stdout file descriptor.  They are not worker
                # protocol records and must not take down the product service.
                continue
            if not isinstance(value, dict):
                continue
            return value

    def run(
        self,
        stage: str,
        argv: list[str],
        stdout_path: Path,
        stderr_path: Path,
    ) -> float:
        if stage not in self.STAGES:
            raise ValueError(f"stage is not persistent: {stage}")
        request_id = uuid.uuid4().hex
        command = {
            "command": "run",
            "id": request_id,
            "stage": stage,
            "argv": argv,
            "stdout_path": str(stdout_path.resolve()),
            "stderr_path": str(stderr_path.resolve()),
        }
        with self.lock:
            if self.process.poll() is not None:
                raise RuntimeError("persistent stage worker is not running")
            assert self.process.stdin is not None
            self.process.stdin.write(json_line(command) + "\n")
            self.process.stdin.flush()
            result = self._next_record(self.timeout)
        if result.get("id") != request_id or result.get("type") != "result":
            raise RuntimeError(f"persistent stage worker protocol mismatch: {result}")
        if result.get("returncode") != 0:
            detail = tail_text(stderr_path) or tail_text(stdout_path) or str(result)
            raise PipelineError(stage, detail)
        return float(result["elapsed_seconds"])

    def alive(self) -> bool:
        return self.process.poll() is None

    def close(self, force: bool = False) -> None:
        if self.process.poll() is not None:
            return
        if not force:
            try:
                assert self.process.stdin is not None
                self.process.stdin.write(
                    json_line({"command": "shutdown", "id": uuid.uuid4().hex}) + "\n"
                )
                self.process.stdin.flush()
                self.process.wait(timeout=5)
                return
            except Exception:
                pass
        self.process.kill()
        self.process.wait(timeout=5)


class PersistentDecoder:
    def __init__(
        self,
        paths: dict[str, Path],
        log_path: Path,
        timeout: int,
    ) -> None:
        self.timeout = timeout
        self.lock = threading.Lock()
        self.output_queue: queue.Queue[str | None] = queue.Queue()
        log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log_path = log_path
        self.stderr_stream = log_path.open("a", encoding="utf-8", newline="\n")
        environment = os.environ.copy()
        environment["ADSP_LIBRARY_PATH"] = (
            f"{paths['qnn_plugin'].parent};/usr/lib/dsp/cdsp;/usr/lib/dsp/adsp;/dsp"
        )
        environment["QWEN35_SHARED_PREFILL_MODEL"] = str(paths["prefill_model"])
        if "logit_correction_model" in paths:
            environment["QWEN35_LOGIT_CORRECTION_MODEL"] = str(paths["logit_correction_model"])
        argv = [
            str(paths["decoder_engine"]),
            str(paths["decoder_model"]),
            str(paths["qnn_plugin"]),
            str(paths["qnn_backend"]),
            str(paths["tensor_contract"]),
            str(paths["advance_plan"]),
            str(paths["embedding"]),
            str(paths["rope"]),
        ]
        self.process = subprocess.Popen(
            argv,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.stderr_stream,
            text=True,
            bufsize=1,
            env=environment,
        )
        assert self.process.stdin is not None and self.process.stdout is not None

        def reader() -> None:
            try:
                for line in self.process.stdout:
                    self.output_queue.put(line)
            finally:
                self.output_queue.put(None)

        self.reader_thread = threading.Thread(target=reader, daemon=True)
        self.reader_thread.start()
        ready = self._next_record(timeout)
        if (
            ready.get("type") != "ready"
            or ready.get("persistent_session") is not True
            or ready.get("fallback_disabled") is not True
            or ready.get("context") != CONTEXT
            or ready.get("shared_prefill") is not True
            or bool(ready.get("logit_correction_enabled")) != ("logit_correction_model" in paths)
        ):
            self.close(force=True)
            raise RuntimeError(f"persistent decoder failed readiness: {ready}")
        self.startup = ready

    def _next_record(self, timeout: float) -> dict[str, Any]:
        try:
            line = self.output_queue.get(timeout=timeout)
        except queue.Empty as exc:
            raise TimeoutError("persistent decoder response timed out") from exc
        if line is None:
            raise RuntimeError(f"persistent decoder exited with code {self.process.poll()}")
        try:
            value = json.loads(line)
        except json.JSONDecodeError as exc:
            raise RuntimeError("persistent decoder emitted invalid JSON") from exc
        if not isinstance(value, dict):
            raise RuntimeError("persistent decoder record must be an object")
        return value

    def prefill(self, request_id, prefix, bundle_path, output_path):
        if any(c in str(bundle_path) for c in "\t\r\n"):
            raise PipelineError("npu_prefill", "invalid bundle path")
        command = "\t".join(["PREFILL_V1", request_id, str(prefix), str(bundle_path), "-"])
        with self.lock:
            if self.process.poll() is not None:
                raise PipelineError("npu_prefill", "native process is not running")
            self.process.stdin.write(command + "\n")
            self.process.stdin.flush()
            record = self._next_record(self.timeout)
        with output_path.open("x", encoding="utf-8") as stream:
            json.dump(record, stream, indent=2)
        if (record.get("type") != "prefill_done" or record.get("request_id") != request_id
                or record.get("prefix_tokens") != prefix
                or record.get("resident_seed_tag") != "shared-resident:" + request_id
                or record.get("all_finite") is not True
                or record.get("fallback_disabled") is not True
                or record.get("session_reused") is not True):
            raise PipelineError("npu_prefill", f"native Prefill failed: {record}")
        return record

    def run(
        self,
        command: str,
        engine_steps: int,
        remainder_mode: bool,
        first_token: int | None,
        request_id: str,
        stdout_path: Path,
        stderr_path: Path,
        on_token: Callable[[int], None],
        event_mode: str = "seeded",
    ) -> dict[str, Any]:
        if event_mode not in {"seeded", "full_prompt"}:
            raise ValueError(f"unknown decoder event mode: {event_mode}")
        if engine_steps == 0:
            if first_token is None:
                raise PipelineError("decoder", "zero engine steps without an endpoint token")
            on_token(first_token)
            return {
                "ready": {
                    **self.startup,
                    "session_create_seconds": 0.0,
                    "startup_session_create_seconds": self.startup.get(
                        "session_create_seconds"
                    ),
                },
                "done": None,
                "engine_wall_seconds": 0.0,
                "finish_reason": "length",
                "engine_tokens": 0,
                "remainder_mode": remainder_mode,
                "stderr": "",
                "persistent": True,
            }

        started = time.perf_counter()
        token_count = 0
        done: dict[str, Any] | None = None
        stderr_path.write_text(
            f"persistent decoder stderr: {self.log_path}\n", encoding="utf-8"
        )
        with self.lock, stdout_path.open("x", encoding="utf-8", newline="\n") as stdout:
            if self.process.poll() is not None:
                raise PipelineError("decoder", "persistent decoder is not running")
            assert self.process.stdin is not None
            self.process.stdin.write(command + "\n")
            self.process.stdin.flush()
            if first_token is not None:
                on_token(first_token)
            deadline = time.monotonic() + self.timeout
            while done is None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(self.process.args, self.timeout)
                record = self._next_record(remaining)
                stdout.write(json_line(record) + "\n")
                stdout.flush()
                if record.get("request_id") != request_id:
                    raise PipelineError("decoder", f"unexpected decoder record: {record}")
                kind = record.get("type")
                token_kind = "token" if event_mode == "full_prompt" else "seeded_token"
                done_kind = "done" if event_mode == "full_prompt" else "seeded_done"
                if kind == token_kind:
                    token_id = int(record["token_id"])
                    if not 0 <= token_id < 248320:
                        raise PipelineError("decoder", "generated token is outside vocabulary")
                    token_count += 1
                    on_token(token_id)
                elif kind == done_kind:
                    done = record
                elif kind == "error":
                    raise PipelineError("decoder", str(record.get("error", "engine error")))
                else:
                    raise PipelineError("decoder", f"unexpected decoder event: {kind}")
        if event_mode == "seeded":
            if done.get("state_reset_after_request") is not True:
                raise PipelineError("decoder", "persistent decoder did not reset state")
        elif (
            done.get("state_reset_before_request") is not True
            or done.get("all_logits_finite") is not True
        ):
            raise PipelineError("decoder", "full-prompt decoder state/finite contract failed")
        if token_count <= 0 or token_count > engine_steps:
            raise PipelineError("decoder", "generated token count is invalid")
        startup_create = self.startup.get("session_create_seconds")
        return {
            "ready": {
                **self.startup,
                "session_create_seconds": 0.0,
                "startup_session_create_seconds": startup_create,
            },
            "done": done,
            "engine_wall_seconds": time.perf_counter() - started,
            "finish_reason": str(done.get("finish_reason", "length")),
            "engine_tokens": token_count,
            "remainder_mode": remainder_mode,
            "event_mode": event_mode,
            "stderr": tail_text(self.log_path),
            "persistent": True,
        }

    def alive(self) -> bool:
        return self.process.poll() is None

    def close(self, force: bool = False) -> None:
        if self.process.poll() is None and not force:
            try:
                assert self.process.stdin is not None
                self.process.stdin.write("SHUTDOWN\n")
                self.process.stdin.flush()
                shutdown = self._next_record(5)
                if shutdown.get("type") != "shutdown" or shutdown.get("status") != "ok":
                    raise RuntimeError(f"decoder shutdown mismatch: {shutdown}")
                self.process.wait(timeout=5)
            except Exception:
                force = True
        if self.process.poll() is None and force:
            self.process.kill()
            self.process.wait(timeout=5)
        self.stderr_stream.close()


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise PipelineError("artifact_validation", f"JSON object required: {path.name}")
    return value


def json_line(value: dict[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def error_body(error: RequestError | Exception) -> dict[str, Any]:
    if isinstance(error, RequestError):
        message = str(error)
        code = error.code
        param = error.param
        error_type = "invalid_request_error"
    else:
        message = f"generation failed: {error}"
        code = "generation_error"
        param = None
        error_type = "server_error"
    return {
        "error": {
            "message": message,
            "type": error_type,
            "param": param,
            "code": code,
        }
    }


def tail_text(path: Path, limit: int = 3000) -> str:
    if not path.is_file():
        return ""
    value = path.read_text(encoding="utf-8", errors="replace")
    return value[-limit:].strip()


def require_clean_report(stage: str, path: Path) -> dict[str, Any]:
    report = read_json(path)
    gates = report.get("gates")
    if (
        report.get("status") != "pass"
        or not isinstance(gates, dict)
        or gates.get("all_pass") is not True
    ):
        raise PipelineError(stage, "runtime report did not pass all gates")
    return report


def decode_visible(tokenizer: Tokenizer, token_ids: list[int]) -> str:
    return tokenizer.decode(token_ids, skip_special_tokens=True).replace("\ufffd", "")


def validate_number(
    request: dict[str, Any], name: str, minimum: float, maximum: float
) -> float | None:
    value = request.get(name)
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RequestError(f"{name} must be a number", param=name)
    result = float(value)
    if not minimum <= result <= maximum:
        raise RequestError(f"{name} must be in [{minimum}, {maximum}]", param=name)
    return result


@dataclass(frozen=True)
class PreparedRequest:
    messages: list[dict[str, Any]]
    image_path: Path | None
    mode: str
    max_tokens: int
    requested_decoding: dict[str, Any]
    tools_ignored: bool
    context_management: dict[str, Any]


class MetricsStore:
    def __init__(self, model_id: str, log_path: Path) -> None:
        self.model_id = model_id
        self.log_path = log_path
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.started_at = time.time()
        self.lock = threading.Lock()
        self.completed = 0
        self.failed = 0
        self.active = 0
        self.last_request: dict[str, Any] | None = None

    def set_active(self, active: int) -> None:
        with self.lock:
            self.active = active

    def record(self, value: dict[str, Any]) -> None:
        safe = dict(value)
        safe["recorded_at_unix"] = time.time()
        with self.lock:
            if safe.get("status") == "pass":
                self.completed += 1
            else:
                self.failed += 1
            self.last_request = safe
            with self.log_path.open("a", encoding="utf-8", newline="\n") as stream:
                stream.write(json.dumps(safe, ensure_ascii=False) + "\n")

    def snapshot(self, ready: bool) -> dict[str, Any]:
        with self.lock:
            return {
                "schema": "qwen35_q6a_openai_metrics_v1",
                "model": self.model_id,
                "ready": ready,
                "uptime_seconds": round(time.time() - self.started_at, 3),
                "requests_completed": self.completed,
                "requests_failed": self.failed,
                "active_requests": self.active,
                "residency_policy": "all_resident_single_common_p5",
            "language_session_group": "standard_qnn_ep_same_process",
            "state_handoff": "native_resident_arena_v1",
            "retained_hidden_chunks": 1,
            "decoder_model_count": 1,
            "queue_serialized": True,
                "last_request": self.last_request,
            }


class Chunk16Engine:
    def __init__(self, config_path: Path) -> None:
        self.config_path = config_path.resolve()
        self.config = read_json(self.config_path)
        self.model_id = str(self.config.get("model_id", "qwen3.5-0.8b-q6a-npu"))
        self.host = str(self.config.get("host", "127.0.0.1"))
        self.port = int(self.config.get("port", 18084))
        self.api_key_env = str(self.config.get("api_key_env", "QWEN35_API_KEY"))
        self.api_key = os.environ.get(self.api_key_env, "")
        self.limits = dict(self.config.get("limits") or {})
        self.runtime = dict(self.config.get("runtime") or {})
        self.paths = {
            key: Path(value)
            for key, value in dict(self.config.get("paths") or {}).items()
        }
        self.full_npu_prefill = bool(
            self.runtime.get("full_npu_prefill", False)
        )
        self._validate_config()
        self.python = self.paths["python"]
        self.scratch_root = self.paths["scratch_root"]
        self.scratch_root.mkdir(parents=True, exist_ok=True)
        self.request_scratch_root = self.scratch_root
        if self.runtime.get("frontend_tmpfs_requests",False):
            shm = Path("/dev/shm")
            if not shm.is_dir() or shutil.disk_usage(shm).free < 768*1024*1024:
                raise RuntimeError("Frontend request scratch requires768MiB free tmpfs")
            self.request_scratch_root = Path(tempfile.mkdtemp(prefix="qwen_q6a_requests_",dir=shm))
        self.tokenizer = Tokenizer.from_file(str(self.paths["tokenizer"]))
        self.generation_lock = threading.Lock()
        self.ready = True
        self.metrics = MetricsStore(self.model_id, self.paths["metrics_log"])
        self.default_max_tokens = int(self.limits.get("default_max_tokens", 128))
        self.max_tokens = int(self.limits.get("max_tokens", 512))
        self.max_request_bytes = int(self.limits.get("max_request_bytes", 12_000_000))
        self.max_image_bytes = int(self.limits.get("max_image_bytes", 8_000_000))
        self.stage_timeout = int(self.runtime.get("stage_timeout_seconds", 300))
        self.decoder_timeout = int(self.runtime.get("decoder_timeout_seconds", 300))
        self.threads = int(self.runtime.get("cpu_threads", 4))
        legacy_repair_tokens = int(self.runtime.get("p5_repair_tokens", 0))
        self.p5_repair_tokens_text = int(
            self.runtime.get("p5_repair_tokens_text", legacy_repair_tokens)
        )
        self.p5_repair_tokens_image = int(
            self.runtime.get("p5_repair_tokens_image", legacy_repair_tokens)
        )
        self.full_p5_text_max_prompt_tokens = int(
            self.runtime.get("full_p5_text_max_prompt_tokens", 0)
        )
        self.default_repetition_penalty = float(
            self.runtime.get("default_repetition_penalty", 1.0)
        )
        self.max_p5_remainder_tokens = max(
            CHUNK - 1,
            self.p5_repair_tokens_text + CHUNK - 1,
            self.p5_repair_tokens_image + CHUNK - 1,
        )
        self.persistent_decoder_enabled = bool(
            self.runtime.get("persistent_decoder", True)
        )
        self._validate_limits()
        self.decoder_restart_count = 0
        self.last_decoder_restart_seconds: float | None = None
        self.persistent_decoder = None
        try:
            self.persistent_decoder = (
                PersistentDecoder(
                    self.paths,
                    self.paths["metrics_log"].parent / "persistent_decoder.stderr.log",
                    self.decoder_timeout,
                )
                if self.persistent_decoder_enabled
                else None
            )
            self.stage_worker = PersistentStageWorker(
                self.python,
                self.paths["persistent_stage_worker"],
                self.paths,
                self.threads,
                self.stage_timeout,
                self.full_npu_prefill,
            )
        except BaseException:
            if self.persistent_decoder is not None:
                self.persistent_decoder.close(force=True)
            if self.request_scratch_root != self.scratch_root:
                self.request_scratch_root.rmdir()
            raise
        self.started_at = time.time()

    def _validate_config(self) -> None:
        if not self.runtime.get("persistent_decoder") or not self.runtime.get("shared_native_resident_prefill"):
            raise ValueError("Shared requires resident native Prefill and Decode")
        if "image_decoder_model" in self.paths:
            raise ValueError("this runtime uses exactly one common P5 model")
        if not self.full_npu_prefill:
            raise ValueError("Frontend precision requires full_npu_prefill=true; learned CPU execution is disabled")
        if not isinstance(self.runtime.get("image_system_prompt", ""), str):
            raise ValueError("runtime.image_system_prompt must be a string")
        if self.host not in {"127.0.0.1", "localhost", "::1"}:
            raise ValueError("the Q6A service must bind to loopback")
        if not 1 <= self.port <= 65535:
            raise ValueError("port must be in [1, 65535]")
        required = {
            "python",
            "frontend_script",
            "persistent_stage_worker",
            "still_frontend_helper",
            "vlm_bundle_helper",
            "tokenizer",
            "embedding",
            "rope",
            "vision_patch_model",
            "vision_body_model",
            "vision_merger_model",
            "prefill_model",
            "decoder_engine",
            "decoder_model",
            "qnn_plugin",
            "qnn_backend",
            "tensor_contract",
            "advance_plan",
            "scratch_root",
            "metrics_log",
        }
        if self.full_npu_prefill:
            required.add("full_npu_tail_assembly_report")
        missing = sorted(required - set(self.paths))
        if missing:
            raise ValueError(f"config is missing paths: {', '.join(missing)}")
        file_keys = required - {"scratch_root", "metrics_log"}
        absent = [key for key in sorted(file_keys) if not self.paths[key].is_file()]
        if absent:
            raise FileNotFoundError(f"configured files are absent: {', '.join(absent)}")

    def _validate_limits(self) -> None:
        if not 1 <= self.default_max_tokens <= self.max_tokens <= CONTEXT:
            raise ValueError(
                f"generation limits must satisfy 1 <= default_max_tokens <= "
                f"max_tokens <= {CONTEXT}"
            )
        if self.max_request_bytes < 2 or self.max_image_bytes < 1:
            raise ValueError("request and image byte limits must be positive")
        if self.stage_timeout < 1 or self.decoder_timeout < 1 or self.threads < 1:
            raise ValueError("runtime timeouts and cpu_threads must be positive")
        if self.runtime.get("single_image_prompt_order", "preserve") not in ("preserve", "image_first"):
            raise ValueError("runtime.single_image_prompt_order must be preserve or image_first")
        if self.p5_repair_tokens_text not in (1, 16, 32, 48):
            raise ValueError("runtime.p5_repair_tokens_text must be 1, 16, 32, or 48")
        if self.p5_repair_tokens_image not in (1, 16, 32, 48):
            raise ValueError("runtime.p5_repair_tokens_image must be 1, 16, 32, or 48")
        if not 0 <= self.full_p5_text_max_prompt_tokens < CONTEXT:
            raise ValueError(
                "runtime.full_p5_text_max_prompt_tokens must be in [0, 2047]"
            )
        if self.full_p5_text_max_prompt_tokens and not self.persistent_decoder_enabled:
            raise ValueError(
                "runtime.full_p5_text_max_prompt_tokens requires persistent_decoder"
            )
        if not 1.0 <= self.default_repetition_penalty <= 1.2:
            raise ValueError(
                "runtime.default_repetition_penalty must be in [1.0, 1.2]"
            )

    def health(self) -> dict[str, Any]:
        return {
            "status": "ok" if self.ready else "not_ready",
            "ready": self.ready,
            "model": self.model_id,
            "capabilities": {"text": True, "image": True, "video": False},
            "vision_image_size_wh": [1024, 576],
            "vision_image_grid_thw": [1, 36, 64],
            "vision_image_tokens": 576,
            "frequency_penalty_scope": "completion_only",
            "single_image_prompt_order": self.runtime.get("single_image_prompt_order", "preserve"),
            "image_default_system_prompt": self.runtime.get("image_system_prompt", ""),
            "image_default_system_policy": "apply_only_when_no_system_message_is_supplied",
            "backend": {
                "vision": "QNNExecutionProvider / HTP / fixed1024x576 three-stage",
                "layers_0_19": "QNNExecutionProvider / HTP / Chunk16",
                "layers_20_23": (
                    "QNNExecutionProvider / HTP / Chunk16"
                    if self.full_npu_prefill
                    else "CPUExecutionProvider / FP32"
                ),
                "language_prefill_full24_npu": self.full_npu_prefill,
                "decode": "QNN / HTP / P5",
                "cpu_fallback_for_npu_partitions": False,
            },
            "context_length": CONTEXT,
            "generation_limits": {
                "default_max_tokens": self.default_max_tokens,
                "max_tokens": self.max_tokens,
                "context_aware_clamp": True,
                "auto_history_truncation": True,
                "minimum_chunk_padding": True,
                "p5_state_repair_tokens_text": self.p5_repair_tokens_text,
                "p5_state_repair_tokens_image": self.p5_repair_tokens_image,
                "p5_state_repair_maximum_actual_tokens": self.max_p5_remainder_tokens,
                "full_p5_text_max_prompt_tokens": self.full_p5_text_max_prompt_tokens,
                "default_repetition_penalty": self.default_repetition_penalty,
                "repetition_penalty_scope": "completion_only",
                "short_text_prefill_policy": (
                    "full_prompt_sequential_p5_qnn_htp"
                    if self.full_p5_text_max_prompt_tokens
                    else "disabled"
                ),
            },
            "persistent_stages": {
                "frontend": self.stage_worker.alive(),
                "vision": self.stage_worker.alive(),
                "vision_backend": self.stage_worker.startup.get(
                    "vision_backend"
                ),
                "vision_session_create_seconds": self.stage_worker.startup.get(
                    "vision_session_create_seconds"
                ),
                "vision_provider_counts": self.stage_worker.startup.get(
                    "vision_provider_counts"
                ),
                "vision_cpu_assigned_nodes": self.stage_worker.startup.get(
                    "vision_cpu_assigned_nodes"
                ),
                "vision_qnn_assigned_nodes": self.stage_worker.startup.get(
                    "vision_qnn_assigned_nodes"
                ),
                "vision_startup_gates": self.stage_worker.startup.get(
                    "vision_startup_gates"
                ),
                "npu_prefill": self.persistent_decoder is not None and self.persistent_decoder.alive(),
                "fp32_tail": (
                    False
                    if self.full_npu_prefill
                    else self.stage_worker.alive()
                ),
                "full_npu_prefill": self.stage_worker.startup.get(
                    "full_npu_prefill"
                ),
                "tail_backend": self.stage_worker.startup.get("tail_backend"),
                "seed_build": "native_resident_state_encoder",
                "startup_seconds": self.stage_worker.startup.get("startup_seconds"),
                "tokenizer_create_seconds": self.stage_worker.startup.get(
                    "tokenizer_create_seconds"
                ),
                "prefill_session_create_seconds": self.persistent_decoder.startup.get(
                    "prefill_session_create_seconds"
                ),
                "prefill_provider_counts": self.stage_worker.startup.get(
                    "prefill_provider_counts"
                ),
                "prefill_cpu_assigned_nodes": self.stage_worker.startup.get(
                    "prefill_cpu_assigned_nodes"
                ),
            },
            "persistent_decoder": {
                "enabled": self.persistent_decoder_enabled,
                "alive": self.persistent_decoder is not None
                and self.persistent_decoder.alive(),
                "startup_session_create_seconds": (
                    self.persistent_decoder.startup.get("session_create_seconds")
                    if self.persistent_decoder is not None
                    else None
                ),
                "restart_count": self.decoder_restart_count,
                "last_restart_seconds": self.last_decoder_restart_seconds,
            },
            "residency_policy": "all_resident_single_common_p5",
            "language_session_group": "standard_qnn_ep_same_process",
            "state_handoff": "native_resident_arena_v1",
            "retained_hidden_chunks": 1,
            "decoder_model_count": 1,
            "queue_serialized": True,
            "metrics_endpoint": "/metrics",
            "requests_completed": self.metrics.completed,
            "uptime_seconds": round(time.time() - self.started_at, 3),
        }

    def authorize(self, authorization: str | None) -> bool:
        if not self.api_key:
            return True
        if not authorization or not authorization.startswith("Bearer "):
            return False
        return hmac.compare_digest(authorization[7:], self.api_key)

    def _prompt_token_count(self, messages: list[dict[str, Any]]) -> int:
        rendered, _ = render_messages(messages)
        return len(self.tokenizer.encode(rendered, add_special_tokens=False).ids)

    @staticmethod
    def _append_prompt_padding(messages: list[dict[str, Any]]) -> None:
        if messages[0]["role"] != "system":
            # An empty system turn is semantically neutral but contributes the
            # ChatML control-token overhead needed by the Chunk16 minimum.
            messages.insert(0, {"role": "system", "content": ""})
            return
        content = messages[-1]["content"]
        if isinstance(content, str):
            messages[-1]["content"] = content + "\n"
            return
        if not isinstance(content, list):
            raise RuntimeError("normalized final user content contract failed")
        for part in reversed(content):
            if part.get("type") == "text":
                part["text"] = str(part.get("text", "")) + "\n"
                return
        content.append({"type": "text", "text": "\n"})

    @staticmethod
    def _append_vlm_suffix_padding(messages: list[dict[str, Any]]) -> None:
        """Insert scalar alignment padding after the image but before its query."""
        # This whitespace triplet is one stable tokenizer token at the image
        # suffix boundary.  Plain repeated newlines are aggressively merged by
        # the tokenizer and may need dozens of characters for one token.
        padding = " \t\n"
        content = messages[-1]["content"]
        if isinstance(content, str):
            messages[-1]["content"] = content + padding
            return
        if not isinstance(content, list):
            raise RuntimeError("normalized final user content contract failed")
        image_indices = [
            index for index, part in enumerate(content) if part.get("type") == "image"
        ]
        if not image_indices:
            raise RuntimeError("VLM alignment padding requires an image part")
        last_image = image_indices[-1]
        # Preserve the semantic query as the most recent tokens seen by the P5
        # repair window.  Padding after the query can dominate a 32-token
        # suffix for short prompts and induce empty/code-fence continuations.
        # Prepending to the first text part after the final image moves the
        # boundary by the same scalar token without duplicating or rewriting
        # the user's instruction.
        for part in content[last_image + 1 :]:
            if part.get("type") == "text":
                part["text"] = padding + str(part.get("text", ""))
                return
        content.append({"type": "text", "text": padding})

    @staticmethod
    def _aligned_prefix_token_count(total_tokens: int, repair_tokens: int) -> int:
        maximal_aligned = total_tokens // CHUNK * CHUNK
        if not repair_tokens:
            return maximal_aligned
        return min(
            maximal_aligned,
            max(
                CHUNK,
                (total_tokens - repair_tokens) // CHUNK * CHUNK,
            ),
        )

    def _vlm_remainder_crosses_image(
        self, messages: list[dict[str, Any]]
    ) -> tuple[bool, int]:
        rendered, image_count = render_messages(messages)
        token_ids = self.tokenizer.encode(rendered, add_special_tokens=False).ids
        if image_count == 0 or (self.runtime.get("frontend_vector_vision_suffix",False) and
                                not self.runtime.get("frontend_retain_alignment_padding",False)):
            return False, len(token_ids)
        aligned_tokens = self._aligned_prefix_token_count(
            len(token_ids), self.p5_repair_tokens_image
        )
        return IMAGE_TOKEN_ID in token_ids[aligned_tokens:], len(token_ids)

    def _fit_messages_to_context(
        self, source: list[dict[str, Any]]
    ) -> tuple[list[dict[str, Any]], dict[str, Any]]:
        # Work on a JSON-safe deep copy so context fitting never mutates the
        # caller's request object or the normalized image markers.
        messages = json.loads(json.dumps(source, ensure_ascii=False))
        original_tokens = self._prompt_token_count(messages)
        original_count = len(messages)
        system_index = 0 if messages[0]["role"] == "system" else None
        image_indices = {
            index
            for index, message in enumerate(messages)
            if isinstance(message.get("content"), list)
            and any(part.get("type") == "image" for part in message["content"])
        }
        selected = {len(messages) - 1, *image_indices}
        system_dropped = False

        if original_tokens >= CONTEXT:
            essential = [messages[index] for index in sorted(selected)]
            essential_tokens = self._prompt_token_count(essential)
            if essential_tokens >= CONTEXT:
                raise RequestError(
                    "the latest user message and image exceed the 2048-token context",
                    "context_length_exceeded",
                    HTTPStatus.BAD_REQUEST,
                    "messages",
                )

            if system_index is not None and system_index not in selected:
                trial_indices = selected | {system_index}
                trial = [messages[index] for index in sorted(trial_indices)]
                if self._prompt_token_count(trial) < CONTEXT:
                    selected = trial_indices
                else:
                    system_dropped = True

            excluded = selected | ({system_index} if system_index is not None else set())
            for index in range(len(messages) - 2, -1, -1):
                if index in excluded:
                    continue
                trial_indices = selected | {index}
                trial = [messages[item] for item in sorted(trial_indices)]
                if self._prompt_token_count(trial) >= CONTEXT:
                    break
                selected = trial_indices

            messages = [messages[index] for index in sorted(selected)]
            content_start = 1 if messages and messages[0]["role"] == "system" else 0
            while (
                len(messages) - content_start > 1
                and messages[content_start]["role"] == "assistant"
            ):
                del messages[content_start]

        context_fitted_count = len(messages)
        dropped_messages = max(0, original_count - context_fitted_count)
        effective_tokens = self._prompt_token_count(messages)
        padding_steps = 0
        while effective_tokens < CHUNK and padding_steps < CHUNK:
            self._append_prompt_padding(messages)
            padding_steps += 1
            effective_tokens = self._prompt_token_count(messages)
        if effective_tokens < CHUNK or effective_tokens >= CONTEXT:
            raise RequestError(
                f"fitted prompt has {effective_tokens} tokens; expected [16, 2047]",
                "context_length_exceeded",
                HTTPStatus.BAD_REQUEST,
                "messages",
            )

        # The P5 remainder engine advances scalar RoPE positions.  If the
        # floor-aligned Chunk16 prefix ends inside the 144-token visual region,
        # handing the suffix to P5 would silently use the wrong spatial
        # M-RoPE.  Add only scalar newline tokens after the image until the
        # aligned prefix includes the complete visual region.  This preserves
        # the image embeddings/M-RoPE and costs at most one additional chunk in
        # ordinary requests.
        vlm_alignment_padding_steps = 0
        crosses_image, effective_tokens = self._vlm_remainder_crosses_image(messages)
        while crosses_image and vlm_alignment_padding_steps < 64:
            if effective_tokens >= CONTEXT - 1:
                raise RequestError(
                    "the image prompt cannot be aligned inside the 2048-token context",
                    "context_length_exceeded",
                    HTTPStatus.BAD_REQUEST,
                    "messages",
                )
            self._append_vlm_suffix_padding(messages)
            vlm_alignment_padding_steps += 1
            crosses_image, effective_tokens = self._vlm_remainder_crosses_image(messages)
            if effective_tokens >= CONTEXT:
                raise RequestError(
                    "the image prompt cannot be aligned inside the 2048-token context",
                    "context_length_exceeded",
                    HTTPStatus.BAD_REQUEST,
                    "messages",
                )
        if crosses_image:
            raise RuntimeError("unable to move the VLM remainder beyond the image region")
        return messages, {
            "original_messages": original_count,
            "effective_messages": len(messages),
            "dropped_messages": dropped_messages,
            "original_prompt_tokens": original_tokens,
            "effective_prompt_tokens": effective_tokens,
            "history_truncated": dropped_messages > 0 or system_dropped,
            "system_dropped": system_dropped,
            "minimum_chunk_padding_steps": padding_steps,
            "vlm_alignment_padding_steps": vlm_alignment_padding_steps,
        }

    def prepare_request(self, request: dict[str, Any], request_dir: Path) -> PreparedRequest:
        model = request.get("model", self.model_id)
        aliases = {self.model_id, "Qwen3.5-0.8B", "qwen3.5-0.8b"}
        if model not in aliases:
            raise RequestError(f"unknown model: {model}", "model_not_found", 404, "model")
        if request.get("n", 1) != 1:
            raise RequestError("only n=1 is supported", param="n")
        if request.get("stop") not in (None, [], ""):
            raise RequestError("custom stop strings are not supported in V0", param="stop")
        response_format = request.get("response_format") or {"type": "text"}
        if not isinstance(response_format, dict) or response_format.get("type", "text") != "text":
            raise RequestError("only response_format.type=text is supported", param="response_format")
        tools = request.get("tools")
        tool_choice = request.get("tool_choice")
        if tools is not None and not isinstance(tools, list):
            raise RequestError("tools must be an array", param="tools")
        if tool_choice not in (None, "auto", "none"):
            raise RequestError("forced tool calls are not supported", param="tool_choice")
        max_tokens = request.get("max_completion_tokens", request.get("max_tokens"))
        if max_tokens is None:
            max_tokens = self.default_max_tokens
        if isinstance(max_tokens, bool) or not isinstance(max_tokens, int):
            raise RequestError("max_tokens must be an integer", param="max_tokens")
        if not 1 <= max_tokens <= self.max_tokens:
            raise RequestError(
                f"max_tokens must be in [1, {self.max_tokens}]", param="max_tokens"
            )
        # Sampling parameters are validated and forwarded to the native decoder.
        messages_for_mode = request.get("messages", [])
        has_image = isinstance(messages_for_mode, list) and any(
            isinstance(m, dict) and isinstance(m.get("content"), list) and any(
                isinstance(part, dict) and part.get("type") in ("image_url", "input_image")
                for part in m["content"]) for m in messages_for_mode)
        defaults = self.runtime.get("image_sampling_defaults" if has_image else "sampling_defaults", {})
        # An explicit legacy alias overrides a default just like its canonical
        # spelling. Keep explicit conflicting spellings for validation below.
        if "repeat_penalty" in request and "repetition_penalty" not in request:
            defaults = {k: v for k, v in defaults.items() if k != "repetition_penalty"}
        request = {**defaults, **request}
        temperature = validate_number(request, "temperature", 0.0, 2.0)
        temperature = 0.0 if temperature is None else temperature
        top_k = request.get("top_k", 20)
        if isinstance(top_k, bool) or not isinstance(top_k, int) or not 1 <= top_k <= 248320:
            raise RequestError("top_k must be an integer in [1, 248320]", param="top_k")
        min_p = validate_number(request, "min_p", 0.0, 1.0)
        min_p = 0.0 if min_p is None else min_p
        presence_penalty = validate_number(request, "presence_penalty", -2.0, 2.0)
        presence_penalty = 0.0 if presence_penalty is None else presence_penalty
        frequency_penalty = validate_number(request, "frequency_penalty", -2.0, 2.0)
        frequency_penalty = 0.0 if frequency_penalty is None else frequency_penalty
        random_seed = request.get("seed", 0)
        if isinstance(random_seed, bool) or not isinstance(random_seed, int) or not 0 <= random_seed < 2**64:
            raise RequestError("seed must be an integer in [0, 2**64)", param="seed")
        top_p = validate_number(request, "top_p", 0.0, 1.0)
        top_p = 1.0 if top_p is None else top_p
        if top_p == 0.0:
            raise RequestError("top_p must be greater than zero", param="top_p")
        if "seed" in request and (
            isinstance(request["seed"], bool) or not isinstance(request["seed"], int)
        ):
            raise RequestError("seed must be an integer", param="seed")
        repeat_penalty = validate_number(request, "repeat_penalty", 1.0, 1.2)
        repetition_penalty = validate_number(
            request, "repetition_penalty", 1.0, 1.2
        )
        if (
            repeat_penalty is not None
            and repetition_penalty is not None
            and abs(repeat_penalty - repetition_penalty) > 1e-12
        ):
            raise RequestError(
                "repeat_penalty and repetition_penalty must match when both are supplied",
                param="repeat_penalty",
            )
        effective_repetition_penalty = (
            repeat_penalty
            if repeat_penalty is not None
            else (
                repetition_penalty
                if repetition_penalty is not None
                else self.default_repetition_penalty
            )
        )

        messages = request.get("messages")
        if not isinstance(messages, list) or not messages:
            raise RequestError("messages must be a non-empty array", param="messages")
        normalized: list[dict[str, Any]] = []
        image_bytes: bytes | None = None
        image_mime: str | None = None
        for message in messages:
            if not isinstance(message, dict):
                raise RequestError("each message must be an object", param="messages")
            role = message.get("role")
            if role not in {"system", "user", "assistant"}:
                raise RequestError("roles must be system, user, or assistant", param="messages")
            content = message.get("content", "")
            if isinstance(content, str):
                normalized.append({"role": role, "content": content})
                continue
            if not isinstance(content, list):
                raise RequestError("message content must be text or an array", param="messages")
            parts: list[dict[str, Any]] = []
            for part in content:
                if not isinstance(part, dict):
                    raise RequestError("message content parts must be objects", param="messages")
                kind = part.get("type")
                if kind in {"text", "input_text"} and isinstance(part.get("text"), str):
                    parts.append({"type": "text", "text": part["text"]})
                    continue
                if kind not in {"image_url", "input_image"}:
                    raise RequestError("only text and still-image parts are supported", param="messages")
                if image_bytes is not None:
                    raise RequestError("only one image is supported per request", param="messages")
                image_value = part.get("image_url")
                if isinstance(image_value, dict):
                    image_value = image_value.get("url")
                if image_value is None:
                    image_value = part.get("image_url") or part.get("image")
                if not isinstance(image_value, str):
                    raise RequestError("image_url.url must be a data URL", param="messages")
                match = DATA_IMAGE_RE.fullmatch(image_value)
                if match is None:
                    raise RequestError(
                        "only base64 data URLs for PNG, JPEG, or WebP are accepted; remote URLs are disabled",
                        param="messages",
                    )
                try:
                    decoded = base64.b64decode(match.group(2), validate=True)
                except (binascii.Error, ValueError) as exc:
                    raise RequestError("invalid base64 image", param="messages") from exc
                if not decoded or len(decoded) > self.max_image_bytes:
                    raise RequestError(
                        f"decoded image must be in [1, {self.max_image_bytes}] bytes",
                        param="messages",
                    )
                image_bytes = decoded
                image_mime = match.group(1).lower()
                parts.append({"type": "image"})
            normalized.append({"role": role, "content": parts})
        if normalized[-1]["role"] != "user":
            raise RequestError("the final message must have role user", param="messages")

        if temperature > 0 and image_bytes is not None and self.p5_repair_tokens_image == 0:
            raise RequestError("image sampling requires p5_repair_tokens_image > 0", param="temperature")
        if temperature > 0 and self.p5_repair_tokens_text == 0:
            raise RequestError("text sampling requires p5_repair_tokens_text > 0", param="temperature")
        # A profile may use the official single-image example order.
        # Keep role order and the relative order of all text parts unchanged.
        # This is a declared input policy, not an answer-dependent rewrite.
        image_order = {"policy": self.runtime.get("single_image_prompt_order", "preserve"),
                       "changed": False, "message_index": None, "original_part_index": None}
        for message_index, message in enumerate(normalized):
            parts = message["content"]
            if not isinstance(parts, list):
                continue
            for part_index, part in enumerate(parts):
                if part.get("type") == "image":
                    image_order.update(message_index=message_index, original_part_index=part_index)
                    if image_order["policy"] == "image_first" and part_index != 0:
                        message["content"] = [part] + parts[:part_index] + parts[part_index+1:]
                        image_order["changed"] = True
                    break
        image_system_prompt = self.runtime.get("image_system_prompt", "")
        image_system_applied = bool(image_bytes is not None and image_system_prompt and
                                    not any(m["role"] == "system" for m in normalized))
        if image_system_applied:
            normalized.insert(0, {"role": "system", "content": image_system_prompt})
        normalized, context_management = self._fit_messages_to_context(normalized)
        context_management["image_default_system_prompt"] = {
            "configured": bool(image_system_prompt),
            "applied_before_context_fit": image_system_applied,
            "retained": image_system_applied and not context_management["system_dropped"],
        }
        context_management["image_content_order"] = image_order

        image_path = None
        if image_bytes is not None:
            suffix = ".jpg" if image_mime in {"image/jpeg", "image/jpg"} else (
                ".webp" if image_mime == "image/webp" else ".png"
            )
            image_path = request_dir / f"input_image{suffix}"
            image_path.write_bytes(image_bytes)
        return PreparedRequest(
            messages=normalized,
            image_path=image_path,
            mode="image" if image_path is not None else "text",
            max_tokens=max_tokens,
            requested_decoding={
                "temperature": temperature,
                "top_p": top_p,
                "seed": random_seed,
                "top_k": top_k,
                "min_p": min_p,
                "presence_penalty": presence_penalty,
                "frequency_penalty": frequency_penalty,
                "repeat_penalty": repeat_penalty,
                "repetition_penalty": repetition_penalty,
                "effective_repetition_penalty": effective_repetition_penalty,
                "penalty_scope": "completion_only",
            },
            tools_ignored=bool(tools),
            context_management=context_management,
        )

    def _run_stage(
        self, stage: str, argv: list[str], request_dir: Path
    ) -> float:
        stdout_path = request_dir / f"{stage}.stdout.log"
        stderr_path = request_dir / f"{stage}.stderr.log"
        if stage in self.stage_worker.STAGES:
            return self.stage_worker.run(stage, argv[2:], stdout_path, stderr_path)
        started = time.perf_counter()
        try:
            with stdout_path.open("x", encoding="utf-8", newline="\n") as stdout, stderr_path.open(
                "x", encoding="utf-8", newline="\n"
            ) as stderr:
                completed = subprocess.run(
                    argv,
                    stdout=stdout,
                    stderr=stderr,
                    text=True,
                    timeout=self.stage_timeout,
                    check=False,
                )
        except subprocess.TimeoutExpired as exc:
            raise PipelineError(stage, f"timed out after {self.stage_timeout}s") from exc
        elapsed = time.perf_counter() - started
        if completed.returncode != 0:
            detail = tail_text(stderr_path) or tail_text(stdout_path) or "no diagnostic output"
            raise PipelineError(stage, f"exit={completed.returncode}; {detail}")
        return elapsed

    def close(self) -> None:
        self.ready = False
        if self.persistent_decoder is not None:
            self.persistent_decoder.close()
        self.stage_worker.close()
        if self.request_scratch_root != self.scratch_root:
            # Only remove our exact, uniquely created empty directory.
            if self.request_scratch_root.parent != Path("/dev/shm") or not self.request_scratch_root.name.startswith("qwen_q6a_requests_"):
                raise RuntimeError("unexpected temporary scratch root")
            try: self.request_scratch_root.rmdir()
            except OSError: pass

    def _restart_persistent_decoder(self) -> float:
        if not self.persistent_decoder_enabled:
            return 0.0
        started = time.perf_counter()
        current = self.persistent_decoder
        self.persistent_decoder = None
        if current is not None:
            current.close(force=True)
        replacement = PersistentDecoder(
            self.paths,
            self.paths["metrics_log"].parent / "persistent_decoder.stderr.log",
            self.decoder_timeout,
        )
        self.persistent_decoder = replacement
        elapsed = time.perf_counter() - started
        self.decoder_restart_count += 1
        self.last_decoder_restart_seconds = elapsed
        return elapsed

    @staticmethod
    def _decoding_mode(d):
        if d["temperature"] > 0:
            return "sample"
        if d["effective_repetition_penalty"] != 1.0 or d["presence_penalty"] != 0 or d.get("frequency_penalty",0.0) != 0:
            return "penalized_greedy"
        return "greedy"

    @staticmethod
    def _effective_parameters(d):
        sample = d["temperature"] > 0
        return {"temperature": d["temperature"], "top_p": d["top_p"] if sample else 1.0,
                "top_k": d["top_k"] if sample else 1, "min_p": d["min_p"] if sample else 0.0,
                "presence_penalty": d["presence_penalty"], "frequency_penalty": d.get("frequency_penalty",0.0),
                "repetition_penalty": d["effective_repetition_penalty"],
                "seed": d["seed"], "penalty_scope": "completion_only"}

    def _decoder_command(
        self,
        seed: dict[str, Any],
        max_tokens: int,
        request_id: str,
        seed_binary: Path,
        requested_decoding: dict[str, Any],
    ) -> tuple[str, int, bool, int | None]:
        history = [int(value) for value in seed["history_token_ids"]]
        position = int(seed["seed_position"])
        rope_delta = int(seed.get("rope_delta", 0))
        remainder = [int(value) for value in seed.get("remainder_token_ids", [])]
        remainder_mode = seed.get("mode") == "p5_remainder_handoff"
        if remainder_mode and not 1 <= len(remainder) <= self.max_p5_remainder_tokens:
            raise PipelineError("decoder", "P5 state-repair suffix length is invalid")
        if not remainder_mode and remainder:
            raise PipelineError("decoder", "non-remainder seed contains repair tokens")
        mrope_mode = rope_delta != 0 or "vlm_prompt_bundle" in seed.get("sources", {})
        first_token = None if remainder_mode else int(seed["first_token"])
        engine_steps = max_tokens if remainder_mode else max_tokens - 1
        repetition_penalty = float(
            requested_decoding["effective_repetition_penalty"]
        )
        mode = self._decoding_mode(requested_decoding)
        penalized = mode != "greedy"
        if mode == "sample" and not remainder_mode:
            raise PipelineError("decoder", "sampling requires a remainder seed")
        command_name = (
            (
                "GENERATE_SEEDED_REMAINDER_MROPE_V2"
                if remainder_mode and mrope_mode
                else (
                    "GENERATE_SEEDED_MROPE_V2"
                    if mrope_mode
                    else (
                        "GENERATE_SEEDED_REMAINDER_V2"
                        if remainder_mode
                        else "GENERATE_SEEDED_V2"
                    )
                )
            )
            if penalized
            else (
                "GENERATE_SEEDED_REMAINDER_MROPE"
                if remainder_mode and mrope_mode
                else (
                    "GENERATE_SEEDED_MROPE"
                    if mrope_mode
                    else (
                        "GENERATE_SEEDED_REMAINDER"
                        if remainder_mode
                        else "GENERATE_SEEDED"
                    )
                )
            )
        )
        fields = [
            command_name,
            request_id,
            str(engine_steps),
            ",".join(str(value) for value in STOP_IDS),
            seed.get("resident_seed_tag") or str(seed_binary.resolve()),
            str(position),
            ",".join(str(value) for value in history),
        ]
        if remainder_mode:
            fields.append(",".join(str(value) for value in remainder))
            if mrope_mode:
                fields.append(str(rope_delta))
        elif mrope_mode:
            fields.append(str(rope_delta))
        if penalized:
            fields.extend(
                [
                    mode,
                    format(requested_decoding["temperature"] or 1.0, ".17g"),
                    format(requested_decoding["top_p"], ".17g"),
                    str(requested_decoding["top_k"]),
                    format(requested_decoding["min_p"], ".17g"),
                    format(requested_decoding["presence_penalty"], ".17g"),
                    format(repetition_penalty, ".17g"),
                    str(requested_decoding["seed"]),
                ]
            )
        if seed.get("vector_vision_suffix"):
            if not remainder_mode or not mrope_mode:
                raise PipelineError("decoder","visual suffix requires a remainder with M-RoPE")
            fields[0] = "GENERATE_SEEDED_EMBEDDINGS_V3"
            if not penalized:
                fields.extend(["greedy","1","1","1","0","0","1","0"])
            files=seed["suffix_files"]
            fields.extend(str(files[key]["path"]) for key in ("hidden","rope_cos","rope_sin"))
        command = "\t".join(fields)
        frequency = float(requested_decoding.get("frequency_penalty",0.0))
        if frequency != 0.0: command = "FREQUENCY_V1\t" + format(frequency,".17g") + "\t" + command
        return command, engine_steps, remainder_mode, first_token

    @staticmethod
    def _full_p5_decoder_command(
        prompt_ids: list[int],
        max_tokens: int,
        request_id: str,
        requested_decoding: dict[str, Any],
    ) -> str:
        if (
            not prompt_ids
            or len(prompt_ids) >= CONTEXT
            or max_tokens < 1
            or len(prompt_ids) + max_tokens > CONTEXT
            or any(token_id < 0 or token_id >= 248320 for token_id in prompt_ids)
        ):
            raise PipelineError("decoder", "full-prompt P5 command contract failed")
        repetition_penalty = float(
            requested_decoding["effective_repetition_penalty"]
        )
        fields = [
            "GENERATE",
            request_id,
            Chunk16Engine._decoding_mode(requested_decoding),
            str(max_tokens),
            str(requested_decoding["seed"]),
            format(requested_decoding["temperature"] or 1.0, ".17g"),
            format(requested_decoding["top_p"], ".17g"),
            str(requested_decoding["top_k"]),
            format(requested_decoding["min_p"], ".17g"),
            format(requested_decoding["presence_penalty"], ".17g"),
            format(repetition_penalty, ".17g"),
            ",".join(str(value) for value in STOP_IDS),
            ",".join(str(value) for value in prompt_ids),
        ]
        command = "\t".join(fields)
        frequency = float(requested_decoding.get("frequency_penalty",0.0))
        if frequency != 0.0: command = "FREQUENCY_V1\t" + format(frequency,".17g") + "\t" + command
        return command

    def _run_full_p5_decoder(
        self,
        prompt_ids: list[int],
        max_tokens: int,
        request_id: str,
        request_dir: Path,
        requested_decoding: dict[str, Any],
        on_token: Callable[[int], None],
    ) -> dict[str, Any]:
        if self.persistent_decoder is None:
            raise PipelineError(
                "decoder", "full-prompt P5 policy requires the persistent decoder"
            )
        command = self._full_p5_decoder_command(
            prompt_ids, max_tokens, request_id, requested_decoding
        )
        result = self.persistent_decoder.run(
            command,
            max_tokens,
            False,
            None,
            request_id,
            request_dir / "decoder.stdout.jsonl",
            request_dir / "decoder.stderr.log",
            on_token,
            event_mode="full_prompt",
        )
        if (result.get("done") or {}).get("frequency_penalty") != float(requested_decoding.get("frequency_penalty", 0.0)):
            raise PipelineError("decoder", "native frequency penalty differs from the request")
        return result

    def _run_decoder(
        self,
        seed: dict[str, Any],
        max_tokens: int,
        request_id: str,
        seed_binary: Path,
        request_dir: Path,
        requested_decoding: dict[str, Any],
        on_token: Callable[[int], None],
    ) -> dict[str, Any]:
        command, engine_steps, remainder_mode, first_token = self._decoder_command(
            seed, max_tokens, request_id, seed_binary, requested_decoding
        )
        if self.runtime.get("frontend_force_seeded_v2", False):
            fields = command.split("\t")
            if not fields[0].endswith("_V2"):
                fields[0] += "_V2"
                fields.extend(["greedy", "1", "1", "1", "0", "0", "1", "0"])
                command = "\t".join(fields)
        if self.persistent_decoder is not None:
            result = self.persistent_decoder.run(
                command,
                engine_steps,
                remainder_mode,
                first_token,
                request_id,
                request_dir / "decoder.stdout.jsonl",
                request_dir / "decoder.stderr.log",
                on_token,
            )
            done = result.get("done") or {}
            if done.get("frequency_penalty") != float(requested_decoding.get("frequency_penalty", 0.0)):
                raise PipelineError("decoder", "native frequency penalty differs from the request")
            if bool(done.get("embedding_remainder",False)) != bool(seed.get("vector_vision_suffix",False)):
                raise PipelineError("decoder","visual suffix mode differs from prepared seed")
            expected_delta = int(seed.get("rope_delta", 0))
            expected_start = int(seed["generation_rope_start"])
            if done.get("rope_delta") != expected_delta or done.get("generation_rope_start") != expected_start:
                raise PipelineError("decoder", "P5 M-RoPE position contract differs from the prepared seed")
            return result
        if engine_steps == 0:
            if first_token is None:
                raise PipelineError("decoder", "zero engine steps without an endpoint token")
            on_token(first_token)
            return {
                "ready": None,
                "done": None,
                "engine_wall_seconds": 0.0,
                "finish_reason": "length",
                "engine_tokens": 0,
                "stderr": "",
            }

        commands_path = request_dir / "decoder.commands.tsv"
        stdout_path = request_dir / "decoder.stdout.jsonl"
        stderr_path = request_dir / "decoder.stderr.log"
        commands_path.write_text(command + "\nSHUTDOWN\n", encoding="utf-8", newline="\n")
        environment = os.environ.copy()
        environment["ADSP_LIBRARY_PATH"] = (
            f"{self.paths['qnn_plugin'].parent};/usr/lib/dsp/cdsp;/usr/lib/dsp/adsp;/dsp"
        )
        argv = [
            str(self.paths["decoder_engine"]),
            str(self.paths["decoder_model"]),
            str(self.paths["qnn_plugin"]),
            str(self.paths["qnn_backend"]),
            str(self.paths["tensor_contract"]),
            str(self.paths["advance_plan"]),
            str(self.paths["embedding"]),
            str(self.paths["rope"]),
        ]
        records: list[dict[str, Any]] = []
        ready: list[dict[str, Any]] = []
        done: list[dict[str, Any]] = []
        shutdown: list[dict[str, Any]] = []
        token_count = 0
        output_queue: queue.Queue[str | None] = queue.Queue()
        started = time.perf_counter()
        with stderr_path.open("x", encoding="utf-8", newline="\n") as stderr, stdout_path.open(
            "x", encoding="utf-8", newline="\n"
        ) as stdout:
            process = subprocess.Popen(
                argv,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=stderr,
                text=True,
                bufsize=1,
                env=environment,
            )
            assert process.stdin is not None and process.stdout is not None

            def reader() -> None:
                try:
                    for line in process.stdout:
                        output_queue.put(line)
                finally:
                    output_queue.put(None)

            reader_thread = threading.Thread(target=reader, daemon=True)
            reader_thread.start()
            process.stdin.write(command + "\nSHUTDOWN\n")
            process.stdin.flush()
            process.stdin.close()
            deadline = time.monotonic() + self.decoder_timeout
            endpoint_emitted = False
            stream_ended = False
            try:
                while not stream_ended:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise subprocess.TimeoutExpired(argv, self.decoder_timeout)
                    try:
                        line = output_queue.get(timeout=min(0.5, remaining))
                    except queue.Empty:
                        if process.poll() is not None and not reader_thread.is_alive():
                            break
                        continue
                    if line is None:
                        stream_ended = True
                        continue
                    stdout.write(line)
                    stdout.flush()
                    if not line.strip():
                        continue
                    try:
                        record = json.loads(line)
                    except json.JSONDecodeError as exc:
                        raise PipelineError("decoder", "engine emitted invalid JSONL") from exc
                    if not isinstance(record, dict):
                        raise PipelineError("decoder", "engine JSONL record must be an object")
                    records.append(record)
                    kind = record.get("type")
                    if kind == "ready":
                        ready.append(record)
                        if first_token is not None and not endpoint_emitted:
                            on_token(first_token)
                            endpoint_emitted = True
                    elif kind == "seeded_token":
                        token_id = int(record["token_id"])
                        if not 0 <= token_id < 248320:
                            raise PipelineError("decoder", "generated token is outside vocabulary")
                        token_count += 1
                        on_token(token_id)
                    elif kind == "seeded_done":
                        done.append(record)
                    elif kind == "shutdown":
                        shutdown.append(record)
                return_code = process.wait(timeout=5)
            except Exception:
                process.kill()
                process.wait(timeout=5)
                raise
            finally:
                reader_thread.join(timeout=2)
        wall_seconds = time.perf_counter() - started
        stderr_value = tail_text(stderr_path)
        if return_code != 0:
            raise PipelineError("decoder", f"exit={return_code}; {stderr_value}")
        if first_token is not None and not endpoint_emitted:
            raise PipelineError("decoder", "engine never reached ready state")
        if len(ready) != 1 or ready[0].get("fallback_disabled") is not True:
            raise PipelineError("decoder", "QNN ready/fallback contract failed")
        if len(done) != 1 or done[0].get("state_reset_after_request") is not True:
            raise PipelineError("decoder", "completion/reset contract failed")
        if len(shutdown) != 1 or shutdown[0].get("status") != "ok":
            raise PipelineError("decoder", "clean shutdown contract failed")
        if token_count <= 0 or token_count > engine_steps:
            raise PipelineError("decoder", "generated token count is invalid")
        if "exception in " in stderr_value.lower() or "ort exception" in stderr_value.lower():
            raise PipelineError("decoder", f"runtime exception in stderr: {stderr_value}")
        return {
            "ready": ready[0],
            "done": done[0],
            "engine_wall_seconds": wall_seconds,
            "finish_reason": str(done[0].get("finish_reason", "length")),
            "engine_tokens": token_count,
            "remainder_mode": remainder_mode,
            "stderr": stderr_value,
        }

    def generate(
        self,
        request: dict[str, Any],
        request_id: str,
        request_started: float,
        on_text: Callable[[str], None] | None = None,
        is_cancelled: Callable[[], bool] | None = None,
    ) -> dict[str, Any]:
        request_dir = self.request_scratch_root / request_id
        request_dir.mkdir(parents=True, exist_ok=False)
        queue_started = time.perf_counter()
        self.generation_lock.acquire()
        queue_wait = time.perf_counter() - queue_started
        self.metrics.set_active(1)
        stage_wall: dict[str, float] = {}
        prepared: PreparedRequest | None = None
        try:
            if is_cancelled is not None and is_cancelled():
                raise ClientDisconnected("SSE client disconnected while queued")
            prepared = self.prepare_request(request, request_dir)
            p5_repair_tokens = (
                self.p5_repair_tokens_image
                if prepared.mode == "image"
                else self.p5_repair_tokens_text
            )
            messages_path = request_dir / "messages.json"
            token_ids_path = request_dir / "prompt_token_ids.npy"
            frontend_report_path = request_dir / "frontend_report.json"
            bundle_path = request_dir / "prompt_bundle.bin"
            messages_path.write_text(
                json.dumps(prepared.messages, ensure_ascii=False), encoding="utf-8"
            )
            frontend_argv = [
                str(self.python),
                str(self.paths["frontend_script"]),
                "--messages-json",
                str(messages_path),
                "--tokenizer-json",
                str(self.paths["tokenizer"]),
                "--embedding-npy",
                str(self.paths["embedding"]),
                "--rope-bin",
                str(self.paths["rope"]),
                "--output-token-ids",
                str(token_ids_path),
                "--report",
                str(frontend_report_path),
                "--threads",
                str(self.threads),
            ]
            if prepared.image_path is not None:
                frontend_argv.extend(
                    [
                        "--image",
                        str(prepared.image_path),
                        "--vision-patch-model",
                        str(self.paths["vision_patch_model"]),
                        "--vision-body-model",
                        str(self.paths["vision_body_model"]),
                        "--vision-merger-model",
                        str(self.paths["vision_merger_model"]),
                        "--output-bundle",
                        str(bundle_path),
                    ]
                )
            else:
                frontend_argv.extend(["--output-bundle", str(bundle_path)])
            stage_wall["frontend"] = self._run_stage("frontend", frontend_argv, request_dir)
            frontend = require_clean_report("frontend", frontend_report_path)
            prompt_tokens = int(frontend["prompt_tokens"])
            aligned_tokens = int(frontend["aligned_prefix_tokens"])
            remainder_tokens = int(frontend["remainder_tokens"])
            bundle = frontend.get("bundle") or {}
            rope_delta = int(bundle.get("rope_delta", 0))
            physical_capacity = CONTEXT - prompt_tokens
            mrope_capacity = CONTEXT - prompt_tokens - max(rope_delta, 0)
            context_capacity = min(physical_capacity, mrope_capacity)
            if context_capacity <= 0:
                raise RequestError(
                    f"rendered prompt exhausts ctx2048: prompt={prompt_tokens}, "
                    f"rope_delta={rope_delta}",
                    "context_length_exceeded",
                    HTTPStatus.BAD_REQUEST,
                    "messages",
                )
            effective_max_tokens = min(prepared.max_tokens, context_capacity)

            use_full_p5_text = (
                prepared.mode == "text"
                and self.full_p5_text_max_prompt_tokens > 0
                and prompt_tokens <= self.full_p5_text_max_prompt_tokens
            )
            if use_full_p5_text:
                prompt_array = np.asarray(
                    np.load(token_ids_path, allow_pickle=False), dtype=np.int64
                )
                if prompt_array.ndim != 1 or prompt_array.size != prompt_tokens:
                    raise PipelineError("frontend", "full-P5 token contract failed")
                prompt_ids = [int(value) for value in prompt_array]
                completion_ids: list[int] = []
                emitted_text = ""
                first_token_at: float | None = None

                def accept_full_p5_token(token_id: int) -> None:
                    nonlocal emitted_text, first_token_at
                    if is_cancelled is not None and is_cancelled():
                        raise ClientDisconnected("SSE client disconnected during decode")
                    if first_token_at is None:
                        first_token_at = time.perf_counter()
                    completion_ids.append(int(token_id))
                    decoded = decode_visible(self.tokenizer, completion_ids)
                    if decoded.startswith(emitted_text):
                        delta = decoded[len(emitted_text) :]
                        emitted_text = decoded
                        if delta and on_text is not None:
                            on_text(delta)
                    if is_cancelled is not None and is_cancelled():
                        raise ClientDisconnected("SSE client disconnected during decode")

                decoder = self._run_full_p5_decoder(
                    prompt_ids,
                    effective_max_tokens,
                    request_id,
                    request_dir,
                    prepared.requested_decoding,
                    accept_full_p5_token,
                )
                stage_wall["decoder"] = float(decoder["engine_wall_seconds"])
                if not completion_ids or len(completion_ids) > effective_max_tokens:
                    raise PipelineError(
                        "decoder", "OpenAI completion-token bound failed"
                    )
                generated_text = decode_visible(self.tokenizer, completion_ids)
                done = decoder.get("done") or {}
                ready = decoder.get("ready") or {}
                prompt_run = float(done.get("prompt_run_seconds_sum", 0.0))
                prompt_e2e = float(done.get("prompt_e2e_seconds_sum", 0.0))
                stage_wall["full_p5_prompt_e2e"] = prompt_e2e
                finished_at = time.perf_counter()
                metrics = {
                    "schema": "qwen35_q6a_openai_request_metrics_v1",
                    "status": "pass",
                    "request_id": request_id,
                    "mode": prepared.mode,
                    "residency_policy": "all_resident_single_common_p5",
                    "decoder_model": str(self.paths["decoder_model"]),
                    "native_frequency_penalty": done.get("frequency_penalty"),
                    "logit_correction_enabled": done.get("logit_correction_enabled", False),
                    "logit_correction_runs": done.get("logit_correction_runs", 0),
                    "logit_correction_seconds": done.get("logit_correction_seconds", 0.0),
                    "prefill_policy": "full_p5_text",
                    "prompt_tokens": prompt_tokens,
                    "aligned_prefix_tokens": 0,
                    "remainder_tokens": 0,
                    "full_p5_prompt_tokens": prompt_tokens,
                    "full_p5_text_max_prompt_tokens": (
                        self.full_p5_text_max_prompt_tokens
                    ),
                    "p5_repair_tokens_requested": 0,
                    "configured_p5_repair_tokens_text": self.p5_repair_tokens_text,
                    "completion_tokens": len(completion_ids),
                    "total_tokens": prompt_tokens + len(completion_ids),
                    "requested_max_tokens": prepared.max_tokens,
                    "effective_max_tokens": effective_max_tokens,
                    "context_capacity_tokens": context_capacity,
                    "max_tokens_clamped_to_context": effective_max_tokens
                    != prepared.max_tokens,
                    "context_management": prepared.context_management,
                    "ttft_seconds": (
                        first_token_at - request_started
                        if first_token_at is not None
                        else None
                    ),
                    "request_seconds": finished_at - request_started,
                    "queue_wait_seconds": queue_wait,
                    "decode_tokens_per_second": done.get(
                        "decode_tokens_per_second"
                    ),
                    "npu_prefill_compute_seconds": prompt_run,
                    "npu_prefill_input_prepare_seconds": max(
                        0.0, prompt_e2e - prompt_run
                    ),
                    "npu_prefill_compute_tokens_per_second": (
                        prompt_tokens / prompt_e2e if prompt_e2e > 0 else None
                    ),
                    "fp32_tail_compute_seconds": 0.0,
                    "vision_session_create_seconds": frontend["timing_seconds"].get(
                        "vision_session_create"
                    ),
                    "vision_run_seconds": frontend["timing_seconds"].get("vision_run"),
                    "decoder_session_create_seconds": ready.get(
                        "session_create_seconds"
                    ),
                    "decoder_first_token_seconds": done.get("first_token_seconds"),
                    "stage_wall_seconds": stage_wall,
                    "backend": {
                        "vision": None,
                        "prefill_layers_0_19": "npu_qnn_htp_p5_sequential",
                        "p5_prompt_state_repair": None,
                        "p5_prompt_state_repair_tokens": 0,
                        "tail_layers_20_23": "npu_qnn_htp_p5_sequential",
                        "decode": "npu_qnn_htp_p5",
                        "fallback_disabled": True,
                    },
                    "decoding": {
                        "requested": prepared.requested_decoding,
                        "effective": self._decoding_mode(prepared.requested_decoding),
                        "effective_parameters": self._effective_parameters(prepared.requested_decoding),
                        "tools_ignored": prepared.tools_ignored,
                    },
                    "image": None,
                    "finish_reason": decoder["finish_reason"],
                }
                # Full-P5 diagnostic evidence; inference is unchanged.

                metrics["backend"]["language_prefill_full24_npu"] = True
                metrics["host_tied_head_seconds"] = 0.0
                evidence_dir = self.paths["metrics_log"].parent / "handoffs"
                evidence_dir.mkdir(exist_ok=True)
                with (evidence_dir / (request_id + ".json")).open("x", encoding="utf-8") as f:
                    json.dump({"prompt_ids": prompt_ids, "prefill_policy": "full_p5_text"}, f, indent=2)
                with (evidence_dir / (request_id + "_decoder.json")).open("x", encoding="utf-8") as f:
                    json.dump({"completion_ids": completion_ids, "decoder": done,
                               "expected_rope_delta": 0,
                               "expected_generation_rope_start": prompt_tokens}, f, indent=2)
                self.metrics.record(metrics)
                return {
                    "text": generated_text,
                    "prompt_tokens": prompt_tokens,
                    "completion_tokens": len(completion_ids),
                    "finish_reason": decoder["finish_reason"],
                    "metrics": metrics,
                }

            from native_handoff import prefix_length, build_handoff
            aligned_tokens = prefix_length(prompt_tokens, p5_repair_tokens)
            remainder_tokens = prompt_tokens - aligned_tokens
            if self.persistent_decoder is None:
                raise PipelineError("npu_prefill", "native resident decoder is required")
            npu_dir = request_dir / "npu"
            npu_dir.mkdir()
            stage_started = time.perf_counter()
            native_prefill = self.persistent_decoder.prefill(
                request_id, aligned_tokens, bundle_path, npu_dir / "native.json")
            stage_wall["npu_prefill"] = time.perf_counter() - stage_started
            if native_prefill["prompt_tokens"] != prompt_tokens or native_prefill["rope_delta"] != rope_delta:
                raise PipelineError("npu_prefill", "native prompt/M-RoPE contract mismatch")
            seed_dir = request_dir / "seed"
            stage_started = time.perf_counter()
            seed = build_handoff(bundle_path, token_ids_path, aligned_tokens, request_id,
                                 seed_dir, prepared.mode == "image")
            stage_wall["seed_build"] = time.perf_counter() - stage_started
            npu = {"status": "pass", "state_storage": "native_resident_arena",
                   "token_count": aligned_tokens, "source_token_count": prompt_tokens,
                   "remainder_token_ids": seed["remainder_token_ids"],
                   "run_seconds": {"total": native_prefill["compute_seconds"]},
                   "preparation_seconds": {"total": native_prefill["preparation_seconds"]},
                   "native": native_prefill}
            with (npu_dir / "result.json").open("x", encoding="utf-8") as stream:
                json.dump(npu, stream, indent=2)
            stage_wall["fp32_tail"] = 0.0
            tail = {"timing_seconds": {"chunk_group_tail_wall": 0.0},
                    "backend": "qnn_htp_inside_full24_prefill"}
            host_evidence = {"seed": seed, "prefill": npu,
                             "prompt_ids": np.load(token_ids_path, allow_pickle=False).tolist()}
            evidence_dir = self.paths["metrics_log"].parent / "handoffs"
            evidence_dir.mkdir(exist_ok=True)
            with (evidence_dir / (request_id + ".json")).open("x", encoding="utf-8") as f:
                json.dump(host_evidence, f, ensure_ascii=False, indent=2)

            completion_ids: list[int] = []
            emitted_text = ""
            first_token_at: float | None = None

            def accept_token(token_id: int) -> None:
                nonlocal emitted_text, first_token_at
                if is_cancelled is not None and is_cancelled():
                    raise ClientDisconnected("SSE client disconnected during decode")
                if first_token_at is None:
                    first_token_at = time.perf_counter()
                completion_ids.append(int(token_id))
                decoded = decode_visible(self.tokenizer, completion_ids)
                if decoded.startswith(emitted_text):
                    delta = decoded[len(emitted_text) :]
                    emitted_text = decoded
                    if delta and on_text is not None:
                        on_text(delta)
                if is_cancelled is not None and is_cancelled():
                    raise ClientDisconnected("SSE client disconnected during decode")

            decoder = self._run_decoder(
                seed,
                effective_max_tokens,
                request_id,
                seed_dir / "p5_seed_inputs.bin",
                request_dir,
                prepared.requested_decoding,
                accept_token,
            )
            native_done = decoder.get("done") or {}
            if native_done.get("resident_seed") is not True:
                raise PipelineError("decoder", "native resident state was not consumed")
            for key, native_key in (("global_clip_fraction", "native_seed_clip_fraction"),
                                    ("max_local_clip_fraction", "native_seed_max_local_clip_fraction")):
                value = native_done.get(native_key)
                if not isinstance(value, (int, float)) or not np.isfinite(value) or not 0 <= value <= 1:
                    raise PipelineError("decoder", "native state encoding audit is missing")
                seed["quantization"][key] = value
            if seed["quantization"]["global_clip_fraction"] > 0.001:
                raise PipelineError("decoder", "native state clipping exceeds the accepted limit")
            stage_wall["decoder"] = float(decoder["engine_wall_seconds"])
            if not completion_ids or len(completion_ids) > effective_max_tokens:
                raise PipelineError("decoder", "OpenAI completion-token bound failed")
            generated_text = decode_visible(self.tokenizer, completion_ids)
            npu_run = float(npu["run_seconds"]["total"])
            npu_prepare = float(npu["preparation_seconds"]["total"])
            tail_compute = float(tail["timing_seconds"]["chunk_group_tail_wall"])
            done = decoder.get("done") or {}
            ready = decoder.get("ready") or {}
            finished_at = time.perf_counter()
            with (evidence_dir / (request_id + "_decoder.json")).open("x", encoding="utf-8") as f:
                json.dump({"completion_ids": completion_ids, "decoder": decoder["done"],
                           "expected_rope_delta": seed["rope_delta"],
                           "expected_generation_rope_start": seed["generation_rope_start"]}, f, indent=2)
            metrics = {
                "schema": "qwen35_q6a_openai_request_metrics_v1",
                "status": "pass",
                "request_id": request_id,
                "mode": prepared.mode,
                    "residency_policy": "all_resident_single_common_p5",
                    "decoder_model": str(self.paths["decoder_model"]),
                    "native_frequency_penalty": done.get("frequency_penalty"),
                "prompt_tokens": prompt_tokens,
                "aligned_prefix_tokens": aligned_tokens,
                "remainder_tokens": remainder_tokens,
                "p5_repair_tokens_requested": p5_repair_tokens,
                "completion_tokens": len(completion_ids),
                "total_tokens": prompt_tokens + len(completion_ids),
                "requested_max_tokens": prepared.max_tokens,
                "effective_max_tokens": effective_max_tokens,
                "context_capacity_tokens": context_capacity,
                "max_tokens_clamped_to_context": effective_max_tokens
                != prepared.max_tokens,
                "context_management": prepared.context_management,
                "ttft_seconds": (
                    first_token_at - request_started if first_token_at is not None else None
                ),
                "request_seconds": finished_at - request_started,
                "queue_wait_seconds": queue_wait,
                "decode_tokens_per_second": done.get("decode_tokens_per_second"),
                "prefill_policy": "native_chunk16_htp_suffix",
                "first_token_head_backend": "QNNExecutionProvider / HTP",
                "decoder_rope_delta": decoder["done"]["rope_delta"],
                "vector_vision_suffix": bool(decoder["done"].get("embedding_remainder",False)),
                "request_scratch_backend": "tmpfs" if self.request_scratch_root != self.scratch_root else "project_filesystem",
                "decoder_generation_rope_start": decoder["done"]["generation_rope_start"],
                "host_tied_head_seconds": seed["timing_seconds"]["tied_head"],
                "native_seed_encode_seconds": done["native_seed_encode_seconds"],
                "logit_correction_enabled": done.get("logit_correction_enabled", False),
                "logit_correction_runs": done.get("logit_correction_runs", 0),
                "logit_correction_seconds": done.get("logit_correction_seconds", 0.0),
                "seed_state_clip_fraction": seed["quantization"]["global_clip_fraction"],
                "seed_state_max_local_clip_fraction": seed["quantization"]["max_local_clip_fraction"],
                "prefill_cached_tokens": native_prefill["cached_prefix_tokens"],
                "prefill_executed_tokens": native_prefill["executed_tokens"],
                "prefill_prefix_cache_enabled": native_prefill["prefix_cache_enabled"],
                "prefill_cache_match_seconds": native_prefill["cache_match_seconds"],
                "prefill_retained_prompt_bytes": native_prefill["retained_prompt_bytes"],
                "npu_prefill_compute_seconds": npu_run,
                "npu_prefill_input_prepare_seconds": npu_prepare,
                "npu_prefill_compute_tokens_per_second": (
                    native_prefill["executed_tokens"] / npu_run if npu_run > 0 else None
                ),
                "fp32_tail_compute_seconds": tail_compute,
                "full_npu_tail_compute_included_in_prefill": (
                    self.full_npu_prefill
                ),
                "vision_session_create_seconds": frontend["timing_seconds"].get(
                    "vision_session_create"
                ),
                "vision_run_seconds": frontend["timing_seconds"].get("vision_run"),
                "decoder_session_create_seconds": ready.get("session_create_seconds"),
                "decoder_first_token_seconds": done.get("first_token_seconds"),
                "stage_wall_seconds": stage_wall,
                "backend": {
                    "vision": (
                        "npu_qnn_htp_fixed1024x576_three_stage"
                        if prepared.mode == "image"
                        else None
                    ),
                    "prefill_layers_0_19": "npu_qnn_htp_chunk16",
                    "language_prefill_full24_npu": self.full_npu_prefill,
                    "p5_prompt_state_repair": (
                        "npu_qnn_htp_sequential"
                        if remainder_tokens
                        else None
                    ),
                    "p5_prompt_state_repair_tokens": remainder_tokens,
                    "tail_layers_20_23": (
                        "npu_qnn_htp_chunk16_same_session"
                        if self.full_npu_prefill
                        else "cpu_fp32"
                    ),
                    "decode": "npu_qnn_htp_p5",
                    "fallback_disabled": True,
                },
                "decoding": {
                    "requested": prepared.requested_decoding,
                    "effective": self._decoding_mode(prepared.requested_decoding),
                        "effective_parameters": self._effective_parameters(prepared.requested_decoding),
                    "tools_ignored": prepared.tools_ignored,
                },
                "image": frontend.get("image"),
                "finish_reason": decoder["finish_reason"],
            }
            self.metrics.record(metrics)
            return {
                "text": generated_text,
                "prompt_tokens": prompt_tokens,
                "completion_tokens": len(completion_ids),
                "finish_reason": decoder["finish_reason"],
                "metrics": metrics,
            }
        except Exception as exc:
            cancelled = isinstance(exc, ClientDisconnected)
            decoder_restart_seconds: float | None = None
            decoder_restart_error: str | None = None
            if cancelled and self.persistent_decoder_enabled:
                try:
                    decoder_restart_seconds = self._restart_persistent_decoder()
                except Exception as restart_exc:
                    decoder_restart_error = (
                        f"{type(restart_exc).__name__}: {restart_exc}"
                    )[-1000:]
            self.metrics.record(
                {
                    "schema": "qwen35_q6a_openai_request_metrics_v1",
                    "status": "cancelled" if cancelled else "fail",
                    "request_id": request_id,
                    "mode": prepared.mode if prepared is not None else None,
                    "context_management": (
                        prepared.context_management if prepared is not None else None
                    ),
                    "request_seconds": time.perf_counter() - request_started,
                    "queue_wait_seconds": queue_wait,
                    "failed_stage": exc.stage if isinstance(exc, PipelineError) else None,
                    "error_type": type(exc).__name__,
                    "error": str(exc)[-3000:],
                    "client_disconnected": cancelled,
                    "decoder_restart_seconds": decoder_restart_seconds,
                    "decoder_restart_error": decoder_restart_error,
                    "stage_wall_seconds": stage_wall,
                }
            )
            raise
        finally:
            self.metrics.set_active(0)
            self.generation_lock.release()
            shutil.rmtree(request_dir, ignore_errors=True)
            gc.collect()


class APIServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], engine: Chunk16Engine) -> None:
        super().__init__(address, Handler)
        self.engine = engine


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server: APIServer

    def log_message(self, _format: str, *_args: Any) -> None:
        return

    def _cors(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Authorization, Content-Type")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")

    def _send_json(self, status: int, body: dict[str, Any]) -> None:
        payload = json_line(body).encode("utf-8")
        self.send_response(int(status))
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self._cors()
        self.end_headers()
        self.wfile.write(payload)

    def _authorized(self) -> bool:
        if self.server.engine.authorize(self.headers.get("Authorization")):
            return True
        self._send_json(HTTPStatus.UNAUTHORIZED, error_body(RequestError(
            "invalid or missing API key", "invalid_api_key", HTTPStatus.UNAUTHORIZED
        )))
        return False

    def _read_json(self) -> dict[str, Any]:
        value = self.headers.get("Content-Length")
        if value is None:
            raise RequestError("Content-Length is required")
        try:
            length = int(value)
        except ValueError as exc:
            raise RequestError("invalid Content-Length") from exc
        if not 2 <= length <= self.server.engine.max_request_bytes:
            raise RequestError(
                f"request body must be in [2, {self.server.engine.max_request_bytes}] bytes"
            )
        try:
            body = json.loads(self.rfile.read(length).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise RequestError("request body must be valid UTF-8 JSON") from exc
        if not isinstance(body, dict):
            raise RequestError("request body must be an object")
        return body

    def do_OPTIONS(self) -> None:
        self.send_response(HTTPStatus.NO_CONTENT)
        self.send_header("Content-Length", "0")
        self._cors()
        self.end_headers()

    def do_GET(self) -> None:
        if self.path == "/health":
            self._send_json(HTTPStatus.OK, self.server.engine.health())
            return
        if not self._authorized():
            return
        if self.path == "/v1/models":
            self._send_json(
                HTTPStatus.OK,
                {
                    "object": "list",
                    "data": [
                        {
                            "id": self.server.engine.model_id,
                            "object": "model",
                            "created": int(self.server.engine.started_at),
                            "owned_by": "local-q6a-qnn",
                        }
                    ],
                },
            )
        elif self.path == "/metrics":
            self._send_json(
                HTTPStatus.OK,
                self.server.engine.metrics.snapshot(self.server.engine.ready),
            )
        else:
            self._send_json(
                HTTPStatus.NOT_FOUND,
                error_body(RequestError("route not found", "not_found", HTTPStatus.NOT_FOUND)),
            )

    def _chat(self, request: dict[str, Any]) -> None:
        request_id = f"chatcmpl-{uuid.uuid4().hex}"
        created = int(time.time())
        request_started = time.perf_counter()
        stream = request.get("stream", False)
        if not isinstance(stream, bool):
            raise RequestError("stream must be a boolean", param="stream")
        print(json_line({
            "event": "chat_request",
            "request_id": request_id,
            "stream": stream,
            "message_count": len(request.get("messages", []))
            if isinstance(request.get("messages"), list)
            else None,
        }), flush=True)
        if not stream:
            result = self.server.engine.generate(request, request_id, request_started)
            body = {
                "id": request_id,
                "object": "chat.completion",
                "created": created,
                "model": self.server.engine.model_id,
                "choices": [
                    {
                        "index": 0,
                        "message": {"role": "assistant", "content": result["text"]},
                        "finish_reason": result["finish_reason"],
                    }
                ],
                "usage": {
                    "prompt_tokens": result["prompt_tokens"],
                    "completion_tokens": result["completion_tokens"],
                    "total_tokens": result["prompt_tokens"] + result["completion_tokens"],
                },
                "qwen35_metrics": result["metrics"],
            }
            self._send_json(HTTPStatus.OK, body)
            return

        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/event-stream; charset=utf-8")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self._cors()
        self.end_headers()
        disconnected = False

        def send_event(value: dict[str, Any]) -> None:
            nonlocal disconnected
            if disconnected:
                return
            try:
                self.wfile.write(f"data: {json_line(value)}\n\n".encode("utf-8"))
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                disconnected = True

        send_event({
            "id": request_id,
            "object": "chat.completion.chunk",
            "created": created,
            "model": self.server.engine.model_id,
            "choices": [
                {"index": 0, "delta": {"role": "assistant"}, "finish_reason": None}
            ],
        })

        def on_text(text: str) -> None:
            send_event({
                "id": request_id,
                "object": "chat.completion.chunk",
                "created": created,
                "model": self.server.engine.model_id,
                "choices": [
                    {"index": 0, "delta": {"content": text}, "finish_reason": None}
                ],
            })
            if disconnected:
                raise ClientDisconnected("SSE client disconnected during stream write")

        try:
            result = self.server.engine.generate(
                request,
                request_id,
                request_started,
                on_text=on_text,
                is_cancelled=lambda: disconnected,
            )
            send_event({
                "id": request_id,
                "object": "chat.completion.chunk",
                "created": created,
                "model": self.server.engine.model_id,
                "choices": [
                    {"index": 0, "delta": {}, "finish_reason": result["finish_reason"]}
                ],
                "usage": {
                    "prompt_tokens": result["prompt_tokens"],
                    "completion_tokens": result["completion_tokens"],
                    "total_tokens": result["prompt_tokens"] + result["completion_tokens"],
                },
                "qwen35_metrics": result["metrics"],
            })
        except Exception as exc:
            send_event(error_body(exc))
        if not disconnected:
            try:
                self.wfile.write(b"data: [DONE]\n\n")
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
        self.close_connection = True

    def do_POST(self) -> None:
        if not self._authorized():
            return
        try:
            if self.path != "/v1/chat/completions":
                raise RequestError("route not found", "not_found", HTTPStatus.NOT_FOUND)
            self._chat(self._read_json())
        except RequestError as exc:
            self._send_json(exc.status, error_body(exc))
        except Exception as exc:
            print(json_line({
                "event": "request_error",
                "path": self.path,
                "error_type": type(exc).__name__,
                "error": str(exc)[-1000:],
            }), flush=True)
            self._send_json(HTTPStatus.INTERNAL_SERVER_ERROR, error_body(exc))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    engine: Chunk16Engine | None = None
    server: APIServer | None = None
    try:
        engine = Chunk16Engine(args.config)
        server = APIServer((engine.host, engine.port), engine)
        from host_memory import release_startup_heap
        print(json_line({"event":"startup_heap_release", **release_startup_heap()}), flush=True)

        def stop_handler(signum: int, _frame: Any) -> None:
            print(json_line({
                "event": "signal_shutdown",
                "signal": signal.Signals(signum).name,
            }), flush=True)
            threading.Thread(target=server.shutdown, daemon=True).start()

        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        signal.signal(signal.SIGTERM, stop_handler)
        signal.signal(signal.SIGINT, stop_handler)
        print(json_line({
            "ready": True,
            "model": engine.model_id,
            "url": f"http://{engine.host}:{engine.port}",
            "config": str(engine.config_path),
        }), flush=True)
        server.serve_forever(poll_interval=0.2)
        return 0
    except Exception:
        traceback.print_exc()
        return 1
    finally:
        if server is not None:
            server.server_close()
        if engine is not None:
            engine.close()
        gc.collect()


if __name__ == "__main__":
    raise SystemExit(main())
