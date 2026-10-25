Cleanup list for `qwen35-npu/files/` — CPU plan residue
======================================================

Status: **executed.** Every item marked DELETE below is gone from `files/` and
from the recipe's `SRC_URI` (18 files in the rewrite commit, 948 insertions /
5475 deletions). The vision question in §5 was settled on the board: the Vision
contexts ship and image input works (see §5).

Companion document: `PORT_NOTES.md` (teammate `runtime`) describes the delivered
new tree (`runtime/`, `native/`, `configs/`, `qwen35-mkconfig.py`, the rewritten
`qwen35-launch.sh` and `qwen35.service`). This file is the audit of what is
**left over** and must go; where the two overlap, `PORT_NOTES.md` is
authoritative for what the new runtime needs, this file for what the old CPU
plan left behind.

> Reconciliation note: `PORT_NOTES.md` (around line 98) still quotes the **old**
> inherited value for `ADSP_LIBRARY_PATH`
> (`<qnn>;/usr/lib/runtime/qnn;/usr/lib/rfsa/adsp;/dsp;/usr/lib/dsp/cdsp;/usr/lib/dsp/adsp`).
> The four rules in §3a below are the board-verified ones and the current
> `qwen35-launch.sh` already implements them, so that paragraph is stale
> documentation, not a live defect. **Confirmed on the board:** the running
> server's environment is `ADSP_LIBRARY_PATH=/usr/lib/dsp/cdsp;/usr/lib/dsp/adsp;/dsp`
> and `qwen35-verify.sh` check 4 passes on it.

Context: the CPU/`qnn-net-run` plan is abandoned. The new design is the
upstream Radxa ONNX Runtime QNN EP runtime with pre-compiled EPContext graphs
(`decode_ctx.onnx` + `decode_ctx_qnn.bin`), 2K only, text + one image, assets in
`/usr/share/qwen35`, code in `/usr/lib/qwen35`. Anything that belongs to the old
int8-ONNX-on-CPU design, to the hand-rolled `qnn-net-run` harness, or to the
board-side installer has no consumer in the new package.

Inventory of `files/` at the time of writing (old CPU-plan leftovers; the new
`qwen35-mkconfig.py`, `runtime/`, `native/`, `configs/` and the docs in this
commit arrived in parallel and are not part of the cleanup):

| File | Bytes | Verdict |
|---|---:|---|
| `LICENSE` | 11,343 | **keep** — new, Apache-2.0 upstream text, md5 `a2f7eec73f8b7e1873feeb201028cf9c` |
| `NOTICE` | 5,747 | **keep** — new, provenance + modification statement |
| `README.md` | 12,084 | **keep** — rewritten for the NPU design (the old 23 KB CPU doc is gone) |
| `CLEANUP.md` | — | this file |
| `qwen35-launch.sh` | 7,982 | **keep** — already rewritten by teammate `runtime`; §3a rules satisfied (verify `$QNN_DIR`/`$SKEL_DIR` pairing) |
| `qwen35.service` | 2,738 | **keep** — already rewritten; §3a rules satisfied; see §3 for two leftovers |
| `qwen35-convert.sh` | 4,150 | **delete** |
| `qwen35-model-install.sh` | 4,745 | **delete** |
| `qwen35-ready.py` | 706 | **delete** (optional keep — see §2) |
| `qwen35-rt.py` | 258 | **delete** |
| `qwen35-run.py` | 10,268 | **delete** |
| `qwen35-run.sh` | 492 | **delete** |
| `qwen35-serve.py` | 18,961 | **delete** |
| `reference.json` | 606 | **delete** |
| `tokenizer.json.gz` | 3,516,051 | **delete** |

Executable bit summary after the deletions: only `qwen35-launch.sh` (rewritten)
and `qwen35-mkconfig.py` (new, from another task) need `0755`.


1. Delete list with reasons
---------------------------

### `qwen35-convert.sh` (84 lines) — DELETE
Build-host (x86) QAIRT conversion script: `qnn-onnx-converter` /
`qnn-context-binary-generator` against `/opt/qairt/2.45.0.260326`, producing a
`kvc.bin` context for the hand-exported graph. We do not compile graphs at all
any more — the compiled EPContexts come from the pinned HuggingFace release.
Nothing references it.

### `qwen35-model-install.sh` (143 lines) — DELETE
Board-side installer that copies `qwen08b_i8.onnx` / `kvc.bin` /
`emb08_f16.npy` from a USB stick into `/var/lib/qwen35`, verifies them and
restarts the unit. Superseded: the assets are installed by the model package at
image build time (1.8 GB, pinned by SHA-256 in the recipe). This script is part
of the "model travels separately" design that we explicitly dropped.

### `qwen35-ready.py` (27 lines) — DELETE (available if the new launch.sh wants it)
Probes `http://$QWEN35_HOST:$QWEN35_PORT/v1/models` and exits 0 when the server
answers; used by `qwen35-launch.sh` and `qwen35-model-install.sh` to write the
`/run/qwen35.ok` marker. It is harmless, but the new runtime already serves
`GET /health` and `launch.sh` can wait on that in ~6 lines of shell. Keeping a
second readiness probe whose only role is the old marker risks reintroducing
the stale-marker bug the file's own docstring warns about.
**If the reviewer prefers to keep `/run/qwen35.ok` (npu-chat integration) and
reuse this file, it needs no change and is the only candidate here worth
keeping.**

### `qwen35-rt.py` (11 lines) — DELETE
Thin CLI whose entire body is `from qwen35rt import main`. `qwen35rt.py` no
longer exists in this directory (it was removed earlier), so this file is a
guaranteed `ImportError`. Dead on arrival.

### `qwen35-run.py` (227 lines) — DELETE
Drives one decode step at a time through `qnn-net-run`, manually shuttling
recurrent state between invocations, with `layout.json` position-based input
ordering, `--use_native_input_files`, and notes about `moveaxis` quirks. The
new native engine (`qwen_text_engine` + the 24-graph Prefill / Decode
EPContexts) replaces the whole `qnn-net-run` harness. `layout.json`, `mklayout.py`
and `qnnres.cpp` that this file depended on are already gone.

### `qwen35-run.sh` (11 lines) — DELETE
`exec python3 qwen35-run.py --ctx …` wrapper, so it dies with the file above.

### `qwen35-serve.py` (464 lines) — DELETE
The old OpenAI-compatible server with `--backend ort|htp`, `qwen08b_i8.onnx`
int8 CPU path and the W8A16 `kvc.bin` HTP path whose text was noise. Fully
replaced by the upstream `runtime/2k/qwen_server.py` + `request_frontend.py` +
`stage_worker.py`. Keeping it would ship two competing servers, two endpoint
sets and two sampling policies in one image.

### `reference.json` (606 bytes) — DELETE
Hand-recorded greedy continuation of the old self-test prompt on
`qwen08b_i8.onnx` (tokens `[9419, 0, 2500, …]`), consumed by
`qwen35-serve.py --selftest`. The model, the tokenizer path and the self-test
mechanism are all different now: the native engine has its own
`--sampler-self-test` and the upstream release ships its own checks. A pinned
token list for a model we no longer install is a false alarm generator.

### `tokenizer.json.gz` (3,516,051 bytes) — DELETE
**The asset bundle already carries the tokenizer, byte-identical.** Evidence:

```
zcat tokenizer.json.gz | wc -c   -> 12807982
zcat tokenizer.json.gz | sha256sum
  5f9e4d4901a92b997e463c1f46055088b6cca5ca61a6522d1b9f64c4bb81cb42
assets/common/tokenizer.json (upstream, 12807982 bytes) sha256
  5f9e4d4901a92b997e463c1f46055088b6cca5ca61a6522d1b9f64c4bb81cb42
```

Upstream wiring confirms it: `assets/manifest.json` maps
`roles.common.tokenizer = "common/tokenizer.json"`, and
`runtime/2k/qwen_server.py` loads exactly that path
(`Tokenizer.from_file(str(self.paths["tokenizer"]))`, `paths` filled from
`runtime.json` — see `install.py`, `cfg['paths'] = {…role_map…}`). Upstream's
`tools/fetch_assets.py --profile 2k` downloads `common/tokenizer.json`; there is
no second tokenizer in the release.

So this file is a redundant 3.4 MB copy of a 12.8 MB asset we install anyway,
in a format (`gzip`) the runtime does not read (`tokenizers.Tokenizer.from_file`
takes the JSON stream directly; the existing code only "works" because
`qwen35-serve.py` decompresses it by hand). Deleting it also removes the only
remaining binary blob from the recipe directory.

Only `qwen35-launch.sh` and `qwen35-serve.py` referenced it, and both are being
replaced.


2. `tokenizer` — decision
-------------------------
Delete the local copy. The single source of truth is
`/usr/share/qwen35/common/tokenizer.json`, installed from the pinned HF asset and
named in `runtime.json`. Nothing in `files/` needs to ship a tokenizer.


3. Not deleted, but must be rewritten before the recipe can build
----------------------------------------------------------------
These two are **kept in place** (the new design still needs a unit and a launch
script) but their current content is CPU-plan content. They are not mine to
edit in this task; flagging for the recipe owner.

### `qwen35-launch.sh` — mostly done, verify only
This file has been rewritten by its owner since the first draft of this
CLEANUP.md and now satisfies §3a: `ADSP_LIBRARY_PATH` is built with `;`, it
starts from `$SKEL_DIR` (the pinned wheel directory), appends only
`/usr/lib/dsp/cdsp;/usr/lib/dsp/adsp;/dsp` with a duplicate check, exports
`LD_LIBRARY_PATH="$LIB_DIR:$QNN_DIR:$LIB_DIR/lib…"`, and carries a QNN
pairing self-check. Nothing in §3a requires a change here **as long as the
`$SKEL_DIR` / `$QNN_DIR` the unit passes in really come from one wheel** — that
is the remaining risk, and it is a recipe/packaging question, not a script one.

Do not reintroduce, when finishing the design migration: `QWEN35_BACKEND=ort|htp`,
the `qwen35-serve.py --selftest` call, `tokenizer.json.gz` decompression, the
`/run/qwen35.ok` marker or the `^mechanical: ok` grep. The shims `qwen-python`
and `engine.sh` are generated here because the generated `runtime.json` points
at them.

### `qwen35.service` — mostly done, verify only
Also updated: `QWEN35_QNN_DIR=/usr/lib/qwen35/qnn`,
`QWEN35_SKEL_DIR=/usr/lib/dsp/cdsp`, `StateDirectory=qwen35`,
`RuntimeDirectory=qwen35`, `CPUAffinity=4-7`, and a correctly `;`-separated
`Environment=ADSP_LIBRARY_PATH=/usr/lib/dsp/cdsp;/usr/lib/dsp/adsp;/dsp` as a
floor. Keep `Nice=-5`, `Restart=on-failure`, `RestartSec=10` and
`After=/Wants=adsp.service adsprpcd.service`. Two things still worth a look:

* `MemoryMax` — the NPU path holds ~3 GB resident; with the 1 GB embedding
table and up to 768 MiB of `/dev/shm` scratch, a tight cap kills the service
mid-request rather than early.
* `ConditionPathExists` must point at the **new** asset paths
(`/usr/share/qwen35/models/2k/decode_ctx.onnx`,
`/usr/share/qwen35/common/embedding_fp32.npy`) so the unit is skipped, not
failed, when the model package is absent.


3a. Board-verified DSP/QNN facts — binding for launch.sh, service and engine.sh
--------------------------------------------------------------------------------
Measured on the PI H1; NPU inference is confirmed working there. Four rules:

1. **`ADSP_LIBRARY_PATH` is `;`-separated, never `:`.** A colon-joined value is
taken as a single bogus directory name and the DSP reports
   `Failed to load skel, error: 1002` / `QnnDevice_create 14001`, which reads
   like a firmware bug. Upstream `install.py` builds it with `';'.join(dsp)`
   — do not "fix" that. *(launch.sh and service now both comply; the old
   colon form must not come back.)*
2. **Backend and skel must be the same QAIRT version.** The image carries two
   stacks: `/usr/lib/rfsa/adsp` + `/usr/lib/dsp/cdsp` = QAIRT **2.47.0**, and
   `/usr/lib/npu-llm` = QAIRT **2.40.1**. A 2.40 `libQnnHtp.so` with a 2.47
   `libQnnHtpV68Skel.so` crashes. Take **both** from the `onnxruntime-qnn`
   wheel (this package's choice), or both from one pinned QAIRT; put that
   directory first in `LD_LIBRARY_PATH` **and** `ADSP_LIBRARY_PATH`, and assert
   it in the recipe (`qwen35-mkconfig.py --verbose` prints the resolved
   `qnn_plugin`/`qnn_backend`; fail the build if their parents differ).
   Note `resolve_qnn()`'s last-resort fallback to `/usr/lib/aarch64-linux-gnu`
   is a pairing hazard for the same reason — a wheel-only build should not
   silently inherit a system QAIRT.
3. **Only `/usr/lib/dsp/*` is DSP-visible.** CDSP — which QNN HTP uses, log
   line `Effective cdsp_id is: 3` — sees `/usr/lib/dsp/cdsp`; ADSP sees
   `/usr/lib/dsp/adsp`. `/usr/lib/rfsa/adsp` is **host-side**: it must not be
   relied on for the skeleton, and listing it re-opens rule 2.
4. **The stack itself is proven.** genie + `llama3.2-1b` with
   `ADSP_LIBRARY_PATH='/usr/lib/dsp/cdsp;/dsp'`,
   `LD_LIBRARY_PATH=/usr/lib/npu-llm:/usr/lib` and
   `/usr/lib/npu-llm/genie-t2t-run -c model-config.json --prompt …` answers
   `2+2=4`. Use that as the bisection baseline before blaming this package.

Reference values (single source of truth is `$QNN_DIR`, the pinned wheel
directory, expanded in `qwen-python` / `engine.sh` — not hand-written twice):

```sh
ADSP_LIBRARY_PATH="$SKEL_DIR;/usr/lib/dsp/cdsp;/usr/lib/dsp/adsp;/dsp"
LD_LIBRARY_PATH="$QNN_DIR:$ORT_CAPI_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

`$QNN_DIR` must contain `libQnnHtp.so` **and** `libQnnHtpV68Skel.so` from the
same wheel.


4. Recipe-side follow-ups (not edited here)
-------------------------------------------
`qwen35-npu_1.0.bb` still lists the deleted files in `SRC_URI` and `do_install`,
and still carries the CPU `SUMMARY`/`DESCRIPTION`, `ORT_WHEEL=1.31.0`,
`QWEN35_MODEL_FILES`, `qwen08b_i8.onnx`/`kvc.bin` artifact table and
`/var/lib/qwen35` packaging. Per the task rules this bb is out of scope for me;
whoever rewrites it must, in the same commit:

1. drop every `file://` entry for the files deleted above
   (`qwen35-convert.sh`, `qwen35-model-install.sh`, `qwen35-ready.py`,
   `qwen35-rt.py`, `qwen35-run.py`, `qwen35-run.sh`, `qwen35-serve.py`,
   `reference.json`, `tokenizer.json.gz`);
2. drop `export_prefill.py`, `qwen35rt.py`, `qwen35ort.py`, `qnnlib.py`,
   `qnnres.cpp`, `offload_kv.py`, `mklayout.py`, `layout.json`,
   `kv_manifest.json`, `calib_gen.py`, `prep_native.py` — **already absent from
   `files/`, so `SRC_URI` currently fails at parse time**;
3. replace the `CLOSED` license with `LICENSE = "Apache-2.0"` plus
   `LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=…"`
   (or the local `file://LICENSE`), and install `LICENSE`/`NOTICE` into
   `${docdir}/qwen35-npu/`;
4. switch `do_install` to the `/usr/share/qwen35` + `/usr/lib/qwen35` layout and
   drop the old `/var/lib/qwen35` and `${datadir}/qwen35-npu/tools` trees.

Verified nothing else in the tree references the files proposed for deletion
except the two files already listed in §3 and this bb.

`files/../.gitignore` still lists the old CPU artifacts (`qwen08b_i8.onnx`,
`kvc.bin`, `emb08_f16.npy`, 720/764/508 MB comments). Harmless — it ignores a
`files/model/` directory that no longer exists — but it should be rewritten to
say that the assets are fetched by the model recipe and never live here (keep
the `__pycache__/`, `*.pyc` entries). Not changed here.


5. Vision: resolved on the board — ship the contexts, keep the runtime unpatched
-----------------------------------------------------------------------------
`files/runtime/qwen_server.py` is **byte-identical to upstream** (verified with
`diff`) and its
`_validate_paths()` requires the three Vision assets to exist as files:

```python
required = {… "vision_patch_model", "vision_body_model", "vision_merger_model", …}
absent = [key for key in sorted(file_keys) if not self.paths[key].is_file()]
if absent:
    raise FileNotFoundError(f"configured files are absent: {', '.join(absent)}")
```

`stage_worker.py` also opens the three vision sessions during warmup, so a
text-only asset tree cannot boot at all: the process exits with
`configured files are absent: vision_body_model, vision_merger_model,
vision_patch_model`.

**Decision (board-verified, not a proposal):** ship the Vision contexts —
`QWEN35_VISION_IN_IMAGE ??= "1"` is the shipped default and `"0"` is only valid
together with a patch to those two call sites, which this package does not
carry. The reason is capability, not just the startup check: with the contexts
installed, image requests work. `/v1/chat/completions` with one base64 PNG
returned `mode=image`, the right colour word, `vision_run_seconds=1.22` and
674 prompt tokens on the PI H1. That path does need **Pillow**
(`request_frontend.py::preprocess_image` imports it unguarded), which is why
`recipes-quectel/onnxruntime/pillow_11.3.0.bb` exists; before it was added the
same request answered 500 `ModuleNotFoundError: No module named 'PIL'`.

For the record, the text-only variant would have cost 166 MB less but needed a
runtime patch and would have kept three graphs in the image that nothing could
call.
