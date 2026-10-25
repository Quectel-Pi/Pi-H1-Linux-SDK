#!/usr/bin/env python3
"""Build one text or fixed-1024x576 still-image prompt for the runtime Chunk16 runtime."""

from __future__ import annotations

import argparse
import hashlib
import json
import time
from pathlib import Path
from typing import Any

import numpy as np
from tokenizers import Tokenizer

from image_frontend import (
    CONTEXT,
    HIDDEN,
    IMAGE_WIDTH,
    IMAGE_HEIGHT,
    IMAGE_TOKEN_ID,
    IMAGE_TOKENS,
    PATCHES,
    PATCH_WIDTH,
    ROTARY,
    VOCAB,
    build_bundle,
    build_mrope,
    build_position_ids,
)
from vision_runtime import (
    load_qnn_vision_sessions,
    run_qnn_vision_chain,
)


_TOKENIZER_CACHE: dict[str, Tokenizer] = {}


def load_tokenizer(path: Path) -> tuple[Tokenizer, bool]:
    key = str(path.resolve())
    cached = _TOKENIZER_CACHE.get(key)
    if cached is not None:
        return cached, True
    tokenizer = Tokenizer.from_file(key)
    _TOKENIZER_CACHE[key] = tokenizer
    return tokenizer, False


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def artifact(path: Path) -> dict[str, Any]:
    return {
        "path": str(path.resolve()),
        "bytes": path.stat().st_size,
        "sha256": sha256(path),
    }


def sanitize_text(value: str) -> str:
    # Prevent user-authored text from being interpreted as Qwen control tokens.
    return value.replace("<|", "<\u200b|")


def normalize_content(content: Any) -> tuple[str, int]:
    if isinstance(content, str):
        return sanitize_text(content), 0
    if not isinstance(content, list):
        raise ValueError("message content must be a string or an array")
    output: list[str] = []
    image_count = 0
    for part in content:
        if not isinstance(part, dict):
            raise ValueError("message content parts must be objects")
        kind = part.get("type")
        if kind == "text" and isinstance(part.get("text"), str):
            output.append(sanitize_text(part["text"]))
        elif kind == "image":
            image_count += 1
            output.append(
                "<|vision_start|>"
                + "<|image_pad|>" * IMAGE_TOKENS
                + "<|vision_end|>"
            )
        else:
            raise ValueError("only normalized text and image content parts are supported")
    return "".join(output), image_count


def render_messages(messages: list[dict[str, Any]]) -> tuple[str, int]:
    if not messages:
        raise ValueError("messages must not be empty")
    rendered: list[str] = []
    image_count = 0
    for index, message in enumerate(messages):
        if not isinstance(message, dict):
            raise ValueError("each message must be an object")
        role = message.get("role")
        if role not in {"system", "user", "assistant"}:
            raise ValueError("roles must be system, user, or assistant")
        if index == len(messages) - 1 and role != "user":
            raise ValueError("the final message must have role user")
        content, images = normalize_content(message.get("content", ""))
        image_count += images
        if role == "assistant" and "</think>" in content:
            content = content.rsplit("</think>", 1)[-1].lstrip()
        rendered.append(f"<|im_start|>{role}\n{content}<|im_end|>\n")
    rendered.append("<|im_start|>assistant\n<think>\n\n</think>\n\n")
    return "".join(rendered), image_count


def preprocess_image(path: Path) -> tuple[np.ndarray, dict[str, Any]]:
    # Pillow is an image-only dependency.  Keeping it lazy lets the Text API
    # remain usable even on a minimal runtime before the VLM extra is installed.
    from PIL import Image, ImageOps

    with Image.open(path) as source:
        oriented = ImageOps.exif_transpose(source).convert("RGB")
        original_size = list(source.size)
        oriented_size = list(oriented.size)
        resized = oriented.size != (IMAGE_WIDTH, IMAGE_HEIGHT)
        if resized:
            oriented = ImageOps.pad(
                oriented,
                (IMAGE_WIDTH, IMAGE_HEIGHT),
                method=Image.Resampling.LANCZOS,
                color=(0, 0, 0),
                centering=(0.5, 0.5),
            )
        uint8_hwc = np.asarray(oriented, dtype=np.uint8)
    normalized_hwc = (
        uint8_hwc.astype(np.float32) - np.float32(127.5)
    ) / np.float32(127.5)
    chw = np.transpose(normalized_hwc, (2, 0, 1))
    patches = chw.reshape(1, 3, 18, 2, 16, 32, 2, 16)
    patches = patches.transpose(0, 2, 5, 3, 6, 1, 4, 7)
    patches = np.expand_dims(patches, axis=6)
    patches = np.broadcast_to(patches, (1, 18, 32, 2, 2, 3, 2, 16, 16))
    pixels = np.ascontiguousarray(patches.reshape(PATCHES, PATCH_WIDTH), dtype="<f4")
    if pixels.shape != (PATCHES, PATCH_WIDTH) or not np.isfinite(pixels).all():
        raise RuntimeError("image preprocessing violated the fixed vision contract")
    return pixels, {
        "original_size_wh": original_size,
        "oriented_size_wh": oriented_size,
        "decoder_size_wh": [IMAGE_WIDTH, IMAGE_HEIGHT],
        "letterbox_resize_applied": resized,
        "resize_policy": "aspect_preserving_contain_with_black_padding",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--messages-json", required=True, type=Path)
    parser.add_argument("--tokenizer-json", required=True, type=Path)
    parser.add_argument("--embedding-npy", required=True, type=Path)
    parser.add_argument("--rope-bin", required=True, type=Path)
    parser.add_argument("--output-token-ids", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--image", type=Path)
    parser.add_argument("--vision-patch-model", type=Path)
    parser.add_argument("--vision-body-model", type=Path)
    parser.add_argument("--vision-merger-model", type=Path)
    parser.add_argument("--output-bundle", type=Path)
    parser.add_argument("--threads", type=int, default=4)
    args = parser.parse_args()

    required = [args.messages_json, args.tokenizer_json, args.embedding_npy, args.rope_bin]
    vision_models = (
        args.vision_patch_model,
        args.vision_body_model,
        args.vision_merger_model,
    )
    if args.image is not None:
        if not all(path is not None for path in vision_models):
            raise ValueError(
                "image mode requires patch, body, and merger Vision models"
            )
        required.extend([args.image, *vision_models])
        if args.output_bundle is None:
            raise ValueError("image mode requires --output-bundle")
    elif any(path is not None for path in vision_models):
        raise ValueError("vision arguments require --image")
    for path in required:
        if path is None or not path.is_file():
            raise FileNotFoundError(path)
    if args.threads < 1:
        raise ValueError("threads must be positive")
    for path in (args.output_token_ids, args.report, args.output_bundle):
        if path is not None:
            path.parent.mkdir(parents=True, exist_ok=True)

    started = time.perf_counter()
    messages = json.loads(args.messages_json.read_text(encoding="utf-8"))
    if not isinstance(messages, list):
        raise ValueError("messages JSON must contain an array")
    rendered, image_marker_count = render_messages(messages)
    expected_images = 1 if args.image is not None else 0
    if image_marker_count != expected_images:
        raise ValueError(
            f"message/image mismatch: markers={image_marker_count} files={expected_images}"
        )

    tokenizer_started = time.perf_counter()
    tokenizer, tokenizer_cache_hit = load_tokenizer(args.tokenizer_json)
    input_ids = np.ascontiguousarray(
        tokenizer.encode(rendered, add_special_tokens=False).ids, dtype="<i8"
    )
    if (
        input_ids.ndim != 1
        or input_ids.size < 16
        or input_ids.size >= CONTEXT
        or np.any(input_ids < 0)
        or np.any(input_ids >= VOCAB)
    ):
        raise RuntimeError(
            "tokenized prompt is outside the ctx2048 Chunk16 contract: "
            f"count={input_ids.size}, expected=[16,{CONTEXT - 1}]"
        )
    np.save(args.output_token_ids, input_ids, allow_pickle=False)
    tokenize_seconds = time.perf_counter() - tokenizer_started

    image_info = None
    vision_session_seconds = 0.0
    vision_run_seconds = 0.0
    vision_component_run_seconds: dict[str, float] = {}
    vision_session_cache_hit: bool | None = None
    vision_startup: dict[str, Any] | None = None
    bundle_info = None
    if args.image is not None:
        pixels, image_info = preprocess_image(args.image)
        session_started = time.perf_counter()
        sessions, vision_session_cache_hit, vision_startup = (
            load_qnn_vision_sessions(
                args.vision_patch_model,
                args.vision_body_model,
                args.vision_merger_model,
                args.report.parent / "vision_qnn_startup",
            )
        )
        vision_session_seconds = time.perf_counter() - session_started
        run_started = time.perf_counter()
        image_embeddings, vision_component_run_seconds = run_qnn_vision_chain(
            sessions, pixels
        )
        vision_run_seconds = time.perf_counter() - run_started
        if image_embeddings.shape != (IMAGE_TOKENS, HIDDEN):
            raise RuntimeError("vision output shape mismatch")

        image_rows = np.flatnonzero(input_ids == IMAGE_TOKEN_ID)
        if image_rows.shape != (IMAGE_TOKENS,) or not np.array_equal(
            image_rows, np.arange(image_rows[0], image_rows[0] + IMAGE_TOKENS)
        ):
            raise RuntimeError("image placeholder expansion is not exactly 576 contiguous tokens")
        modalities = np.zeros(input_ids.shape, dtype="<i8")
        modalities[image_rows] = 1
        position_ids, rope_delta = build_position_ids(modalities)
        cosine, sine = build_mrope(position_ids, args.rope_bin)
        embedding = np.load(args.embedding_npy, mmap_mode="r", allow_pickle=False)
        if embedding.shape != (VOCAB, HIDDEN) or embedding.dtype != np.float32:
            raise RuntimeError("embedding table contract mismatch")
        hidden = np.ascontiguousarray(embedding[input_ids], dtype="<f4")
        hidden[image_rows] = image_embeddings
        bundle_info = build_bundle(
            args.output_bundle,
            input_ids,
            hidden,
            cosine,
            sine,
            rope_delta,
        )


    elif args.output_bundle is not None:
        # Text shares the same validated raw bundle ABI, with original 1D RoPE.
        embedding = np.load(args.embedding_npy, mmap_mode="r", allow_pickle=False)
        if embedding.shape != (VOCAB, HIDDEN) or embedding.dtype != np.float32:
            raise RuntimeError("embedding table contract mismatch")
        rope = np.memmap(args.rope_bin, mode="r", dtype="<f4").reshape(2, CONTEXT, ROTARY)
        bundle_info = build_bundle(args.output_bundle, input_ids,
            np.ascontiguousarray(embedding[input_ids], dtype="<f4"),
            np.ascontiguousarray(rope[0, :input_ids.size]),
            np.ascontiguousarray(rope[1, :input_ids.size]), 0)

    report = {
        "schema": "qwen35_q6a_openai_request_frontend_v2",
        "status": "pass",
        "mode": "image" if args.image is not None else "text",
        "prompt_tokens": int(input_ids.size),
        "tokenizer_cache_hit": tokenizer_cache_hit,
        "aligned_prefix_tokens": int(input_ids.size // 16 * 16),
        "remainder_tokens": int(input_ids.size % 16),
        "image_count": image_marker_count,
        "image": image_info,
        "bundle": bundle_info,
        "vision": (
            {
                "backend": "qnn_htp_fixed1024x576_three_stage",
                "fallback_disabled": True,
                "session_cache_hit": vision_session_cache_hit,
                "startup_provider_counts": vision_startup["provider_counts"],
                "startup_cpu_assigned_nodes": vision_startup[
                    "cpu_assigned_nodes"
                ],
                "startup_qnn_assigned_nodes": vision_startup[
                    "qnn_assigned_nodes"
                ],
                "startup_gates": vision_startup["gates"],
            }
            if vision_startup is not None
            else None
        ),
        "timing_seconds": {
            "tokenize": tokenize_seconds,
            "vision_session_create": vision_session_seconds,
            "vision_run": vision_run_seconds,
            "vision_components": vision_component_run_seconds,
            "total": time.perf_counter() - started,
        },
        "artifacts": {
            "messages": artifact(args.messages_json),
            "token_ids": artifact(args.output_token_ids),
            "bundle": artifact(args.output_bundle) if args.output_bundle is not None else None,
        },
        "gates": {
            "one_image_or_text_only": image_marker_count in (0, 1),
            "prompt_within_ctx2048": 16 <= input_ids.size < CONTEXT,
            "aligned_prefix_nonempty": input_ids.size // 16 >= 1,
            "fixed1024x576_bundle_present_if_image": args.image is None
            or (bundle_info is not None and args.output_bundle.is_file()),
            "vision_qnn_startup_passed_if_image": args.image is None
            or (
                vision_startup is not None
                and vision_startup["gates"]["all_pass"] is True
            ),
            "all_pass": True,
        },
    }
    args.report.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps({
        "status": report["status"],
        "mode": report["mode"],
        "prompt_tokens": report["prompt_tokens"],
        "timing_seconds": report["timing_seconds"],
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
