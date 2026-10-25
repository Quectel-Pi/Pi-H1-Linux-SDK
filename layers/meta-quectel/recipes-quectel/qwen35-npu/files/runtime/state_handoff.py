"""One-use arrays between serialized prefill and seed stages in one worker.

The on-disk JSON is explicitly a manifest, not a serialized tensor payload.
Diagnostic standalone invocations still use real NPZ files.
"""
import json,os
from pathlib import Path
import numpy as np

_ARRAYS={}
def enabled():return os.environ.get("QWEN35_STATE_HANDOFF")=="1"
def clear_arrays():_ARRAYS.clear()
def pending_count():return len(_ARRAYS)

def save_arrays(path,**arrays):
    if not enabled():raise RuntimeError("worker memory handoff is not enabled")
    p=Path(path).resolve()
    if p.suffix!=".json" or str(p) in _ARRAYS:raise RuntimeError("invalid or duplicate handoff manifest")
    values={k:np.asarray(v) for k,v in arrays.items()}
    if any(v.dtype.hasobject for v in values.values()):raise TypeError("object tensors are forbidden")
    manifest={"format":"same_worker_arrays_v1","persistent":False,
              "arrays":{k:{"shape":list(v.shape),"dtype":str(v.dtype),"bytes":v.nbytes} for k,v in values.items()}}
    with p.open("x",encoding="utf-8",newline="\n") as f:json.dump(manifest,f,sort_keys=True);f.write("\n")
    _ARRAYS[str(p)]=(manifest,values)

def load_arrays(path):
    p=Path(path).resolve()
    if p.suffix==".npz":return np.load(p,allow_pickle=False)
    if p.suffix!=".json" or not enabled():raise RuntimeError("invalid state handoff")
    entry=_ARRAYS.pop(str(p),None)
    if entry is None:raise RuntimeError("missing or already-consumed worker state")
    manifest,values=entry
    if json.loads(p.read_text(encoding="utf-8"))!=manifest:raise RuntimeError("state manifest changed")
    return values
