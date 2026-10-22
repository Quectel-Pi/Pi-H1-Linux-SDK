Qwen3.5-0.8B on the QCS6490 — status and tooling
================================================

This package is the *runtime* for Qwen3.5-0.8B: a resident decode loop, an
OpenAI-compatible server, the systemd unit that starts it at boot, and the
conversion tooling that produced the graph. It ships two backends that share
the same server, tokenizer and model loop:

  ort   qwen08b_i8.onnx -- int8 weights, fp32 activations, onnxruntime on the
        CPU.  DEFAULT.  Logits cosine 0.9925 against an fp32 run; 139 ms/token
        at 4 threads (7.2 tok/s), 6.4 tok/s through the HTTP server.

  htp   kvc.bin -- a W8A16 context binary on the Hexagon V68 NPU.  137 ms of
        graph time per token (5.5 tok/s end to end).  Not used: 16-bit
        per-tensor activations over the whole graph reach only cosine 0.9688,
        which does not keep the top-1 token, so the text is noise.  Kept in the
        tree because every measurement below comes from it.

Why not genie: Qwen3.5 is Qwen3_5ForConditionalGeneration. 18 of its 24 layers
are gated-DeltaNet (linear attention), which genie's transformer library cannot
express, so the genie path that runs Qwen2.5-0.5B and Llama-3.2-1B cannot be
used here.  The graph is exported to ONNX and driven directly.


STATUS (measured 2026-10-22, board 192.168.3.104, QAIRT 2.45, firmware QNN 2.47)
-------------------------------------------------------------------------------

Works
  * whole decode graph exports: 3669 ONNX nodes, 52 inputs, 57 outputs, fp32
    and fp16 variants (the fp32 export reproduces the HF logits at cosine
    0.999996 on the first step)
  * int8 dynamic quantisation: 754,554,881 B, 3677 nodes, 435 initialisers
  * board-side conversation in Chinese and English, multi-turn, streaming
  * service enabled at boot, npu-chat offers qwen3.5 as the default model

Speed

      backend                     graph      HTTP decode     end to end
      htp  (W8A16, 1 ctx)         126 ms     5.3 tok/s       -
      ort  (int8, 4 threads)      139 ms     6.4 tok/s       7.2 tok/s graph-only
      ort  (int8, 8 threads)      188 ms     -               -   (25% slower)

      prompt (prefill)            947 ms / 64 tokens           14.8 ms/token
      prompt (one token at a time) 132 ms / token              132  ms/token

  Threads matter more than the backend: this SoC is 4 big + 4 little cores, and
  an 8-thread int8 MatMul waits on the little cores at every barrier, which cost
  25% (188 -> 139 ms).  IOBinding is no help (142 ms with and without); the
  per-step host bookkeeping is a 52-buffer write plus 12 state copies.

  Three things that looked like wins and are not (each measured on the board):

  * 4-bit weights (ORT MatMulNBits, block 32): 472 MB instead of 754 MB, but
    153 ms/token -- slower than int8, and logits cosine drops 0.9925 -> 0.9868.
    Unpacking nibbles and rescaling per block costs more than the bandwidth it
    saves, because the ARM dot product is int8-only.
  * core affinity: pinning to the 4 big cores (2.4 GHz vs 1.96 GHz) gives
    144 ms; all 8 cores with 4 threads gives 137 ms.  The little cores do help.
  * tuning session options: spinning already on by default and *required*
    (allow_spinning=0: 137 -> 222 ms), mem pattern and arena toggles are noise.

  The graph streams 754 MB of int8 weights per token, i.e. 5.4 GB/s, which is
  the board's measured memcpy rate -- the int8 path is at the memory wall, so
  the only way further down is fewer bytes, and 4-bit does not pay for itself
  on this CPU.

  Time to first token used to be 2.6-4.0 s for a short prompt, because the
  decode graph is batch=1 and every prompt token cost a full pass over the
  754 MB of weights.  It now goes through qwen08b_pf64_i8.onnx, the same model
  exported with 64 prompt tokens as one batch: 947 ms per 64-token block
  (14.8 ms/token) instead of 8.6 s, so a 100 token prompt is answered in 1.9 s
  and the first token of a normal prompt arrives in ~1.4 s.  See "Batched
  prefill" below.

  A published port of the same model for the Radxa Dragon Q6A (same QCS6490,
  same 2 MB VTCM, QAIRT 2.47, C++ host) reports 7.41 tok/s = 135 ms/token at
  2K context; its own server reports 6.62 tok/s (151 ms/token) here.  The int8
  ORT backend is at 139 ms/token of graph time, so this is level with it.

Batched prefill (why the prompt is fast now)
  The decode graph takes one token, so a prompt cost one full pass over the
  weights *per token*: 132 ms each, the same rate as decode, 13.2 s for 100
  tokens.  The fix is a second export of the same model with a 64-token batch
  dimension (export_prefill.py, host side):

  * projections, short convolution, attention (full and DeltaNet read-out) and
    the MLP are all batched over the block, so a layer reads its weights once
    per block.  Those are the 754 MB that mattered.
  * only the gated DeltaNet recurrence keeps a token-by-token loop: it is a
    serial dependency, so it is unrolled 64 times in the graph.  That is 42,872
    ONNX nodes and 5.4 s of load time.
  * the block is always 64 lanes, so a short prompt is fed with `padn` *leading*
    pad lanes.  Leading, not trailing, because the short convolution reads the
    three previous tokens from its state: pads in front only ever see zero
    history, which is exactly the state a fresh sequence has, so a padded block
    is exact.  The fp32 export reproduces the HF logits to cosine 1.0000000
    (measured at L = 1, 8, 23, 64, 100, 128, 200, argmax identical), which is
    why qwen35-serve.py can prefill `ids[:-1]` and then step the last token as
    before -- sampling, stopping and streaming are untouched.
  * after a block the runner copies the conv/rec state and the KV ring into the
    decode graph's own input buffers, so the next token continues from the right
    position.  The two rings have different widths (2047 against 2048) and only
    agree below position 2047, so prompts longer than 2040 tokens still take the
    token path.

  What is left is the recurrence: it touches a [1,16,128,128] fp32 state 64
  times per layer, about 5 GB of traffic per block against 754 MB of weights, so
  947 ms is mostly elementwise bandwidth on the state, not weights.  A chunked
  (WY/UT) DeltaNet would touch the state once and is the next step if prompt
  latency ever matters more; the export is exact today, which is what the next
  change would have to preserve.

  Accuracy of the batched path is below the token path, and this is the
  quantiser, not the export.  The fp32 exports agree with each other to cosine
  0.999996 (12 and 45 token prompts, argmax identical).  Quantising 64 lanes
  together gives ORT one activation range per MatMul where the decode graph had
  one per token, so on the same two prompts:

      fp32 decode vs fp32 prefill   0.999996
      int8 decode vs fp32 decode    0.990 / 0.971
      int8 prefill vs fp32 decode   0.938 / 0.936
      int8 prefill vs int8 decode   0.945 / 0.958

  Top-1 agreed on both prompts and the answers stay coherent (English, Chinese
  and code prompts all read normally, see the sample outputs above), but a
  greedy continuation can diverge from the token path after a few tokens.  The
  self-test therefore reports `prefill: cosine 0.9xxx` instead of claiming the
  two are identical, and only fails below 0.85 -- a wrong state hand-over looks
  nothing like quantisation noise.  The fix, if the accuracy ever matters, is
  to calibrate static per-tensor activation scales over real prompts instead of
  taking them per call; that is the same trade the HTP path lost.

  At boot the second graph costs 5.4 s of load time and 720 MB of RSS (the
  service sits at 3.1 GB).  With it loaded decode is unchanged (139.0 against
  134.2-137.9 ms/token, inside run-to-run noise).  QWEN35_PREFILL=0 turns the
  batched path off; a missing or unreadable graph does the same by itself and
  the self-test prints `prefill: absent`.

Accuracy, ort backend
  * logits cosine 0.9925 against an fp32 run of the same ONNX, same feed
  * 12*12 = 144, "capital of Australia" = Canberra, 17x23 gets the steps right
    but the product wrong (401) -- that is the 0.8B model, not the quantiser
  * sample output, greedy, temperature 0.7:

        Q: 你好，请用三句话介绍你自己。
        A: 你好！我是 Qwen3.5，由阿里巴巴集团旗下的通义实验室自主研发的
           超大规模语言模型，拥有 8200 亿参数，旨在提供幻觉少、知识准确、
           逻辑严密以及高效智能的对话服务。

        Q: 用一个比喻解释什么是神经网络。
        A: 如果把**神经网络**比作一个**超级大脑**，那么它的功能就是：
           **它就像一个有意识、会自我学习、并能根据"感受器"读取"信号"来做
           出决策的复杂系统。** ...

        Q: Write a haiku about the ocean.
        A: Deep blue waves crash, / Shattered by the sun, / With gold and silence.

  The same prompts through the batched prefill path (time to first token in
  brackets) come out just as clean, e.g.

        Q: What is the capital of France?                        [1.4 s]
        A: The capital of France is **Paris**.
           Located in the heart of the country, Paris is the largest city ...

        Q: 用三句话介绍你自己，并说明你能做什么。                     [1.4 s]
        A: 我是一个人工智能助手，由人工智能技术构建，旨在通过逻辑推理、语言理解、
           代码执行和数据分析等能力，为用户提供高效、准确的信息和服务。...


  (The "8200 亿参数" is the model inventing its own size -- the real figure is
  0.8B.  Nobody asked it to be accurate about itself.)

Why the NPU path was set aside
  The graph runs correctly on the HTP and is slightly faster per token than the
  CPU, but the quantisation the V68 backend allows costs too much accuracy:

  * every activation is stored with one per-tensor scale -- 16 bits, and 8 bits
    for the 181 tensors whose two operands are both dynamic (all 12 attention
    MatMuls, i.e. the DeltaNet read-out).  QNN reinterprets tensors in place at
    the graph boundary: of 36 state in/out pairs, 25 use different encodings on
    the way out and the way in.
  * measured on a real self-regression feed: quantising only the weights
    reproduces fp32 at cosine 0.9991, switching activation quantisation on drops
    it to 0.9586 (the device reaches 0.9688).  An offline simulator that applies
    the per-tensor round-trip to 1621 tensors reproduces the device to 0.9888
    and its top-5 tokens match the device in 4 places, so this is the whole
    story -- not a layout bug, not the tokenizer, not the calibration set.
  * it is *not* a few bad tensors.  Per-tensor error ranking says the top 5
    tensors hold 52% of the error variance, but skipping them changes the
    result by 3e-5, and so does skipping every `x*x` and every `q^T @ S`.  The
    error is spread over 24 layers and 1621 round-trips; there is no local fix.
  * the read-out is where it hurts: |S @ q| max is 0.015 while its summands are
    ~0.6 x 0.9, so a 6e-4 relative error on the operands becomes ~1e-2 on the
    result, and layer 0's hidden state already lands at cosine 0.85.

  Escape routes, all tried and closed on this SoC:
  * IEEE fp16 arithmetic: V68 has none.  A FLOAT_16 context is refused by the
    skel ("failed to process context binary", 5005) even at 504 MB, and the
    firmware's own QNN 2.47 rejects fp16 I/O the same way.
  * 16-bit weights (A16W16): "QnnBackend_validateOpConfig failed 3110".
  * fp32 or fp16 I/O via --preserve_io / --custom_io: 3110 as well; --custom_io
    is the only I/O override that QAIRT 2.45 accepts, and its best form (one
    shared encoding per state pair, cos 0.9688) is still not enough.
  * entropy calibration instead of min/max: 1 tensor out of 455 changed,
    device cosine identical to four decimals.
  * per-tensor vs per-channel, symmetric schema, KV cache in 8 bits, bias in
    32 bits (already the default): each measured, none moves the needle.
  * splitting the graph into 24 per-layer contexts so the states can cross
    between them in fp32: 24 x 19 ms of FastRPC overhead = 456 ms/token, 3.3x
    slower than the single context.
  * the Adreno GPU backend (fp16): 597 ms/token and logits that come out NaN.

  The residual-split idea (feed each state as S_hi + S_lo/256 and add two
  matmuls) would give ~24 effective bits and is the one NPU route that might
  still work, but it needs a re-export with the state arithmetic inside the
  graph, and the CPU backend already delivers better accuracy at the same
  speed.


FILES
-----
Runtime (board)
  qwen35ort.py        decode loop for the int8 ONNX (onnxruntime, CPU) -- default
  qnnlib.py           ctypes binding to libQnnHtp/libQnnSystem (no headers needed)
  qwen35rt.py         HTP decode loop, tokenizer, KV ring, sampling; the
                      tokenizer is shared by both backends
  qwen35-rt.py        CLI wrapper (same arguments as qwen35rt.py)
  qwen35-serve.py     OpenAI-compatible server, --backend ort|htp
  qwen35-launch.sh    self-test, marker, then exec the server (systemd entry)
  qwen35-model-install.sh  find the artifacts, verify, install, restart
  qwen35.service      systemd unit, enabled by the image
  layout.json         onnx shape vs QNN declared layout for all 101 tensors
  kv_manifest.json    which past_*_in each 1 KB new-slice output belongs to
  reference.json      the 8 tokens the self-test expects (quality: pass)
  tokenizer.json.gz   tokenizer (12.8 MB json, 3.5 MB compressed)

Conversion (host)
  qwen35-convert.sh   export -> calibrate -> quantize -> context binary
  export_prefill.py   the 64-token prefill export (a second graph, same weights)
  offload_kv.py       move the KV ring buffer out of the graph (2.1x)
  mklayout.py         build layout.json from the context JSON
  calib_gen.py        closed-loop CPU calibration generator
  prep_native.py      ONNX-order raws -> native layout (+quantise)

Legacy (qnn-net-run path, kept for A/B measurements)
  qwen35-run.py, qwen35-run.sh, qnnres.cpp (C++ version of qnnlib, optional)


USAGE
-----
The weights ship in the firmware. `quecpi-image` installs qwen35-npu-model,
which drops them into /var/lib/qwen35:

    qwen08b_i8.onnx      720 MB  int8-weight ONNX decode graph (ORT backend)
    qwen08b_pf64_i8.onnx 720 MB  the same weights, 64 prompt tokens per call
    kvc.bin              764 MB  QNN context binary (only for QWEN35_BACKEND=htp)
    emb08_f16.npy        508 MB  fp16 token embedding table, (248320, 1024)

The service is enabled at boot, so on a normal firmware qwen3.5 is up within a
few seconds and npu-chat offers it as the default model.  The unit carries

    Environment=QWEN35_BACKEND=ort
    Environment=QWEN35_THREADS=4
    ConditionPathExists=/var/lib/qwen35/qwen08b_i8.onnx
    ConditionPathExists=/var/lib/qwen35/emb08_f16.npy

so a runner-only build is skipped rather than failed.  Set QWEN35_BACKEND=htp
(and ship kvc.bin) to run the NPU graph instead.

onnxruntime is not a Yocto recipe here: the image's python3 is the Debian 3.13
overlaid by quecpi-image, so the recipe unpacks the manylinux aarch64 wheel
(1.31.0, cp313) into /usr/lib/python3/dist-packages from SRC_URI.  It needs
numpy and packaging, both already in that rootfs.  Nothing else: flatbuffers and
protobuf are only used by the quantisation helpers, not at inference time.

The model files are build products, not source, so they are not in this layer
and not in git: qwen35-npu_1.0.bb fetches them from the GitHub release

    https://github.com/super617/Qwen3.5-for-qcs6490/releases

during do_fetch, verifies each one against the sha256 in
QWEN35_MODEL_ARTIFACTS, and installs it from ${WORKDIR}/qwen35-model.  They
stay in DL_DIR, so a rebuild or a second image does not download them again.
QWEN35_MODEL_TAG (default latest) picks the release tag; QWEN35_MODEL_URL
overrides the whole base, which is what an offline build uses:

    # local.conf: the four artifacts in a directory instead of the release
    QWEN35_MODEL_URL = "file:///data/qwen35-model"

only the files named in QWEN35_MODEL_FILES are fetched, so an ORT image never
downloads kvc.bin and an HTP image never downloads the two ORT graphs.  Files
already sitting in files/model/ (git-ignored) do not participate any more --
point QWEN35_MODEL_URL at that directory if you want to use them.

An anonymous python function expands QWEN35_MODEL_FILES into SRC_URI plus one
SRC_URI[name.sha256sum] per artifact, so a file the table does not know is a
parse-time bb.fatal rather than a 1.9 GB download that fails afterwards.  The
checksum has to go in as a varflag:

    d.setVarFlag("SRC_URI", "%s.sha256sum" % label, sha)    # right
    d.setVar("SRC_URI[%s.sha256sum]" % label, sha)           # wrong

the datastore has no bracket syntax, so setVar() creates a variable whose name
contains the brackets and bb.fetch2's getVarFlag("SRC_URI", name) still sees
nothing -- do_fetch then completes every download and only afterwards reports
"Missing SRC_URI checksum".

How to produce them:

  qwen08b_i8.onnx
                 export the decode graph to fp32 ONNX (qwen35-convert.sh wraps
                 this), then

                     from onnxruntime.quantization import quantize_dynamic, QuantType
                     m = onnx.load("qwen08b_fp32.onnx")   # under 2 GB, see below
                     quantize_dynamic(m, "qwen08b_i8.onnx", weight_type=QuantType.QInt8)

                 Pass the ModelProto, not a path: the fp32 graph is 3.0 GB and
                 onnx.save() fails with "Failed to serialize proto" (protobuf's
                 2 GB limit) once it has grown a sidecar.  Result: int8 weights,
                 fp32 activations, 754,554,881 B.

  kvc.bin        qwen35-convert.sh, then offload_kv.py, then
                 qnn-context-binary-generator. 763,400,192 bytes for the
                 24-layer W8A16 graph with a 2 MB VTCM.
  qwen08b_pf64_i8.onnx
                 the same weights again, but exported with a 64-token batch
                 dim (export_prefill.py) and therefore 42,872 nodes: the
                 DeltaNet recurrence is unrolled once per lane, everything
                 else is batched.  760,231,420 B.  quantize_dynamic with the
                 same settings as the decode graph.

  emb08_f16.npy  the embedding weight of the official Qwen3.5-0.8B release:

                     model.language_model.embed_tokens.weight
                     [248320, 1024] bf16 -> np.asarray(..., float16)
                     np.save("emb08_f16.npy", emb)

                 The SHA256SUMS asset on the release pins the exact file the
                 recipe downloads.

QWEN35_MODEL_FILES picks which artifacts the firmware carries (default
"qwen08b_i8.onnx qwen08b_pf64_i8.onnx emb08_f16.npy"; use "kvc.bin
emb08_f16.npy" for an HTP-only image, or drop the pf64 graph to save 720 MB --
prompts then go through one token at a time and the runner says so in its
self-test).  A release asset that is missing only breaks the configuration that
names it.  Setting QWEN35_MODEL_IN_IMAGE = "0", or dropping qwen35-npu-model
from IMAGE_INSTALL, builds the runner only; the weights can then be installed on
target:

    qwen35-model-install.sh /run/media/sda1/qwen35
    # or: qwen35-model-install.sh /tmp   (also searches /tmp/qwen35, /media/*)
    # copies, checks the size, then systemctl restart qwen35
    # QWEN35_MODEL_FILE=kvc.bin to install the QNN context instead

Then:

    systemctl start qwen35            # or: /usr/share/qwen35-npu/tools/qwen35-launch.sh
    curl -s localhost:8000/health
    curl -s localhost:8000/v1/chat/completions -H 'Content-Type: application/json' \
         -d '{"messages":[{"role":"user","content":"hi"}],"max_tokens":16}'

Direct use without the server:

    cd /usr/share/qwen35-npu/tools
    python3 qwen35ort.py --prompt "Hello" --steps 32          # int8 ONNX
    python3 qwen35-rt.py --ctx /var/lib/qwen35/kvc.bin \
        --emb /var/lib/qwen35/emb08_f16.npy --prompt "Hello"  # HTP

How npu-chat decides
  qwen3.5 is the first entry in the model list, so it is the default. The gate
  is deliberately simple, because a firmware build ships the weights and
  enables the unit:

    1. the weights are in /var/lib/qwen35 (the embedded table plus either
       qwen08b_i8.onnx or kvc.bin -- whichever backend the unit uses)
    2. a launcher exists
    3. either /run/qwen35.ok exists, or the unit is enabled

  Rule 3 is what makes it work on a normal boot: the launcher loads and
  self-tests the model for a few seconds before it answers, so the GUI usually
  starts first.  If the unit is enabled, the server is either up or on its way,
  and the first turn waits for it inside start_qwen35().  A manual install with
  the unit disabled still has to prove itself with the marker.

  QWEN35_HIDE=1 hides the model, QWEN35_FORCE=1 skips the checks, and
  QWEN35_DIR / QWEN35_OK override the paths.

  Accuracy is recorded separately in /run/qwen35.quality, which the self-test
  fills in by comparing its 8 greedy tokens against reference.json
  ("pass"/"MISMATCH").  It is not part of the gate: a mismatch means the model,
  the tokenizer or the quantisation changed and the text should be read again.

Notes that cost time
  * ORT's own thread count is the single biggest tuning knob here: 4 threads
    (139 ms) beat 8 (188 ms) and 6 (192 ms); 2 is the same as 4 but leaves less
    headroom.  Do not "fix" this by pinning to the big cluster -- that measured
    144 ms, and allow_spinning=0 measured 222 ms.
  * the tokenizer needs the GPT-2 byte map to be *exactly* right.  `¡`..`¬` is
    0xA1..0xAC, and the middle range of the map ends at 0xAC; a map that stops
    at 0xAB shifts every byte above it by one, which for UTF-8 means 0xAD -- the
    most common CJK continuation byte -- decodes as the previous byte. Chinese
    then comes out as nonsense even though the model is perfect, and encoding
    turns unknown characters into token 0.
  * HTP: QNN tensor ids come from the context binary; a descriptor must be
    copied from it and only its buffer replaced. Rebuilding one from scratch
    fails with "Expected Tensor ID: 1 not found".
  * HTP: a graph retrieved from a serialized context is already finalized;
    graphFinalize answers 0x3e8 and that is not an error.
  * HTP: QnnBackend_create needs a logger, but QnnLog_create on this build
    answers "provider isn't initialized" unless the interface table is read at
    the right offset: 40 for the backend (two versions in Qnn_ApiVersion_t), 32
    for the system library (a single version).
  * HTP: the context reports QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_3, whose
    layout differs from V1/V2; the graph info V1 prefix is still valid.
  * HTP: per-tensor layouts are inconsistent: conv_state is declared [1,6144,3]
    on the way out (ONNX order) but [1,3,6144] on the way in (native). Do not
    blanket-transpose; layout.json has the per-tensor rule.  The ORT backend
    needs none of this -- the graph's inputs are already ONNX-layout.
  * HTP: the KV cache is a ring: attention is invariant to key order, so the
    new slice goes into slot pos % 2047 and the mask admits the written slots.
  * qnn-net-run (legacy path) parses inputs as float32 unless
    --use_native_input_files, and its output is float32 by default.
