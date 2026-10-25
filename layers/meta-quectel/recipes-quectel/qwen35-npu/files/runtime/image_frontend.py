#!/usr/bin/env python3
"""Dependency-light Qwen3.5-0.8B still-image frontend for the Dragon Q6A.

The accepted runtime path deliberately keeps the vision tower on the open ONNX
Runtime CPU EP and feeds its 1024-wide image embeddings into the frozen strict
QNN decoder.  This frontend reproduces the pinned Transformers 5.9.0 image
packing, chat expansion, M-RoPE indexing, and embedding scatter without
requiring PyTorch or Transformers on the board.

runtime's first practical contract is one RGB image at exactly 1024x576.  Arbitrary
resolution policy is intentionally kept out of this binary-compatible canary.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import struct
import time
from pathlib import Path
from typing import Any

import numpy as np
import onnxruntime as ort
from tokenizers import Tokenizer

try:
    from PIL import Image, ImageOps
except ModuleNotFoundError:
    # runtime reuses this module's binary bundle/M-RoPE helpers for Text requests.
    # Pillow remains mandatory when this runtime image CLI itself is executed.
    Image = None  # type: ignore[assignment]
    ImageOps = None  # type: ignore[assignment]

try:
    import resource
except ModuleNotFoundError:  # Windows host-side oracle validation only.
    resource = None  # type: ignore[assignment]


MAGIC = b"R20VLM1\0"
VERSION = 1
HEADER = struct.Struct("<8sIIIiIIIIQQQQQ")
CONTEXT = 2048
VOCAB = 248_320
HIDDEN = 1024
ROTARY = 64
IMAGE_TOKEN_ID = 248_056
IMAGE_TOKENS = 576
PATCHES = 2304
PATCH_WIDTH = 1536
IMAGE_WIDTH = 1024
IMAGE_HEIGHT = 576


def align(value: int, alignment: int = 64) -> int:
    return (value + alignment - 1) // alignment * alignment


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(8 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def array_sha256(value: np.ndarray) -> str:
    canonical = np.ascontiguousarray(value)
    return hashlib.sha256(canonical.tobytes(order="C")).hexdigest()


def array_record(value: np.ndarray) -> dict[str, Any]:
    return {
        "shape": list(value.shape),
        "dtype": str(value.dtype),
        "finite": bool(np.isfinite(value).all()) if np.issubdtype(value.dtype, np.floating) else None,
        "sha256_canonical_bytes": array_sha256(value),
    }


def file_record(path: Path) -> dict[str, Any]:
    return {
        "path": str(path.resolve()),
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
    }


def metrics(actual: np.ndarray, expected: np.ndarray) -> dict[str, Any]:
    left = np.asarray(actual, dtype=np.float64).reshape(-1)
    right = np.asarray(expected, dtype=np.float64).reshape(-1)
    if left.shape != right.shape:
        return {"shape_match": False, "actual_shape": list(actual.shape), "expected_shape": list(expected.shape)}
    delta = left - right
    left_norm = float(np.linalg.norm(left))
    right_norm = float(np.linalg.norm(right))
    return {
        "shape_match": True,
        "shape": list(actual.shape),
        "max_abs": float(np.abs(delta).max(initial=0.0)),
        "mean_abs": float(np.abs(delta).mean()) if delta.size else 0.0,
        "relative_l2": float(np.linalg.norm(delta) / max(right_norm, 1e-30)),
        "cosine": float(np.dot(left, right) / max(left_norm * right_norm, 1e-30)),
        "bit_exact": bool(np.array_equal(actual, expected)),
    }


def thermal_snapshot() -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for zone in sorted(Path("/sys/class/thermal").glob("thermal_zone*")):
        try:
            kind = (zone / "type").read_text(encoding="utf-8").strip()
            millidegrees = int((zone / "temp").read_text(encoding="utf-8").strip())
        except (OSError, ValueError):
            continue
        rows.append({"zone": zone.name, "type": kind, "millidegrees_c": millidegrees})
    return rows


def render_single_image_prompt(prompt: str) -> str:
    if not prompt:
        raise ValueError("prompt must not be empty")
    image_placeholders = "<|image_pad|>" * IMAGE_TOKENS
    return (
        "<|im_start|>user\n"
        "<|vision_start|>"
        f"{image_placeholders}"
        "<|vision_end|>"
        f"{prompt}"
        "<|im_end|>\n"
        "<|im_start|>assistant\n"
        "<think>\n\n</think>\n\n"
    )


def preprocess_384(image_path: Path) -> tuple[np.ndarray, dict[str, Any]]:
    if Image is None or ImageOps is None:
        raise RuntimeError("Pillow is required for still-image preprocessing")
    with Image.open(image_path) as source:
        transposed = ImageOps.exif_transpose(source)
        rgb = transposed.convert("RGB")
        original_size = list(source.size)
        oriented_size = list(rgb.size)
        if rgb.size != (IMAGE_WIDTH, IMAGE_HEIGHT):
            raise ValueError(
                f"runtime fixed-image contract requires 1024x576 after EXIF orientation; got {rgb.size}"
            )
        uint8_hwc = np.asarray(rgb, dtype=np.uint8)

    # Pinned Qwen3.5 preprocessor has rescale=1/255, mean=std=0.5.
    # This fused FP32 expression is bit-identical to the official processor for
    # the canonical runtime fixture and avoids two unnecessary array traversals.
    normalized_hwc = (
        uint8_hwc.astype(np.float32) - np.float32(127.5)
    ) / np.float32(127.5)
    chw = np.transpose(normalized_hwc, (2, 0, 1))
    patches = chw.reshape(1, 3, 18, 2, 16, 32, 2, 16)
    patches = patches.transpose(0, 2, 5, 3, 6, 1, 4, 7)
    patches = np.expand_dims(patches, axis=6)
    patches = np.broadcast_to(patches, (1, 18, 32, 2, 2, 3, 2, 16, 16))
    pixel_values = np.ascontiguousarray(patches.reshape(PATCHES, PATCH_WIDTH), dtype="<f4")
    if pixel_values.shape != (PATCHES, PATCH_WIDTH) or not np.isfinite(pixel_values).all():
        raise RuntimeError("packed pixel tensor violates the static vision contract")
    return pixel_values, {
        "original_size_wh": original_size,
        "oriented_rgb_size_wh": oriented_size,
        "mode_after_conversion": "RGB",
        "exif_transpose_applied": True,
    }


def build_position_ids(mm_types: np.ndarray) -> tuple[np.ndarray, int]:
    if mm_types.ndim != 1 or mm_types.size == 0:
        raise ValueError("mm token types must be a non-empty vector")
    groups: list[tuple[int, int, int]] = []
    start = 0
    for index in range(1, mm_types.size + 1):
        if index == mm_types.size or mm_types[index] != mm_types[start]:
            groups.append((int(mm_types[start]), start, index))
            start = index

    current_pos = 0
    pieces: list[np.ndarray] = []
    image_groups = 0
    for modality, begin, end in groups:
        length = end - begin
        if modality == 0:
            values = np.arange(length, dtype=np.int64) + current_pos
            pieces.append(np.broadcast_to(values, (3, length)).copy())
            current_pos += length
        elif modality == 1:
            image_groups += 1
            if length != IMAGE_TOKENS or image_groups != 1:
                raise ValueError("runtime frontend supports exactly one contiguous 576-token image group")
            height = np.arange(18, dtype=np.int64) + current_pos
            width = np.arange(32, dtype=np.int64) + current_pos
            temporal = np.full(IMAGE_TOKENS, current_pos, dtype=np.int64)
            pieces.append(
                np.stack(
                    [
                        temporal,
                        np.repeat(height, 32),
                        np.tile(width, 18),
                    ],
                    axis=0,
                )
            )
            current_pos += 32
        else:
            raise ValueError(f"unsupported modality type {modality}")

    if image_groups != 1:
        raise ValueError("exactly one image group is required")
    position_ids = np.ascontiguousarray(np.concatenate(pieces, axis=1), dtype="<i8")
    rope_delta = int(position_ids.max() + 1 - mm_types.size)
    return position_ids, rope_delta


def build_mrope(position_ids: np.ndarray, rope_path: Path) -> tuple[np.ndarray, np.ndarray]:
    expected_bytes = 2 * CONTEXT * ROTARY * np.dtype("<f4").itemsize
    if rope_path.stat().st_size != expected_bytes:
        raise RuntimeError(f"unexpected RoPE asset size: {rope_path.stat().st_size}")
    tables = np.fromfile(rope_path, dtype="<f4").reshape(2, CONTEXT, ROTARY)
    if position_ids.min() < 0 or position_ids.max() >= CONTEXT:
        raise RuntimeError("M-RoPE positions exceed the fixed context table")
    outputs: list[np.ndarray] = []
    for table in tables:
        half = table[position_ids[0], : ROTARY // 2].copy()
        # Pinned interleaved section [11, 11, 10]: T remains in all other
        # frequency lanes, H replaces 1::3 through 31, W replaces 2::3 through 29.
        half[:, 1:33:3] = table[position_ids[1], : ROTARY // 2][:, 1:33:3]
        half[:, 2:30:3] = table[position_ids[2], : ROTARY // 2][:, 2:30:3]
        outputs.append(np.ascontiguousarray(np.concatenate([half, half], axis=1), dtype="<f4"))
    return outputs[0], outputs[1]


def build_bundle(
    output_path: Path,
    input_ids: np.ndarray,
    hidden: np.ndarray,
    cosine: np.ndarray,
    sine: np.ndarray,
    rope_delta: int,
) -> dict[str, int]:
    sequence = int(input_ids.size)
    if (
        input_ids.shape != (sequence,)
        or hidden.shape != (sequence, HIDDEN)
        or cosine.shape != (sequence, ROTARY)
        or sine.shape != (sequence, ROTARY)
    ):
        raise RuntimeError("bundle array shape mismatch")
    ids = np.ascontiguousarray(input_ids, dtype="<i8")
    hidden = np.ascontiguousarray(hidden, dtype="<f4")
    cosine = np.ascontiguousarray(cosine, dtype="<f4")
    sine = np.ascontiguousarray(sine, dtype="<f4")
    ids_offset = align(HEADER.size)
    hidden_offset = align(ids_offset + ids.nbytes)
    cosine_offset = align(hidden_offset + hidden.nbytes)
    sine_offset = align(cosine_offset + cosine.nbytes)
    total_bytes = sine_offset + sine.nbytes
    payload = bytearray(total_bytes)
    HEADER.pack_into(
        payload,
        0,
        MAGIC,
        VERSION,
        HEADER.size,
        sequence,
        rope_delta,
        HIDDEN,
        ROTARY,
        VOCAB,
        0,
        ids_offset,
        hidden_offset,
        cosine_offset,
        sine_offset,
        total_bytes,
    )
    payload[ids_offset : ids_offset + ids.nbytes] = ids.tobytes(order="C")
    payload[hidden_offset : hidden_offset + hidden.nbytes] = hidden.tobytes(order="C")
    payload[cosine_offset : cosine_offset + cosine.nbytes] = cosine.tobytes(order="C")
    payload[sine_offset : sine_offset + sine.nbytes] = sine.tobytes(order="C")
    output_path.write_bytes(payload)
    return {
        "sequence": sequence,
        "rope_delta": rope_delta,
        "generation_rope_start": sequence + rope_delta,
        "ids_offset": ids_offset,
        "hidden_offset": hidden_offset,
        "cosine_offset": cosine_offset,
        "sine_offset": sine_offset,
        "total_bytes": total_bytes,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--prompt", required=True)
    parser.add_argument("--vision-model", required=True, type=Path)
    parser.add_argument("--tokenizer-json", required=True, type=Path)
    parser.add_argument("--embedding-npy", required=True, type=Path)
    parser.add_argument("--rope-bin", required=True, type=Path)
    parser.add_argument("--output-bundle", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--debug-npz", type=Path)
    parser.add_argument("--oracle-npz", type=Path)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--runs", type=int, default=1)
    args = parser.parse_args()
    if args.threads <= 0 or args.runs <= 0:
        raise ValueError("threads and runs must be positive")
    required = [args.image, args.vision_model, args.tokenizer_json, args.embedding_npy, args.rope_bin]
    if args.oracle_npz is not None:
        required.append(args.oracle_npz)
    for path in required:
        if not path.is_file():
            raise FileNotFoundError(path)
    for output in (args.output_bundle, args.report, args.debug_npz):
        if output is not None:
            output.parent.mkdir(parents=True, exist_ok=True)

    started = time.perf_counter()
    temperature_before = thermal_snapshot()
    preprocess_started = time.perf_counter()
    pixel_values, image_info = preprocess_384(args.image)
    preprocess_seconds = time.perf_counter() - preprocess_started

    tokenizer_started = time.perf_counter()
    tokenizer = Tokenizer.from_file(str(args.tokenizer_json.resolve()))
    rendered = render_single_image_prompt(args.prompt)
    input_ids = np.ascontiguousarray(
        tokenizer.encode(rendered, add_special_tokens=False).ids, dtype="<i8"
    )
    if input_ids.size >= CONTEXT or (input_ids < 0).any() or (input_ids >= VOCAB).any():
        raise RuntimeError("tokenized prompt is outside the frozen decoder contract")
    image_rows = np.flatnonzero(input_ids == IMAGE_TOKEN_ID)
    if image_rows.shape != (IMAGE_TOKENS,) or not np.array_equal(
        image_rows, np.arange(image_rows[0], image_rows[0] + IMAGE_TOKENS)
    ):
        raise RuntimeError("tokenizer did not produce one contiguous 576-token image region")
    mm_types = np.zeros(input_ids.shape, dtype="<i8")
    mm_types[image_rows] = 1
    position_ids, rope_delta = build_position_ids(mm_types)
    cosine, sine = build_mrope(position_ids, args.rope_bin)
    if not 0 <= input_ids.size + rope_delta < CONTEXT:
        raise RuntimeError("generation M-RoPE start exceeds the fixed context")
    tokenize_and_rope_seconds = time.perf_counter() - tokenizer_started

    options = ort.SessionOptions()
    options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
    options.intra_op_num_threads = args.threads
    options.inter_op_num_threads = 1
    options.enable_mem_pattern = True
    session_started = time.perf_counter()
    session = ort.InferenceSession(
        str(args.vision_model.resolve()),
        sess_options=options,
        providers=["CPUExecutionProvider"],
    )
    session_seconds = time.perf_counter() - session_started
    if session.get_providers() != ["CPUExecutionProvider"]:
        raise RuntimeError(f"unexpected vision providers: {session.get_providers()}")
    inputs = session.get_inputs()
    outputs = session.get_outputs()
    if (
        len(inputs) != 1
        or inputs[0].name != "pixel_values"
        or inputs[0].shape != [PATCHES, PATCH_WIDTH]
        or len(outputs) != 1
        or outputs[0].name != "image_embeddings"
        or outputs[0].shape != [IMAGE_TOKENS, HIDDEN]
    ):
        raise RuntimeError("static full-vision ONNX IO contract mismatch")

    vision_seconds: list[float] = []
    vision_outputs: list[np.ndarray] = []
    for _ in range(args.runs):
        vision_started = time.perf_counter()
        value = session.run(None, {"pixel_values": pixel_values})[0]
        vision_seconds.append(time.perf_counter() - vision_started)
        vision_outputs.append(np.ascontiguousarray(value, dtype="<f4"))
    image_embeddings = vision_outputs[0]
    deterministic = all(np.array_equal(image_embeddings, value) for value in vision_outputs[1:])
    if image_embeddings.shape != (IMAGE_TOKENS, HIDDEN) or not np.isfinite(image_embeddings).all():
        raise RuntimeError("vision output violates the decoder scatter contract")

    scatter_started = time.perf_counter()
    embedding = np.load(args.embedding_npy, mmap_mode="r", allow_pickle=False)
    if embedding.shape != (VOCAB, HIDDEN) or embedding.dtype != np.dtype("float32"):
        raise RuntimeError("embedding table contract mismatch")
    token_embeddings = np.ascontiguousarray(embedding[input_ids], dtype="<f4")
    decoder_inputs = token_embeddings.copy()
    decoder_inputs[image_rows] = image_embeddings
    scatter_seconds = time.perf_counter() - scatter_started

    bundle_info = build_bundle(
        args.output_bundle,
        input_ids,
        decoder_inputs,
        cosine,
        sine,
        rope_delta,
    )
    if args.debug_npz is not None:
        np.savez_compressed(
            args.debug_npz,
            input_ids=input_ids,
            mm_token_type_ids=mm_types,
            pixel_values=pixel_values,
            image_embeddings=image_embeddings,
            token_embeddings_before_scatter=token_embeddings,
            decoder_inputs_after_scatter=decoder_inputs,
            position_ids=position_ids.reshape(3, 1, -1),
            rope_deltas=np.asarray([[rope_delta]], dtype="<i8"),
            rotary_cos=cosine.reshape(1, -1, ROTARY),
            rotary_sin=sine.reshape(1, -1, ROTARY),
        )

    oracle_comparisons: dict[str, Any] = {}
    oracle_gates: dict[str, bool] = {}
    if args.oracle_npz is not None:
        with np.load(args.oracle_npz, allow_pickle=False) as oracle:
            exact_pairs = {
                "input_ids": (input_ids.reshape(1, -1), oracle["input_ids"]),
                "mm_token_type_ids": (mm_types.reshape(1, -1), oracle["mm_token_type_ids"]),
                "pixel_values": (pixel_values, oracle["pixel_values"]),
                "token_embeddings_before_scatter": (
                    token_embeddings.reshape(1, -1, HIDDEN),
                    oracle["token_embeddings_before_scatter"],
                ),
                "position_ids": (position_ids.reshape(3, 1, -1), oracle["position_ids"]),
                "rope_deltas": (np.asarray([[rope_delta]], dtype="<i8"), oracle["rope_deltas"]),
                "rotary_cos": (cosine.reshape(1, -1, ROTARY), oracle["rotary_cos"]),
                "rotary_sin": (sine.reshape(1, -1, ROTARY), oracle["rotary_sin"]),
            }
            for name, (actual, expected) in exact_pairs.items():
                result = metrics(actual, expected)
                oracle_comparisons[name] = result
                oracle_gates[f"oracle_{name}_bit_exact"] = bool(result.get("bit_exact", False))
            oracle_comparisons["image_embeddings"] = metrics(
                image_embeddings, oracle["image_embeds"]
            )
            oracle_comparisons["decoder_inputs_after_scatter"] = metrics(
                decoder_inputs.reshape(1, -1, HIDDEN), oracle["decoder_inputs_after_scatter"]
            )
        image_metric = oracle_comparisons["image_embeddings"]
        oracle_gates.update(
            {
                "oracle_image_embeddings_relative_l2_at_most_1e_4": image_metric.get("relative_l2", 1.0) <= 1e-4,
                "oracle_image_embeddings_max_abs_at_most_5e_3": image_metric.get("max_abs", 1.0) <= 5e-3,
                "oracle_image_embeddings_cosine_at_least_0_99999": image_metric.get("cosine", 0.0) >= 0.99999,
            }
        )

    temperature_after = thermal_snapshot()
    max_temperature = max(
        [row["millidegrees_c"] for row in temperature_before + temperature_after],
        default=None,
    )
    gates = {
        "fixed_image_contract_1024x576": image_info["oriented_rgb_size_wh"] == [1024, 576],
        "pixel_shape_2304x1536": pixel_values.shape == (PATCHES, PATCH_WIDTH),
        "image_placeholder_count_576": image_rows.shape == (IMAGE_TOKENS,),
        "image_placeholders_contiguous": bool(
            np.array_equal(image_rows, np.arange(image_rows[0], image_rows[0] + IMAGE_TOKENS))
        ),
        "position_ids_shape_3xseq": position_ids.shape == (3, input_ids.size),
        "generation_mrope_in_context": 0 <= bundle_info["generation_rope_start"] < CONTEXT,
        "cpu_ep_only": session.get_providers() == ["CPUExecutionProvider"],
        "vision_all_finite": all(np.isfinite(value).all() for value in vision_outputs),
        "vision_repeated_runs_bit_exact": deterministic,
        "scatter_bit_exact": bool(np.array_equal(decoder_inputs[image_rows], image_embeddings)),
        "bundle_bytes_exact": args.output_bundle.stat().st_size == bundle_info["total_bytes"],
        **oracle_gates,
    }
    gates["all_pass"] = all(gates.values())
    report = {
        "schema": "qwen35_q6a_raw_still_frontend_v1",
        "status": "pass" if gates["all_pass"] else "fail",
        "decision": "open_raw_image_to_strict_qnn_decoder" if gates["all_pass"] else "fail_closed",
        "device": {
            "hostname": platform.node(),
            "machine": platform.machine(),
            "boot_id": (
                Path("/proc/sys/kernel/random/boot_id").read_text(encoding="utf-8").strip()
                if Path("/proc/sys/kernel/random/boot_id").is_file()
                else None
            ),
        },
        "runtime": {
            "python": platform.python_version(),
            "numpy": np.__version__,
            "onnxruntime": ort.__version__,
            "pillow": getattr(Image, "__version__", "unavailable"),
            "tokenizers": getattr(__import__("tokenizers"), "__version__", "unknown"),
            "providers": session.get_providers(),
            "threads": args.threads,
            "runs": args.runs,
            "preprocess_seconds": preprocess_seconds,
            "tokenize_and_mrope_seconds": tokenize_and_rope_seconds,
            "session_create_seconds": session_seconds,
            "vision_run_seconds": vision_seconds,
            "scatter_seconds": scatter_seconds,
            "total_seconds": time.perf_counter() - started,
            "process_max_rss_bytes": (
                int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * 1024)
                if resource is not None
                else None
            ),
            "temperature_before": temperature_before,
            "temperature_after": temperature_after,
            "maximum_snapshot_temperature_c": None if max_temperature is None else max_temperature / 1000.0,
        },
        "request": {
            "image": file_record(args.image),
            "image_decode": image_info,
            "prompt": args.prompt,
            "prompt_utf8_sha256": hashlib.sha256(args.prompt.encode("utf-8")).hexdigest(),
            "rendered_prompt_utf8_sha256": hashlib.sha256(rendered.encode("utf-8")).hexdigest(),
        },
        "lineage": {
            "vision_model": file_record(args.vision_model),
            "tokenizer_json": file_record(args.tokenizer_json),
            "embedding_npy": file_record(args.embedding_npy),
            "rope_bin": file_record(args.rope_bin),
            "oracle_npz": file_record(args.oracle_npz) if args.oracle_npz is not None else None,
            "output_bundle": file_record(args.output_bundle),
            "debug_npz": file_record(args.debug_npz) if args.debug_npz is not None else None,
        },
        "contract": {
            "context": CONTEXT,
            "vocab": VOCAB,
            "hidden": HIDDEN,
            "rotary": ROTARY,
            "image_size": [IMAGE_WIDTH, IMAGE_HEIGHT],
            "image_grid_thw": [1, 36, 64],
            "image_tokens": IMAGE_TOKENS,
            "mrope_section": [11, 11, 10],
            "mrope_interleaved": True,
            "normalization": "(uint8_fp32 - 127.5f) / 127.5f",
            "resize_policy": "reject_non_1024x576",
        },
        "arrays": {
            "input_ids": array_record(input_ids),
            "mm_token_type_ids": array_record(mm_types),
            "pixel_values": array_record(pixel_values),
            "image_embeddings": array_record(image_embeddings),
            "decoder_inputs_after_scatter": array_record(decoder_inputs),
            "position_ids": array_record(position_ids),
            "rotary_cos": array_record(cosine),
            "rotary_sin": array_record(sine),
        },
        "bundle": bundle_info,
        "oracle_comparisons": oracle_comparisons,
        "gates": gates,
        "guardrails": {
            "standard_onnx_cpu_ep_for_vision": True,
            "strict_qnn_decoder_is_downstream": True,
            "sudo_or_root": False,
            "lfm_touched": False,
        },
    }
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if gates["all_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
