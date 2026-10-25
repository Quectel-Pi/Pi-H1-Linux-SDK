#!/usr/bin/env python3
"""Build a P5 decoder seed directly from dynamic NPU-body and FP32-tail state."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import time
from pathlib import Path

import numpy as np

from runtime_cache import savez as runtime_savez
from runtime_cache import sha256 as runtime_sha256
from prompt_bundle import load_vlm_prompt_bundle


ATTENTION_LAYERS = (3, 7, 11, 15, 19, 23)
DELTA_LAYERS = tuple(index for index in range(24) if index not in ATTENTION_LAYERS)
CONTEXT = 2048
VOCAB = 248320
HIDDEN = 1024
ROTARY = 64


def sha256(path: Path) -> str:
    return runtime_sha256(path)


def artifact(path: Path) -> dict[str, object]:
    return {
        "path": str(path.resolve()),
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
    }


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def parse_shape(raw: str) -> tuple[int, ...]:
    return tuple(int(value) for value in raw.split(",") if value)


def quantize_u16(
    value: np.ndarray, scale: float, zero_point: int
) -> tuple[np.ndarray, dict[str, object]]:
    source = np.asarray(value, dtype=np.float32)
    raw = np.rint(source / np.float32(scale)).astype(np.float64) + float(zero_point)
    below = int(np.count_nonzero(raw < 0.0))
    above = int(np.count_nonzero(raw > 65535.0))
    return np.clip(raw, 0.0, 65535.0).astype(np.uint16), {
        "clipped_count": below + above,
        "below_count": below,
        "above_count": above,
        "total_count": int(raw.size),
        "clip_fraction": float((below + above) / raw.size),
    }


def recurrent_slice(name: str, state: np.ndarray) -> np.ndarray:
    head_match = re.search(r"/head_(\d+)/", name)
    if head_match is None:
        raise RuntimeError(f"cannot parse recurrent destination: {name}")
    head = int(head_match.group(1))
    lane_match = re.search(r"/lane_(\d+)/", name)
    half_match = re.search(r"/half_(\d+)/", name)
    if "/headlocal_recurrence/" in name:
        key = (slice(None), head, slice(None), slice(None))
    elif "/value_lane_recurrence/" in name and lane_match is not None:
        lane = int(lane_match.group(1))
        key = (slice(None), head, slice(None), slice(lane * 16, lane * 16 + 16))
    elif (
        "/selective_lane8_refinement/" in name
        and lane_match is not None
        and half_match is not None
    ):
        lane = int(lane_match.group(1))
        half = int(half_match.group(1))
        start = lane * 16 + half * 8
        key = (slice(None), head, slice(None), slice(start, start + 8))
    else:
        raise RuntimeError(f"unsupported recurrent destination: {name}")
    return np.ascontiguousarray(state[key][:, None], dtype=np.float32)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--npu-final-state", type=Path, required=True)
    parser.add_argument("--tail-output", type=Path, required=True)
    parser.add_argument("--embedding", type=Path, required=True)
    parser.add_argument("--norm-weight", type=Path, required=True)
    parser.add_argument("--rope", type=Path, required=True)
    parser.add_argument("--tensor-contract", type=Path, required=True)
    parser.add_argument("--advance-plan", type=Path, required=True)
    remainder_source = parser.add_mutually_exclusive_group()
    remainder_source.add_argument(
        "--full-token-ids-npy",
        type=Path,
        help="Optional full prompt. A bounded suffix is consumed by P5 after the aligned NPU prefix.",
    )
    remainder_source.add_argument(
        "--vlm-prompt-bundle",
        type=Path,
        help="Full multimodal prompt with scattered embeddings, M-RoPE, and optional suffix.",
    )
    parser.add_argument(
        "--max-remainder-tokens",
        type=int,
        default=15,
        help="Maximum P5 repair/remainder suffix. The production-compatible default remains 15.",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--expected-first-token", type=int)
    parser.add_argument("--expected-seed-sha256")
    parser.add_argument("--vector-vision-suffix",action="store_true")
    args = parser.parse_args()
    sources = (
        args.npu_final_state,
        args.tail_output,
        args.embedding,
        args.norm_weight,
        args.rope,
        args.tensor_contract,
        args.advance_plan,
    ) + (
        (args.full_token_ids_npy,)
        if args.full_token_ids_npy is not None
        else ((args.vlm_prompt_bundle,) if args.vlm_prompt_bundle is not None else ())
    )
    for path in sources:
        if not path.is_file():
            raise FileNotFoundError(path)
    if not 1 <= args.max_remainder_tokens < CONTEXT:
        raise ValueError("max-remainder-tokens must be in [1, 2047]")
    if args.output_dir.exists():
        raise FileExistsError(args.output_dir)
    args.output_dir.mkdir(parents=True)

    started = time.perf_counter()
    from state_handoff import load_arrays
    npu = load_arrays(args.npu_final_state)
    tail = load_arrays(args.tail_output)
    tokens = np.asarray(npu["token_ids"], dtype=np.int64)
    if (
        tokens.ndim != 1
        or tokens.size < 16
        or tokens.size > 2032
        or tokens.size % 16 != 0
        or np.any(tokens < 0)
        or np.any(tokens >= VOCAB)
    ):
        raise RuntimeError("NPU final-state token history contract mismatch")
    position = int(tokens.size)
    remainder_tokens = np.empty((0,), dtype=np.int64)
    full_tokens = tokens
    vlm_bundle = None
    rope_delta = 0
    remainder_mode = False
    if args.full_token_ids_npy is not None:
        full_tokens = np.asarray(
            np.load(args.full_token_ids_npy, allow_pickle=False), dtype=np.int64
        )
        if (
            full_tokens.ndim != 1
            or not 1 <= full_tokens.size - tokens.size <= args.max_remainder_tokens
            or full_tokens.size > CONTEXT - 1
            or not np.array_equal(full_tokens[: tokens.size], tokens)
            or np.any(full_tokens < 0)
            or np.any(full_tokens >= VOCAB)
        ):
            raise RuntimeError("full prompt / aligned-prefix remainder contract mismatch")
        if args.expected_first_token is not None:
            raise RuntimeError("expected-first-token is invalid for a remainder handoff")
        remainder_tokens = np.ascontiguousarray(full_tokens[tokens.size :], dtype=np.int64)
        remainder_mode = True
    elif args.vlm_prompt_bundle is not None:
        vlm_bundle = load_vlm_prompt_bundle(args.vlm_prompt_bundle)
        full_tokens = np.asarray(vlm_bundle["input_ids"], dtype=np.int64)
        rope_delta = int(vlm_bundle["rope_delta"])
        suffix = int(full_tokens.size - tokens.size)
        if (
            full_tokens.ndim != 1
            or suffix < 0
            or suffix > args.max_remainder_tokens
            or full_tokens.size > CONTEXT - 1
            or not np.array_equal(full_tokens[: tokens.size], tokens)
            or np.any(full_tokens < 0)
            or np.any(full_tokens >= VOCAB)
            or not 0 <= full_tokens.size + rope_delta < CONTEXT
        ):
            raise RuntimeError("VLM bundle / aligned-prefix contract mismatch")
        if suffix:
            if args.expected_first_token is not None:
                raise RuntimeError("expected-first-token is invalid for a VLM remainder handoff")
            remainder_tokens = np.ascontiguousarray(full_tokens[tokens.size :], dtype=np.int64)
            remainder_mode = True

    if not remainder_mode:
        raise RuntimeError("layered forbids CPU tied-head execution; reserve an HTP prompt suffix")
    embedding = np.load(args.embedding, mmap_mode="r", allow_pickle=False)
    norm_weight = np.load(args.norm_weight, allow_pickle=False)
    rope_raw = np.memmap(args.rope, mode="r", dtype="<f4")
    if embedding.shape != (VOCAB, HIDDEN) or embedding.dtype != np.float32:
        raise RuntimeError("embedding contract mismatch")
    if norm_weight.shape != (HIDDEN,) or norm_weight.dtype != np.float32:
        raise RuntimeError("final norm contract mismatch")
    if rope_raw.size != 2 * CONTEXT * ROTARY:
        raise RuntimeError("RoPE binary contract mismatch")
    rope_raw = rope_raw.reshape(2, CONTEXT, ROTARY)
    seed_rope_position = position + rope_delta
    vector_suffix = args.vector_vision_suffix and vlm_bundle is not None and remainder_mode
    if not vector_suffix and not 0 <= seed_rope_position < CONTEXT:
        raise RuntimeError("seed M-RoPE position is outside fixed context")
    remainder_rotary_scalarizable = True
    if vlm_bundle is not None and remainder_mode and not vector_suffix:
        remainder_positions = np.arange(position, full_tokens.size, dtype=np.int64) + rope_delta
        remainder_rotary_scalarizable = bool(
            np.array_equal(
                vlm_bundle["rope_cos"][position : full_tokens.size],
                rope_raw[0, remainder_positions],
            )
            and np.array_equal(
                vlm_bundle["rope_sin"][position : full_tokens.size],
                rope_raw[1, remainder_positions],
            )
        )
        if not remainder_rotary_scalarizable:
            raise RuntimeError("VLM remainder crosses a non-scalar M-RoPE region")

    endpoint = np.ascontiguousarray(tail["tail_hidden"][:, -1, -1], dtype=np.float32)
    if endpoint.shape != (1, HIDDEN):
        raise RuntimeError("tail endpoint contract mismatch")
    logits: np.ndarray | None = None
    head_seconds = 0.0
    top10: list[dict[str, object]] = []
    first_token: int | None = None
    if remainder_mode:
        seed_input_token = int(remainder_tokens[0])
    else:
        normalized = endpoint * np.float32(
            1.0 / np.sqrt(np.mean(endpoint**2, axis=-1, keepdims=True) + 1e-6)
        )
        normalized *= norm_weight.reshape(1, -1)
        head_started = time.perf_counter()
        logits = np.asarray(embedding @ normalized[0], dtype=np.float32)
        head_seconds = time.perf_counter() - head_started
        top_indices = np.argpartition(logits, -10)[-10:]
        top_indices = top_indices[np.argsort(logits[top_indices])[::-1]]
        top10 = [
            {"token_id": int(token), "logit": float(logits[token])}
            for token in top_indices
        ]
        first_token = int(top_indices[0])
        seed_input_token = first_token

    recurrent_states: dict[int, np.ndarray] = {}
    conv_states: dict[int, np.ndarray] = {}
    attention_kv: dict[tuple[int, str], np.ndarray] = {}
    for layer in range(20):
        if layer in ATTENTION_LAYERS:
            for kind in ("k", "v"):
                value = np.asarray(npu[f"layer_{layer:02d}_present_{kind}"], np.float32)
                attention_kv[(layer, kind)] = np.ascontiguousarray(
                    value[..., -2047:, :], dtype=np.float32
                )
        else:
            conv_states[layer] = np.asarray(
                npu[f"layer_{layer:02d}_conv_state_out"], np.float32
            )
            recurrent_states[layer] = np.asarray(
                npu[f"layer_{layer:02d}_recurrent_state_out"], np.float32
            )
    for layer in (20, 21, 22):
        conv_states[layer] = np.asarray(tail[f"layer_{layer:02d}_conv_state"], np.float32)
        recurrent_states[layer] = np.asarray(
            tail[f"layer_{layer:02d}_recurrent_state"], np.float32
        )
    for kind in ("k", "v"):
        value = np.asarray(tail[f"layer_23_present_{kind}"], np.float32)
        attention_kv[(23, kind)] = np.ascontiguousarray(
            value[..., -2047:, :], dtype=np.float32
        )

    contract_rows = read_tsv(args.tensor_contract)
    input_rows = [row for row in contract_rows if row["kind"] == "input"]
    advance_rows = read_tsv(args.advance_plan)
    advance_by_destination = {row["destination"]: row for row in advance_rows}
    if (
        len(input_rows) != 578
        or [int(row["position"]) for row in input_rows] != list(range(578))
        or len(advance_by_destination) != len(advance_rows)
    ):
        raise RuntimeError("P5 input/advance contract mismatch")

    if vlm_bundle is not None and remainder_mode:
        seed_rope_cos = vlm_bundle["rope_cos"][position]
        seed_rope_sin = vlm_bundle["rope_sin"][position]
    else:
        seed_rope_cos = rope_raw[0, seed_rope_position]
        seed_rope_sin = rope_raw[1, seed_rope_position]
    seed: dict[str, np.ndarray] = {
        "hidden_in": np.ascontiguousarray(
            (vlm_bundle["hidden"][position] if vector_suffix else embedding[seed_input_token])[None,None],np.float32),
        "rope_cos": np.ascontiguousarray(seed_rope_cos[None, None, None], np.float32),
        "rope_sin": np.ascontiguousarray(seed_rope_sin[None, None, None], np.float32),
    }
    mask = np.full((1, 1, 1, CONTEXT), -64.0, dtype=np.float32)
    mask[..., CONTEXT - position - 1 :] = 0.0
    seed["tail_mask"] = mask

    audits: dict[str, dict[str, object]] = {}
    for row in input_rows:
        name = row["name"]
        if name in seed:
            continue
        conv_match = re.fullmatch(r"layer_(\d+)_conv_state_in", name)
        attention_match = re.fullmatch(r"layer_(\d+)_past_([kv])", name)
        value: np.ndarray
        if conv_match:
            layer = int(conv_match.group(1))
            value = np.ascontiguousarray(conv_states[layer], dtype=np.float32)
        elif attention_match:
            layer = int(attention_match.group(1))
            kind = attention_match.group(2)
            plan = advance_by_destination[name]
            value, audit = quantize_u16(
                attention_kv[(layer, kind)],
                float(plan["input_scale"]),
                int(plan["input_zero_point"]),
            )
            audits[name] = audit
        elif row["dtype"] == "uint16" and "state_in_QuantizeLinear_Output" in name:
            layer_match = re.search(r"layer_(\d+)", name)
            if layer_match is None:
                raise RuntimeError(name)
            layer = int(layer_match.group(1))
            plan = advance_by_destination[name]
            value, audit = quantize_u16(
                recurrent_slice(name, recurrent_states[layer]),
                float(plan["input_scale"]),
                int(plan["input_zero_point"]),
            )
            audits[name] = audit
        else:
            raise RuntimeError(f"unhandled P5 input: {name}")
        expected_shape = parse_shape(row["shape"])
        expected_dtype = np.float32 if row["dtype"] == "float32" else np.uint16
        value = np.ascontiguousarray(value, dtype=expected_dtype)
        if value.shape != expected_shape:
            raise RuntimeError(f"P5 value shape mismatch for {name}")
        seed[name] = value

    names = [row["name"] for row in input_rows]
    if set(seed) != set(names):
        raise RuntimeError("P5 seed name set mismatch")
    all_float_finite = all(
        np.isfinite(value).all() for value in seed.values() if value.dtype.kind == "f"
    )
    clipped_count = sum(int(value["clipped_count"]) for value in audits.values())
    quantized_count = sum(int(value["total_count"]) for value in audits.values())
    clip_fraction = float(clipped_count / max(quantized_count, 1))
    max_local_clip = max((float(value["clip_fraction"]) for value in audits.values()), default=0.0)
    offenders = {
        name: value
        for name, value in sorted(
            audits.items(),
            key=lambda item: (-float(item[1]["clip_fraction"]), item[0]),
        )
        if int(value["clipped_count"]) > 0
    }

    seed_npz = args.output_dir / "p5_seed_inputs.npz"
    runtime_savez(seed_npz, **seed)
    seed_binary = args.output_dir / "p5_seed_inputs.bin"
    with seed_binary.open("xb") as stream:
        for row in input_rows:
            dtype = np.dtype("<f4") if row["dtype"] == "float32" else np.dtype("<u2")
            value = np.ascontiguousarray(seed[row["name"]], dtype=dtype)
            if value.nbytes != int(row["bytes"]):
                raise RuntimeError(f"binary byte contract mismatch for {row['name']}")
            stream.write(memoryview(value).cast("B"))
    suffix_files = {}
    if vector_suffix:
        for key in ("hidden","rope_cos","rope_sin"):
            path=args.output_dir/("suffix_"+key+".bin")
            value=np.ascontiguousarray(vlm_bundle[key][position:full_tokens.size],dtype="<f4")
            with path.open("xb") as stream: stream.write(memoryview(value).cast("B"))
            suffix_files[key]=artifact(path)
    history = (
        np.ascontiguousarray(full_tokens, dtype=np.int64)
        if remainder_mode
        else np.concatenate([tokens, [first_token]]).astype(np.int64)
    )
    expected_bytes = sum(int(row["bytes"]) for row in input_rows)
    binary_hash = sha256(seed_binary)
    gates = {
        "aligned_dynamic_history_within_context": tokens.size % 16 == 0
        and history.size <= CONTEXT,
        "vlm_generation_mrope_within_context": 0
        <= full_tokens.size + rope_delta
        < CONTEXT,
        "vlm_remainder_positions_supported": bool(vector_suffix or remainder_rotary_scalarizable),
        "remainder_handoff_contract_if_requested": not remainder_mode
        or (
            1 <= remainder_tokens.size <= args.max_remainder_tokens
            and history.size == tokens.size + remainder_tokens.size
            and np.array_equal(history[-remainder_tokens.size :], remainder_tokens)
        ),
        "endpoint_and_head_all_finite": bool(np.isfinite(endpoint).all())
        and (logits is None or bool(np.isfinite(logits).all())),
        "expected_first_token_if_provided": args.expected_first_token is None
        or first_token == args.expected_first_token,
        "exact_578_input_contract": len(seed) == 578 and set(seed) == set(names),
        "all_seed_float_arrays_finite": bool(all_float_finite),
        "global_quantized_clip_fraction_le_0_001": clip_fraction <= 0.001,
        "binary_bytes_exact": seed_binary.stat().st_size == expected_bytes == 35_930_624,
        "expected_seed_hash_if_provided": args.expected_seed_sha256 is None
        or binary_hash == args.expected_seed_sha256,
    }
    gates["all_pass"] = all(gates.values())
    report = {
        "schema": "qwen35_q6a_dynamic_chunk16_to_p5_seed_v1",
        "status": "pass" if gates["all_pass"] else "fail",
        "scope": (
            "dynamic aligned prefix state -> P5 NPU remainder handoff seed"
            if remainder_mode
            else "dynamic aligned prompt state -> one-time tied head -> exact 578-input P5 seed"
        ),
        "mode": "p5_remainder_handoff" if remainder_mode else "aligned_endpoint_head",
        "prompt_tokens": int(full_tokens.size),
        "aligned_prefix_tokens": int(tokens.size),
        "seed_position": position,
        "seed_cache_position": position,
        "seed_rope_position": seed_rope_position,
        "rope_delta": rope_delta,
        "vector_vision_suffix": vector_suffix,
        "suffix_files": suffix_files,
        "generation_rope_start": int(full_tokens.size + rope_delta),
        "seed_input_token": seed_input_token,
        "first_token": first_token,
        "history_token_ids": history.tolist(),
        "remainder_token_ids": remainder_tokens.tolist(),
        "top10": top10,
        "timing_seconds": {
            "tied_head": head_seconds,
            "total_build": time.perf_counter() - started,
        },
        "quantization": {
            "clipped_count": clipped_count,
            "total_count": quantized_count,
            "global_clip_fraction": clip_fraction,
            "max_local_clip_fraction": max_local_clip,
            "offenders": offenders,
        },
        "contract": {
            "input_count": len(seed),
            "float32_inputs": sum(value.dtype == np.float32 for value in seed.values()),
            "uint16_inputs": sum(value.dtype == np.uint16 for value in seed.values()),
            "binary_bytes": expected_bytes,
        },
        "gates": gates,
        "sources": {
            "npu_final_state": artifact(args.npu_final_state),
            "tail_output": artifact(args.tail_output),
            "embedding": artifact(args.embedding),
            "norm_weight": artifact(args.norm_weight),
            "rope": artifact(args.rope),
            "tensor_contract": artifact(args.tensor_contract),
            "advance_plan": artifact(args.advance_plan),
            **(
                {"full_token_ids": artifact(args.full_token_ids_npy)}
                if args.full_token_ids_npy is not None
                else (
                    {"vlm_prompt_bundle": artifact(args.vlm_prompt_bundle)}
                    if args.vlm_prompt_bundle is not None
                    else {}
                )
            ),
        },
        "artifacts": {
            "seed_npz": artifact(seed_npz),
            "seed_binary": artifact(seed_binary),
        },
        "next_action": (
            "consume the 1-15 token suffix in P5 NPU, then generate"
            if remainder_mode
            else "load this seed into the shared-memory QNN decoder and generate"
        ),
    }
    report_path = args.output_dir / "result.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "status": report["status"],
                "prompt_tokens": report["prompt_tokens"],
                "first_token": first_token,
                "top10": top10,
                "timing_seconds": report["timing_seconds"],
                "quantization": {
                    key: report["quantization"][key]
                    for key in (
                        "clipped_count",
                        "total_count",
                        "global_clip_fraction",
                        "max_local_clip_fraction",
                    )
                },
                "contract": report["contract"],
                "gates": gates,
                "artifacts": report["artifacts"],
            },
            indent=2,
        ),
        flush=True,
    )
    return 0 if gates["all_pass"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
