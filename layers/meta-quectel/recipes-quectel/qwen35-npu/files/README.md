Qwen3.5-0.8B on the Quectel PI H1 (QCS6490) — ONNX Runtime QNN NPU build
========================================================================

```text
LICENSE      Apache-2.0. Read it before you ship an image built from this.
NOTICE       Where every piece came from and what this integration changed.
```

This package runs the Qwen3.5-0.8B language model on the Hexagon NPU of the
QCS6490 (HTP v68) through the **standard ONNX Runtime QNN Execution Provider**
with **pre-compiled EPContext graphs**. The neural network executes on the NPU;
the CPU only prepares inputs, looks up token embeddings, transfers recurrent
state and samples tokens. There is no CPU fallback partition for model nodes.

**Text and single-image input both work.** A chat request may carry one
`image_url` data URL (PNG/JPEG/WebP); the three Vision EPContext graphs encode it
on the NPU first. Read "Not supported" below before you file a bug.

Provenance in one paragraph: the Python runtime and the C++ engine are the
Apache-2.0 Radxa Dragon Q6A port
(`super617/radxa-dragon-q6a-qwen3.5-0.8b-qcs6490-qnn-npu`, forked from
`PrismPhi/...`); the compiled graphs are the upstream author's published assets
on HuggingFace pinned to one commit; the base model is Qwen3.5-0.8B from the
Qwen team, Apache-2.0. Full statement in **NOTICE**, license text in
**LICENSE**. Nothing here is endorsed by Qwen, Qualcomm, Radxa or Microsoft.


Not supported
-------------
| Not shipped | Why |
|---|---|
| **4K context** | Only the 2,048-token envelope is installed. `models/4k/` and `contracts/4k/` are not downloaded, not packaged, not selectable. |
| Audio, video, batching, embeddings, tool calls, structured JSON, `n > 1` | Not part of the runtime. |
| Remote image URLs, multiple images per request | Only base64 `data:` URLs are decoded, and only one image per request (`max_image_bytes` bounds it). |

The 2K envelope covers system prompt + history + prompt + generated tokens.
It is not the upstream model's native maximum context. Longer conversations are
truncated by dropping the oldest turns; if the current turn cannot fit, the
request is rejected.


Architecture
------------
```text
 client (curl / OpenAI SDK / npu-chat)
   |  HTTP, 127.0.0.1:8000
   v
 qwen_server.py ................ Python: HTTP, chat template, tokenizer,
   |                                     context fitting, sampling policy
   |  UNIX socket + shared files in /dev/shm
   v
 request_frontend.py ....... Python: tokenization of the rendered prompt
 stage_worker.py ........... Python: embedding lookup, M-RoPE input prep,
   |                                 chunked Prefill orchestration, state handoff
   v
 qwen_text_engine (C++) .... owns the ORT sessions and the recurrent state
   |
   +--> prefill_ctx.onnx .............. HTP: 24-layer Chunk16 Prefill
   +--> decode_ctx.onnx ............... HTP: sequential suffix + Decode
   |
   v
 CPU: exact top-k sampling, EOS/stop handling, streaming
```

Why EPContext and not genie: 18 of the 24 layers are gated-DeltaNet (linear
attention with a recurrent matrix), which genie's transformer library cannot
express. The graph is therefore exported to ONNX and compiled ahead of time by
the QNN EP compiler into an **EPContext** binary plus its **external weights**.

The two files of an EPContext pair must always travel together:

| File | Role |
|---|---|
| `decode_ctx.onnx` (222 KB) | The context *model*: graph metadata, input/output contract, embedded EPContext blob reference. Loaded by ONNX Runtime, not executed by it. |
| `decode_ctx_qnn.bin` (847 MB) | The external compiled weights/context. Resolved by name relative to the `.onnx`; renaming or separating the pair breaks the session. |
| `prefill_ctx.onnx` (21 KB) | Same idea for the 24 Prefill graphs, which share the Decode session's QNN context group (ORT EPContext weight sharing). |

The session opens `decode_ctx.onnx` first; the Prefill session then joins the
same context group. That ordering is required by the sharing design and is done
by the native engine, not by this recipe.

State handling: input and output state buffers live in disjoint slices of one
aligned QNN shared-memory arena, so an output can never overwrite an input that
is still being read. State is copied as quantized codes with the source and
destination quantisation parameters respected; it is never round-tripped
through float. Scratch space needs at least 768 MiB free in `/dev/shm`, and the
CPU side is pinned to the four big cores (`CPUAffinity=4-7`). Without the pin,
schedutil holds the big cluster at 1.5 GHz and per-token cost rises ~47%.


Installed file layout
---------------------
Two directories, one rule: **assets are read-only, code is not written to.**

`/usr/share/qwen35/` — model assets, byte-identical to the pinned upstream
release, owned by the model package:

```text
/usr/share/qwen35/
  common/
    embedding_fp32.npy                    1,017,018,848  fp32 token embedding table
    tokenizer.json                           12,807,982  tokenizers JSON (this IS the tokenizer)
  models/2k/
    decode_ctx.onnx                             222,469  EPContext model, decode
    decode_ctx_qnn.bin                      846,712,832  EPContext external weights, decode
    prefill_ctx.onnx                             20,538  EPContext model, prefill
    assembly_report.json                         16,755  how the NPU tail was assembled
  contracts/2k/
    rope_ctx2048_cos_sin_fp32.bin             1,048,576  M-RoPE cos/sin table
    tensor_contract.tsv                         139,720  per-tensor names/shapes/encodings
    advance_plan.tsv                            169,904  graph-to-graph state routing
  models/vision/
    patch_ctx.onnx                                  822  EPContext model, ViT patch embed
    patch_ctx_qnn.bin                         1,712,128  EPContext external weights
    body_ctx.onnx                                   823  EPContext model, ViT body
    body_ctx_qnn.bin                        159,666,176  EPContext external weights
    merger_ctx.onnx                                 833  EPContext model, merger
    merger_ctx_qnn.bin                       13,000,704  EPContext external weights
```

Total ~2.0 GB. There is no separate tokenizer in the runtime: the assets carry
exactly one, and `runtime.json` points at it.

The vision group is not optional at the packaging level even for text-only
traffic: `qwen_server.py::_validate_paths()` lists `vision_patch_model`,
`vision_body_model` and `vision_merger_model` next to the text assets and
refuses to start when one is absent, and `stage_worker.py` opens the three
sessions during warmup. `QWEN35_VISION_IN_IMAGE = "0"` only makes sense together
with a patch to those two places, and the recipe does not ship one.

`/usr/lib/qwen35/` — runtime, engine and configuration, owned by `qwen35-npu`:

```text
/usr/lib/qwen35/
  qwen_server.py            HTTP API, chat template, context fitting, sampling
  request_frontend.py       request-side tokenizer and prompt rendering
  stage_worker.py           persistent decoder driver
  image_frontend.py         image input validation / 1024x576 fixture path
  prompt_bundle.py          prompt/position bundle assembly
  native_handoff.py         shared-arena handoff to the engine
  state_handoff.py          state arena mapping
  state_encoder.py          quantised state re-encoding
  host_memory.py            arena and alignment helpers
  runtime_cache.py          preceding-prefix state reuse
  vision_runtime.py         Vision graph driver (patch/body/merger sessions)
  qwen_text_engine          native engine (cross-compiled, links libonnxruntime)
  engine.sh                 wrapper: taskset -c 4-7 qwen_text_engine ... arena_disjoint
  qwen-python               wrapper: LD_LIBRARY_PATH + ADSP_LIBRARY_PATH + python
  qwen35-launch.sh          start script used by the systemd unit
  qwen35-verify.sh          board-side acceptance check (11 sections)
  qnn/                      QNN plugin + HTP backend libraries (optional; see below)
  runtime.json              all absolute paths, model id, limits, sampling defaults
```

`runtime.json` is generated at build time by `qwen35-mkconfig.py` (upstream's
`install.py` is not used: no aarch64 gate, no package-version lock, no
board-side `g++`, no SHA-256 re-verification at install time — all of that
happens in the Yocto build instead). It records, for every role in the upstream
asset manifest, one absolute path: the six asset paths under `/usr/share/qwen35`,
the program paths under `/usr/lib/qwen35`, the resolved `qnn_plugin` /
`qnn_backend`, the port and the model id. `qwen35-mkconfig.py --check-files`
reports every configured path that is missing on a running system — that is the
first thing to run when the service will not start.

Writable state lives outside both trees, in `/var/lib/qwen35/`: `scratch/`
(prefill staging, delegated to tmpfs request files where possible) and
`logs/requests.jsonl` (the request metrics). `/usr/lib/qwen35` and
`/usr/share/qwen35` are never written to at runtime.


DSP / QNN environment — four rules that decide whether this starts at all
------------------------------------------------------------------------
These are board-verified on the PI H1. All four are non-negotiable; each one
produces a hard, misleading failure when broken.

### 1. `ADSP_LIBRARY_PATH` is `;`-separated, never `:`

```sh
# correct
export ADSP_LIBRARY_PATH='/usr/lib/dsp/cdsp;/dsp'

# wrong -- the whole string is treated as one directory name
export ADSP_LIBRARY_PATH=/usr/lib/rfsa/adsp:/dsp
```

`ADSP_LIBRARY_PATH` is a Qualcomm search-path **list**, not a POSIX path list.
A colon-joined value is silently accepted as one long bogus name, so the DSP
finds no skeleton and the QNN layer reports:

```text
Failed to load skel, error: 1002
QnnDevice_create 14001
```

That error looks like a firmware or version mismatch. It is a separator bug.
Upstream's `install.py` builds the variable with `';'.join(dsp)`; the same rule
is what the runtime does internally (`qwen_server.py`, twice), and it is what
`qwen-python` / `engine.sh` must export.

### 2. Backend and skeleton must be the same QAIRT version

The image can contain **two different QAIRT stacks**:

| Location | QAIRT | Comes from |
|---|---|---|
| `/usr/lib/rfsa/adsp/`, `/usr/lib/dsp/cdsp/` | 2.47.0 | the board firmware / vendor packages |
| `/usr/lib/npu-llm/` | 2.40.1 | the preinstalled `npu-llm` stack (genie) |

A 2.40.x `libQnnHtp.so` loading a 2.47.x `libQnnHtpV68Skel.so` crashes. The
pair must come from **one** source: either both from the `onnxruntime-qnn`
wheel (this package's choice — the wheel's own `libQnnHtp.so` + V68 skel), or
both from one pinned QAIRT release. Never mix, and never let the search path
decide: put the chosen directory **first** in both `LD_LIBRARY_PATH` and
`ADSP_LIBRARY_PATH`, and verify with
`qwen35-mkconfig.py --verbose` that `qnn_plugin` and `qnn_backend` resolve
inside that one directory.

### 3. Only `/usr/lib/dsp/*` is visible to the DSP

| Directory | Visible to | Contents |
|---|---|---|
| `/usr/lib/dsp/cdsp` | **CDSP** — which QNN HTP uses | skel + firmware |
| `/usr/lib/dsp/adsp` | ADSP | skel + firmware |
| `/usr/lib/rfsa/adsp` | **host only** | host-side RPC libraries, *not* DSP-visible |

QNN HTP runs on the CDSP (`Effective cdsp_id is: 3` in the QNN log). Putting
the skel path only in `/usr/lib/rfsa/adsp` gives the same `error: 1002` as a
wrong separator, because the DSP cannot see that directory at all.

### 4. Known-good reference command

The stack itself is proven on this board: genie + `llama3.2-1b` answers
`2+2=4` with

```sh
export ADSP_LIBRARY_PATH='/usr/lib/dsp/cdsp;/dsp'
export LD_LIBRARY_PATH=/usr/lib/npu-llm:/usr/lib
/usr/lib/npu-llm/genie-t2t-run -c model-config.json --prompt '2+2=? answer with the number only'
```

Use it as a bisection tool: if this prints a correct answer, the firmware,
FastRPC, CDSP and the DSP domain are fine and a failure in `qwen35` is ours
(paths, wheels, EPContext pairing). If it also fails, stop debugging the model
and fix the board's QNN stack first.


Service
-------
Not started at boot. The resident server pins four big cores, holds ~490 MB
RSS and owns the whole 850 MB NPU context, so it would degrade every other
workload on the board. It is started on demand:

```sh
systemctl start qwen35   # by hand (the package ships the unit disabled)
#                         # or implicitly: pick qwen3.5 in npu-chat
systemctl stop qwen35    # frees the cores, the RAM and the NPU context
systemctl enable qwen35  # opt in to the old start-at-boot behaviour
```

`npu-chat` starts the unit when the user selects qwen3.5 and stops it again
when the user switches to another model, so the NPU is only held while it is
selected. There is no readiness guesswork: `qwen35-launch.sh` writes
`/run/qwen35.ok` once `GET /v1/models` answers (~4 s of NPU upload after exec),
and the unit removes it in `ExecStopPost`.

```sh
systemctl status qwen35
systemctl restart qwen35
journalctl -u qwen35 -f
tail -f /var/lib/qwen35/logs/requests.jsonl
```

Bound to `127.0.0.1:8000` — loopback only. To reach it from a workstation:

```sh
ssh -N -L 8000:127.0.0.1:8000 root@<board>
curl --fail http://127.0.0.1:8000/health
```

Endpoints (OpenAI-compatible subset, one resident model, serial requests):

| Route | Purpose |
|---|---|
| `GET /health` | readiness, profile, context size, placement, decoder state |
| `GET /v1/models` | active model id (`qwen3.5-0.8b-q6a-2k`) |
| `POST /v1/chat/completions` | JSON or SSE (`stream: true`), text or text + one base64 image |
| `GET /metrics` | local request statistics |

```sh
curl --fail http://127.0.0.1:8000/v1/chat/completions \
  -H 'Content-Type: application/json' \
  -d '{"model":"qwen3.5-0.8b-q6a-2k",
       "messages":[{"role":"user","content":"What is 2 plus 5?"}],
       "max_tokens":64,"stream":false}'
```

Sampling defaults: `temperature 0.7`, `top_p 0.8`, `top_k 20`,
`presence_penalty 1.5`. `max_tokens` defaults to 2048 and is clamped to the
remaining 2K capacity. Roles are `system`/`user`/`assistant` and the request
must end with `user`. For SSE, read `choices[].delta.content` until
`data: [DONE]`; a dropped stream is not a completed answer.

Image input uses the same route with one extra stage. The four parts of the
request content are:

```json
{"role":"user","content":[
  {"type":"image_url","image_url":{"url":"data:image/png;base64,<...>"}},
  {"type":"text","text":"What color is this image?"}]}
```

`request_frontend.py::preprocess_image` decodes it with **Pillow** (shipped as
the `pillow` package — a self-contained manylinux wheel, see
`../onnxruntime/README-DEPS.md` §1; without it this endpoint answers 500
`ModuleNotFoundError: No module named 'PIL'`), EXIF-rotates it, pads it
LANCZOS to 448x448 and normalises to ±1. The NPU then runs patch/body/merger
before the prompt is prefilled. Measured on the PI H1 with a 96x96 PNG:
`vision_run_seconds` 1.22, prompt 674 tokens, TTFT 10.8 s, then decode at the
same ~7 tok/s as a text request. One image per request, `max_image_bytes`
applies; remote URLs are rejected by design.

Expected throughput on this SoC, measured by the upstream project on a Radxa
Dragon Q6A (same QCS6490, HTP v68, 4 big cores): ~7.4 decode tok/s and ~82
prefill tok/s with the Chunk16 schedule. That is a different board revision and
a different thermal envelope, not a guarantee for the PI H1; this board measures
~6.97 tok/s over three runs (spread 1.006x, TTFT ~3.8 s, 341 MiB resident,
51 °C). Dense OCR, multi-step arithmetic and long extraction tasks can still
fail; plausible text is not proof of correctness.


Licenses and provenance
-----------------------
| Component | Origin | Terms |
|---|---|---|
| Python runtime, native engine, docs | `super617/radxa-dragon-q6a-qwen3.5-0.8b-qcs6490-qnn-npu` @ `97650f6`, forked from `PrismPhi/radxa-dragon-…-qnn-npu` v0.1.0-rc1 | Apache-2.0 — see `LICENSE` |
| Compiled EPContext graphs, tensor contracts, embedding table, tokenizer | HuggingFace `PrismPhi/Qwen3.5-0.8B-Radxa-Dragon-Q6A-QCS6490-QNN-NPU` @ `05de67d2f33c0cb83e375f13788895c761d30b1e`, downloaded unchanged and pinned per file by SHA-256 | Apache-2.0 (upstream model derivative) |
| Base model Qwen3.5-0.8B | `Qwen/Qwen3.5-0.8B` @ `2fc06364715b967f1860aea9cf38778875588b17` | Apache-2.0, © Qwen team |
| Bundled ONNX Runtime API headers (`native/ort_include/*.h`) | microsoft/onnxruntime v1.27.0 | MIT, unmodified, notices intact |
| `onnxruntime`, `onnxruntime-qnn` | Official PyPI wheels, separate recipe | Each wheel's own licenses and notices |
| Qualcomm QNN/QAIRT libraries, CDSP firmware | Installed by the target system, **not** part of this recipe | Qualcomm vendor terms |

An Apache-2.0 repository does not relicense the closed vendor binaries that a
runtime package ships. Keep `LICENSE` and `NOTICE` with any image or source
bundle you redistribute. Qwen, Qualcomm, Radxa, Microsoft and Hugging Face
names identify upstream hardware/models/software; there is no endorsement and
no trademark grant.

What this integration changed, relative to upstream: 2K-only (4K not built),
image decoding through a shipped Pillow wheel, Yocto packaging instead of
`install.py` + board venv, split `/usr/lib/qwen35` + `/usr/share/qwen35` layout
with build-time `runtime.json`, and a systemd unit on 127.0.0.1:8000. No asset
was modified. Details in `NOTICE` section 5.


Troubleshooting
---------------
| Symptom | Check |
|---|---|
| Unit inactive right after flashing | The model package is not installed; the unit is skipped, not failed. `ls /usr/share/qwen35/models/2k/` |
| `configured files are absent: vision_*_model` | `_validate_paths()` requires the three Vision assets at startup even for text-only traffic, so a build with `QWEN35_VISION_IN_IMAGE = "0"` cannot boot. Rebuild with the default (`"1"`) or patch the runtime. |
| `no such file: decode_ctx_qnn.bin` | The `.onnx`/`_qnn.bin` pair was separated. They must sit in the same directory with the original names. |
| Fails at session creation, `libQnnHtp.so` not found | QNN runtime missing from the image, or `ADSP_LIBRARY_PATH`/`LD_LIBRARY_PATH` wrong in `qwen-python`. |
| `Failed to load skel, error: 1002` + `QnnDevice_create 14001` | Two causes, check in this order: `ADSP_LIBRARY_PATH` joined with `:` instead of `;` (see rule 1), or the skel path is only `/usr/lib/rfsa/adsp` and therefore invisible to the CDSP (rule 3). |
| Skel loads but the process aborts/segfaults at `QnnDevice_create` | Backend/skeleton version mix (rule 2): a 2.40.x `libQnnHtp.so` with a 2.47.x `libQnnHtpV68Skel.so`, usually because two QAIRT stacks exist in the image. Repoint both at one directory. |
| Runs but is slow, big cluster at 1.5 GHz | Affinity pin lost (`engine.sh` must run under `taskset -c 4-7`). |
| First request fails or is very slow | Cold Prefill plus `/dev/shm` pressure: `df -h /dev/shm` needs ≥ 768 MiB free. |
| Any path-related startup error | `python3 /usr/lib/qwen35/qwen35-mkconfig.py --check-files` lists the missing absolute paths. |
| Garbage or repetitive output | Not a support issue to fix here: the compiled graph and its numerical corrections are upstream artifacts. Report upstream, with the request and `/var/lib/qwen35/logs/requests.jsonl`. |

For the full upstream design notes (porting lessons, measurement scope, the
"delta from one" vs "direct scale" RMSNorm trap, EPContext weight sharing),
follow the documentation links in the upstream repository rather than this
packaging note.
