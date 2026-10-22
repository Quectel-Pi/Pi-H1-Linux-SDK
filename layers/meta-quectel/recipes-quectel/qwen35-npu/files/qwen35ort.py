#!/usr/bin/env python3
"""qwen35ort.py -- resident decode loop for the int8 ONNX model (CPU/ORT).

Why this backend exists
-----------------------
The HTP path is numerically too coarse for this model.  Every activation in the
QNN graph is stored with a single per-tensor scale -- 16 bits, and 8 bits for
the tensors whose two operands are both dynamic -- and 1621 such round-trips
accumulate across the 24 layers.  Measured on the board: quantising only the
weights reproduces an fp32 run at cosine 0.9991, switching activation
quantisation on drops it to 0.9586 (device: 0.9688), and ablating the worst
tensors one at a time changes nothing -- the error is spread over the whole
graph, so there is no local fix.  At 0.9688 the logits still pick a different
top-1 token often enough that the text turns to noise.

This backend keeps the same model but quantises only the weights: ORT's
`quantize_dynamic` turns every MatMul/Gemm weight into int8 (per-channel,
chosen at run time from the activation range) and leaves activations in fp32.
The same feed then reaches cosine 0.9925 against fp32, and the text is clean.

It is also not slower.  A token is ~139 ms here at 4 threads (7.2 tok/s),
against 126 ms of graph time on the NPU (5.6 tok/s end to end) -- the HTP is
waiting on FastRPC and a serialised 16->8->16 reinterpretation of every tensor,
while the CPU is streaming the 720 MB of int8 weights at very nearly memory
bandwidth.

Threads: 4, not 8.  The SoC has 4 big and 4 little cores; an 8-thread int8
MatMul lets the little cores' slower participation push the barriers later than
the big cores finish, which measured 25% slower than 4 (188 -> 139 ms).

Layout: unlike the QNN backend there is nothing to transpose.  The graph is the
ONNX model itself, so its inputs are already in ONNX layout, and the recurrent
state and KV ring live in the graph's own input arrays -- a step is one
`session.run` plus copying the state outputs back into their inputs.
"""
import json
import os
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import qwen35rt  # noqa: E402  (Tokenizer, cos_sin, CTX_LEN, EOS)

DEFAULT_MODEL = "/var/lib/qwen35/qwen08b_i8.onnx"
DEFAULT_EMB = "/var/lib/qwen35/emb08_f16.npy"
DEFAULT_PREFILL = "/var/lib/qwen35/qwen08b_pf64_i8.onnx"
DEFAULT_THREADS = 4
# Tokens per batched-prefill call.  The export unrolls the DeltaNet recurrence,
# so the graph carries 64 copies of it; 64 is the size the numbers in the class
# docstring were measured at, and it is what a ChatML prompt usually needs.
PREFILL_BLOCK = 64
# Longest prompt the batched path will take: the prefill graph's KV ring is
# 2047 slots and it is written by absolute position, the decode ring is 2048
# slots and is written by position modulo 2048.  The two layouts only agree
# below 2047, so past that the prompt goes through the token path instead.
PREFILL_MAX = 2040
# Below this a single block (947 ms, all 64 lanes) costs more than the tokens
# do one at a time (132 ms each), so short prompts keep the token path.
PREFILL_MIN = 8
CTX_LEN = qwen35rt.CTX_LEN
EOS = qwen35rt.EOS


class Prefill(object):
    """One graph call per PREFILL_BLOCK prompt tokens.

    The decode graph is batch=1 by construction, so a prompt used to cost a
    full pass over the 754 MB of int8 weights *per token*: 132 ms per prompt
    token, identical to decode.  This graph does the same work for 64 tokens at
    once -- the projections, the short convolution, the attention and the MLP
    are batched over the block -- so the weights are read once per block.  Only
    the gated DeltaNet recurrence keeps its token-by-token loop, because it is
    a serial dependency.

    Measured on the board at 4 threads: 947 ms per 64-token block, i.e.
    14.8 ms/token against 132 ms/token, so a 100 token prompt goes from 13.2 s
    to 1.9 s.  What is left is the recurrence: it touches a [1,16,128,128] fp32
    state 64 times per layer, so a block is ~5 GB of state traffic against the
    754 MB of weights.  A chunked (WY/UT) DeltaNet would cut that to one pass
    and is the next step if prompt latency ever matters more.

    Padding: a block is always 64 lanes, so a prompt that is not a multiple of
    64 needs `padn` leading pad lanes.  Leading (not trailing) because the
    short convolution reads the three previous tokens from its state: pads in
    front only ever see zero history, which is exactly the state a fresh
    sequence has, so a padded first block is bit-exact against the unpadded
    run.  Blocks after the first are always full.
    """

    mask_fill = -64.0

    def __init__(self, path, threads, block=PREFILL_BLOCK):
        import onnxruntime as ort
        so = ort.SessionOptions()
        so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_BASIC
        so.intra_op_num_threads = threads
        so.inter_op_num_threads = 1
        t0 = time.time()
        self.sess = ort.InferenceSession(path, so,
                                         providers=["CPUExecutionProvider"])
        self.block_tokens = block
        self.feed = {}
        for i in self.sess.get_inputs():
            self.feed[i.name] = np.zeros([int(d) for d in i.shape], np.float32)
        self.out_names = [o.name for o in self.sess.get_outputs()]
        for n in ("hidden_in", "rope_cos", "rope_sin", "attn_mask", "pad"):
            if n not in self.feed:
                sys.exit("prefill graph has no tensor %s" % n)
        self.carry = [(n, n[:-3] + "_out") for n in self.feed
                      if n.endswith("_state_in")]
        self.kv = [(n, n[:-3] + "_out") for n in self.feed if "_past_" in n]
        self.seq = self.feed["hidden_in"].shape[1]
        self.slots = self.feed[self.kv[0][0]].shape[2]
        self.pos = 0
        self.ms = []
        self.load_ms = (time.time() - t0) * 1000.0
        self.path = path

    def reset(self):
        self.pos = 0
        for t in self.feed.values():
            t[...] = 0
        self.ms = []

    def block(self, hidden, padn=0):
        """Feed `hidden` ([L,1024] fp32) as the last L lanes, return logits."""
        L = hidden.shape[0]
        seq = self.seq
        f = self.feed
        f["hidden_in"][0] = 0.0
        f["hidden_in"][0, padn:padn + L] = hidden
        f["pad"][0, :padn] = 0.0
        f["pad"][0, padn:padn + L] = 1.0
        for j in range(seq):
            c, s = qwen35rt.cos_sin(self.pos + max(j - padn, 0))
            f["rope_cos"][0, j, 0] = c
            f["rope_sin"][0, j, 0] = s
        # one row per lane: the ring slots already written, plus this lane and
        # its predecessors inside the block.  Pad lanes only see the ring.
        m = np.full((seq, f["attn_mask"].shape[3]), self.mask_fill, np.float32)
        m[:, :self.pos] = 0.0
        for j in range(padn, seq):
            m[j, self.slots + padn:self.slots + j + 1] = 0.0
        f["attn_mask"][...] = m.reshape(f["attn_mask"].shape)
        t0 = time.time()
        res = dict(zip(self.out_names, self.sess.run(self.out_names, f)))
        self.ms.append((time.time() - t0) * 1000.0)
        for src, dst in self.carry:
            np.copyto(f[src], res[dst])
        # the graph appends lane j of this block at ring index pos + j, so only
        # the real lanes are copied back; the pad lanes are zero keys and would
        # otherwise leave holes in the ring
        for past, new in self.kv:
            nb = res[new][0, :, self.slots - seq:, :].reshape(-1, seq, 256)
            f[past][0, :, self.pos:self.pos + L, :] = nb[:, padn:padn + L, :]
        self.pos += L
        return res["logits"].ravel()


class Runtime(object):
    """Same surface as qwen35rt.Runtime, so qwen35-serve.py drives either one."""

    def __init__(self, model=None, emb=None, threads=None, manifest=None,
                 mask_fill=-64.0, prefill=None):
        import onnxruntime as ort          # imported here: the HTP backend
                                           # needs no ORT, and vice versa
        model = model or os.environ.get("QWEN35_ONNX", DEFAULT_MODEL)
        emb = emb or os.environ.get("QWEN35_EMB", DEFAULT_EMB)
        threads = int(threads or os.environ.get("QWEN35_THREADS", DEFAULT_THREADS))
        manifest = manifest or os.path.join(HERE, "kv_manifest.json")

        so = ort.SessionOptions()
        # A decode graph is a straight line: no inter-op parallelism is useful,
        # ORT_ENABLE_ALL only costs load time.
        so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        so.intra_op_num_threads = threads
        so.inter_op_num_threads = 1
        t0 = time.time()
        self.sess = ort.InferenceSession(model, so,
                                         providers=["CPUExecutionProvider"])
        self.provider = self.sess.get_providers()[0]
        self.threads = threads

        # Persistent buffers for every graph input; ORT copies from them, but
        # the KV ring has to be a live array anyway because a token updates one
        # slot of it instead of rebuilding 100 MB of cache.
        self.feed = {}
        for i in self.sess.get_inputs():
            self.feed[i.name] = np.zeros([int(d) for d in i.shape], np.float32)
        self.out_names = [o.name for o in self.sess.get_outputs()]
        for n in ("hidden_in", "rope_cos", "rope_sin", "attn_mask"):
            if n not in self.feed:
                sys.exit("graph has no tensor %s" % n)

        # Carried recurrence: every `*_state_in` input has a `*_state_out`
        # output of the same shape (conv_state for the short convolution,
        # rec_state for the gated DeltaNet), so a step is run + copy back.
        self.carry = [(n, n[:-3] + "_out") for n in self.feed
                      if n.endswith("_state_in")]
        # KV: the graph takes the whole ring as an input and appends the new
        # slice at index -1 internally; kv_manifest.json names the tensor that
        # carries that slice back out.
        self.kv = []
        for layer, kinds in sorted(json.load(open(manifest)).items()):
            for kind in ("k", "v"):
                s = kinds[kind]
                if s["past_in"] not in self.feed:
                    sys.exit("graph has no tensor %s" % s["past_in"])
                self.kv.append((s["past_in"], s["new_out"]))
        self.slots = self.feed[self.kv[0][0]].shape[2]

        self.emb = np.load(emb, mmap_mode="r")
        self.mask_fill = mask_fill
        self.pos = 0
        self.ms = []
        # mean_ms stays 0 until the first token has actually run: /health reports
        # it, and a session that has only been loaded is not 2 s/token.
        self.stats = {"mean_ms": 0.0, "inputs": len(self.feed),
                      "load_ms": (time.time() - t0) * 1000.0,
                      "backend": "ort", "threads": threads,
                      "provider": self.provider}
        self.pf = self._load_prefill(prefill, threads)

    def _load_prefill(self, path, threads):
        """Optional: drop the prompt cost from 132 ms/token to ~15 ms/token.

        Any problem here (no file, a graph this build cannot drive, an old
        export) only loses the speedup: the token path below is the reference
        and always stays available, so the load failure is reported and lived
        with rather than taking the service down.  QWEN35_PREFILL=0 disables it.
        """
        path = path or os.environ.get("QWEN35_PREFILL", DEFAULT_PREFILL)
        path = path.strip()
        if path.lower() in ("1", "on", "yes", "true"):
            path = DEFAULT_PREFILL
        if path.lower() in ("0", "off", "no", "none", ""):
            return None
        if not os.path.exists(path):
            self.stats["prefill"] = "absent"
            return None
        try:
            pf = Prefill(path, threads)
            pf.mask_fill = self.mask_fill
        except (Exception, SystemExit) as e:  # noqa: BLE001 - never fatal
            print("qwen35: prefill graph unusable (%s), %s" % (e, path),
                  file=sys.stderr, flush=True)
            self.stats["prefill"] = "broken"
            return None
        self.stats["prefill"] = "%d lane blocks" % pf.seq
        self.stats["prefill_load_ms"] = pf.load_ms
        self.stats["load_ms"] += pf.load_ms
        return pf

    # ------------------------------------------------------------------ state
    def reset(self):
        self.pos = 0
        for t in self.feed.values():
            t[...] = 0
        self.ms = []

    def prefill(self, ids):
        """Run the whole prompt through the batched graph.

        Returns the logits for the token after `ids`, having left the decode
        feed in exactly the state `len(ids)` single steps would have left it in
        (same conv/rec state, same KV ring, same position), or None when the
        batched path cannot take this prompt -- the caller then feeds the
        tokens one at a time.  The two paths are not bit-identical (the int8
        graph is quantised per call, so 64 lanes share one activation range),
        which is why the self-test compares logits instead of tokens.
        """
        if self.pf is None or len(ids) < PREFILL_MIN or len(ids) > PREFILL_MAX:
            return None
        pf = self.pf
        pf.reset()
        n = len(ids)
        padn = (-n) % pf.seq          # one partial block in front, then fulls
        first = pf.seq - padn if padn else pf.seq
        logits = pf.block(np.asarray(self.emb[ids[:first]], np.float32),
                          padn=padn)
        for i in range(first, n, pf.seq):
            chunk = ids[i:i + pf.seq]
            logits = pf.block(np.asarray(self.emb[chunk], np.float32))
        # hand the state to the decode graph: the names and shapes line up,
        # apart from the ring width (2047 against 2048), and the layouts only
        # agree below 2047 -- PREFILL_MAX keeps the prompt inside that
        for src, dst in pf.carry:
            if src in self.feed:
                np.copyto(self.feed[src], pf.feed[src])
        # only the written slots carry keys, and the ring is 100 MB in total:
        # copying pos slots instead of all 2047 keeps that off the critical path
        w = min(pf.pos, pf.slots, self.feed[pf.kv[0][0]].shape[2])
        for past, _new in pf.kv:
            if past in self.feed:
                np.copyto(self.feed[past][:, :, :w], pf.feed[past][:, :, :w])
        self.pos = pf.pos
        self.ms = []
        self.stats["prefill_ms"] = sum(pf.ms)
        self.stats["prefill_tokens"] = n
        self.logits = logits
        return logits

    def write_mask(self):
        """Attention is order-invariant over keys, so the KV cache is a ring.

        The graph concatenates the new slice at the end of past_k, i.e. the
        newest key always lands at index -1, and the 2039 ring slots hold the
        previous positions.  The mask therefore admits exactly the slots that
        have been written (they are contiguous until the ring wraps) plus the
        last lane.
        """
        m = np.full(2048, self.mask_fill, np.float32)
        m[:min(self.pos, self.slots)] = 0.0
        m[-1] = 0.0
        self.feed["attn_mask"][...] = m.reshape(self.feed["attn_mask"].shape)

    def step(self, token):
        self.feed["hidden_in"][...] = np.asarray(
            self.emb[token], np.float32).reshape(self.feed["hidden_in"].shape)
        c, s = qwen35rt.cos_sin(self.pos)
        self.feed["rope_cos"][...] = c.reshape(self.feed["rope_cos"].shape)
        self.feed["rope_sin"][...] = s.reshape(self.feed["rope_sin"].shape)
        self.write_mask()
        t0 = time.time()
        res = dict(zip(self.out_names, self.sess.run(self.out_names, self.feed)))
        self.ms.append((time.time() - t0) * 1000.0)
        for src, dst in self.carry:
            np.copyto(self.feed[src], res[dst])
        slot = self.pos % self.slots
        for past, new in self.kv:
            self.feed[past][0, :, slot, :] = res[new][0, :, 0, :]
        self.pos += 1
        self.logits = res["logits"]
        self.stats["mean_ms"] = sum(self.ms) / len(self.ms)
        return self.ms[-1]

    # ----------------------------------------------------------------- logits
    def logits_f32(self):
        return self.logits.ravel()

    def top1(self):
        return int(np.argmax(self.logits.ravel()))

    def close(self):
        self.sess = None
        self.pf = None


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=None)
    ap.add_argument("--emb", default=None)
    ap.add_argument("--threads", type=int, default=None)
    ap.add_argument("--tok", default=os.path.join(HERE, "tokenizer.json.gz"))
    ap.add_argument("--prompt", default="Hello")
    ap.add_argument("--steps", type=int, default=32)
    a = ap.parse_args()

    rt = Runtime(a.model, a.emb, a.threads)
    tok = qwen35rt.Tokenizer(a.tok)
    qwen35rt._fill_rev(tok)
    body = ("<|im_start|>user\n%s<|im_end|>\n<|im_start|>assistant\n"
            % a.prompt) + "<think>\n\n</think>\n\n"
    ids = tok.encode(body)
    print("loaded in %.1f s (%s, %d threads), %d inputs, %d kv tensors x %d slots"
          % (rt.stats["load_ms"] / 1000.0, rt.provider, rt.threads,
             len(rt.feed), len(rt.kv), rt.slots), flush=True)
    rt.reset()
    for t in ids[:-1]:
        rt.step(t)
    stream = qwen35rt.TokenStream(tok)
    nxt = ids[-1]
    out = []
    for _ in range(a.steps):
        rt.step(nxt)
        nxt = rt.top1()
        if nxt == EOS:
            break
        out.append(nxt)
        sys.stdout.write(stream.push(nxt))
        sys.stdout.flush()
    sys.stdout.write(stream.flush())
    ms = sum(rt.ms[len(ids) - 1:]) / max(len(rt.ms) - len(ids) + 1, 1)
    print("\n%d prompt tokens, %.1f ms/token, %.2f tok/s"
          % (len(ids), ms, 1000.0 / ms))


if __name__ == "__main__":
    main()
