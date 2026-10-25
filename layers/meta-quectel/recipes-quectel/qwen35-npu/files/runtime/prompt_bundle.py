#!/usr/bin/env python3
"""Read and validate the mmap-friendly runtime/R21 VLM prompt bundle."""

from __future__ import annotations

import struct
from pathlib import Path
from typing import Any

import numpy as np


MAGIC = b"R20VLM1\0"
VERSION = 1
HIDDEN = 1024
ROTARY = 64
VOCAB = 248320
CONTEXT = 2048
HEADER = struct.Struct("<8sIIIiIIIIQQQQQ")


def align(value: int, alignment: int = 64) -> int:
    return (value + alignment - 1) // alignment * alignment


def load_vlm_prompt_bundle(path: Path, *, copy: bool = False) -> dict[str, Any]:
    if not path.is_file():
        raise FileNotFoundError(path)
    with path.open("rb") as stream:
        header_bytes = stream.read(HEADER.size)
    if len(header_bytes) != HEADER.size:
        raise RuntimeError("VLM prompt bundle header is truncated")
    (
        magic,
        version,
        declared_header_bytes,
        sequence,
        rope_delta,
        hidden,
        rotary,
        vocab,
        reserved,
        ids_offset,
        hidden_offset,
        cosine_offset,
        sine_offset,
        total_bytes,
    ) = HEADER.unpack(header_bytes)
    expected_ids_offset = align(HEADER.size)
    expected_hidden_offset = align(expected_ids_offset + sequence * np.dtype("<i8").itemsize)
    expected_cosine_offset = align(
        expected_hidden_offset + sequence * HIDDEN * np.dtype("<f4").itemsize
    )
    expected_sine_offset = align(
        expected_cosine_offset + sequence * ROTARY * np.dtype("<f4").itemsize
    )
    expected_total_bytes = expected_sine_offset + sequence * ROTARY * np.dtype("<f4").itemsize
    if (
        magic != MAGIC
        or version != VERSION
        or declared_header_bytes != HEADER.size
        or not 1 <= sequence < CONTEXT
        or hidden != HIDDEN
        or rotary != ROTARY
        or vocab != VOCAB
        or reserved != 0
        or ids_offset != expected_ids_offset
        or hidden_offset != expected_hidden_offset
        or cosine_offset != expected_cosine_offset
        or sine_offset != expected_sine_offset
        or total_bytes != expected_total_bytes
        or path.stat().st_size != total_bytes
        or not 0 <= sequence + rope_delta < CONTEXT
    ):
        raise RuntimeError("VLM prompt bundle header/offset contract mismatch")

    arrays: dict[str, np.ndarray] = {
        "input_ids": np.memmap(path, mode="r", dtype="<i8", offset=ids_offset, shape=(sequence,)),
        "hidden": np.memmap(
            path, mode="r", dtype="<f4", offset=hidden_offset, shape=(sequence, HIDDEN)
        ),
        "rope_cos": np.memmap(
            path, mode="r", dtype="<f4", offset=cosine_offset, shape=(sequence, ROTARY)
        ),
        "rope_sin": np.memmap(
            path, mode="r", dtype="<f4", offset=sine_offset, shape=(sequence, ROTARY)
        ),
    }
    if (
        np.any(arrays["input_ids"] < 0)
        or np.any(arrays["input_ids"] >= VOCAB)
        or not np.isfinite(arrays["hidden"]).all()
        or not np.isfinite(arrays["rope_cos"]).all()
        or not np.isfinite(arrays["rope_sin"]).all()
    ):
        raise RuntimeError("VLM prompt bundle contains invalid IDs or non-finite arrays")
    if copy:
        arrays = {
            name: np.ascontiguousarray(value, dtype=value.dtype)
            for name, value in arrays.items()
        }
    return {
        "sequence": int(sequence),
        "rope_delta": int(rope_delta),
        "generation_rope_start": int(sequence + rope_delta),
        "offsets": {
            "input_ids": int(ids_offset),
            "hidden": int(hidden_offset),
            "rope_cos": int(cosine_offset),
            "rope_sin": int(sine_offset),
        },
        "total_bytes": int(total_bytes),
        **arrays,
    }
