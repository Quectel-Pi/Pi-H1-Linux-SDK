"""Small request metadata for a native, resident full24 Prefill session.

The recurrent/KV state stays in the native process. Only bounded visual suffix
vectors are written; this module performs no learned model operations.
"""
import hashlib
import json
import time
from pathlib import Path
import numpy as np
from prompt_bundle import load_vlm_prompt_bundle


def prefix_length(sequence, repair):
    if not 17 <= sequence < 2048 or repair not in (1, 16, 32, 48):
        raise ValueError('native Prefill sequence/repair policy is invalid')
    prefix = min(sequence // 16 * 16, max(16, (sequence - repair) // 16 * 16))
    if not 1 <= sequence - prefix <= repair + 15:
        raise ValueError('native Prefill suffix contract failed')
    return prefix


def artifact(path):
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return {'path': str(path.resolve()), 'bytes': path.stat().st_size, 'sha256': digest}


def build_handoff(bundle_path, token_path, prefix, request_id, directory, image):
    started = time.perf_counter()
    bundle = load_vlm_prompt_bundle(bundle_path)
    ids = np.asarray(np.load(token_path, allow_pickle=False), dtype=np.int64)
    if not np.array_equal(ids, bundle['input_ids']) or not 16 <= prefix < ids.size:
        raise ValueError('native Prefill bundle/token history mismatch')
    if prefix % 16 or ids.size - prefix > 63:
        raise ValueError('native Prefill suffix exceeds its fixed bound')
    if not image and int(bundle['rope_delta']) != 0:
        raise ValueError('plain text must use the original 1D rotary positions')
    directory.mkdir(exist_ok=False)
    suffix_files = {}
    if image:
        for key in ('hidden', 'rope_cos', 'rope_sin'):
            path = directory / (key + '_suffix.bin')
            value = np.ascontiguousarray(bundle[key][prefix:], dtype='<f4')
            if not np.isfinite(value).all():
                raise ValueError('visual suffix is not finite')
            with path.open('xb') as stream:
                stream.write(memoryview(value).cast('B'))
            suffix_files[key] = artifact(path)
    result = {
        'schema': 'shared_native_resident_handoff_v1', 'status': 'pass',
        'mode': 'p5_remainder_handoff', 'resident_seed_tag': 'shared-resident:' + request_id,
        'seed_position': prefix, 'history_token_ids': ids.tolist(),
        'remainder_token_ids': ids[prefix:].tolist(), 'rope_delta': int(bundle['rope_delta']),
        'generation_rope_start': int(ids.size + bundle['rope_delta']),
        'vector_vision_suffix': bool(image), 'suffix_files': suffix_files,
        'sources': {'vlm_prompt_bundle' if image else 'text_prompt_bundle': artifact(bundle_path)},
        'timing_seconds': {'tied_head': 0.0, 'metadata': time.perf_counter() - started},
        'quantization': {'global_clip_fraction': None, 'max_local_clip_fraction': None,
                         'measured_in': 'decoder.done after native state encoding'},
        'state_storage': 'native_resident_arena', 'serialized_state_bytes': 0,
        'gates': {'all_pass': True},
    }
    with (directory / 'result.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
    return result
