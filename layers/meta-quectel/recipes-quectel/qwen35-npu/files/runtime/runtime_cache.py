#!/usr/bin/env python3
"""Production-only cache helpers for immutable runtime artifacts.

The default behavior remains evidence-grade: files are hashed and NPZ outputs are
compressed on every standalone script invocation.  The OpenAI service may point
QWEN35_RUNTIME_HASH_CACHE at a cache built by build_runtime_hash_cache.py and
enable QWEN35_RUNTIME_FAST_NPZ for ephemeral request scratch.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
from pathlib import Path
from typing import Any

import numpy as np


HASH_CACHE_ENV = "QWEN35_RUNTIME_HASH_CACHE"
FAST_NPZ_ENV = "QWEN35_RUNTIME_FAST_NPZ"
_SHA256_RE = re.compile(r"[0-9a-f]{64}")
_CACHE: dict[str, dict[str, Any]] | None = None


def _load_cache() -> dict[str, dict[str, Any]]:
    global _CACHE
    if _CACHE is not None:
        return _CACHE
    _CACHE = {}
    raw = os.environ.get(HASH_CACHE_ENV, "")
    if not raw:
        return _CACHE
    path = Path(raw)
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
        if document.get("schema") != "qwen35_q6a_runtime_hash_cache_v1":
            return _CACHE
        entries = document.get("entries")
        if not isinstance(entries, list):
            return _CACHE
        for value in entries:
            if not isinstance(value, dict) or not isinstance(value.get("path"), str):
                continue
            _CACHE[value["path"]] = value
    except (OSError, ValueError, TypeError):
        # A missing or malformed optimization cache must never weaken correctness.
        # Falling back to a full hash preserves the standalone evidence contract.
        _CACHE = {}
    return _CACHE


_SESSION_HASHES = {}


def sha256(path: Path) -> str:
    resolved = path.resolve()
    stat = resolved.stat()
    memo_key = (str(resolved), stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns, stat.st_ino)
    memo_enabled = os.environ.get("QWEN35_MEMOIZE_HASHES") == "1"
    if memo_enabled and memo_key in _SESSION_HASHES:
        return _SESSION_HASHES[memo_key]
    cached = _load_cache().get(str(resolved))
    if (
        cached is not None
        and cached.get("bytes") == stat.st_size
        and cached.get("mtime_ns") == stat.st_mtime_ns
        and isinstance(cached.get("sha256"), str)
        and _SHA256_RE.fullmatch(cached["sha256"])
    ):
        return cached["sha256"]
    digest = hashlib.sha256()
    with resolved.open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    value = digest.hexdigest()
    if memo_enabled:
        _SESSION_HASHES[memo_key] = value
    return value


def savez(path: Path, **arrays: np.ndarray) -> None:
    if os.environ.get(FAST_NPZ_ENV) == "1":
        np.savez(path, **arrays)
    else:
        np.savez_compressed(path, **arrays)

