#!/usr/bin/env python3
"""qwen35-serve.py -- OpenAI-compatible HTTP server for the resident runner.

Endpoints:
  GET  /v1/models                 {"data":[{"id":"qwen3.5-0.8b",...}]}
  POST /v1/chat/completions       stream=true (SSE) or false
  GET  /health                    {"ok":true,"ms_per_token":...}

Everything except the sampling runs in one process: the model stays loaded and
the recurrent state stays in the graph's input buffers, so a token costs one
`session.run` (139 ms on this board).  Requests are serialised -- there is
exactly one model and one KV cache.

Two backends share this file:

  --backend ort   int8 weights + fp32 activations (onnxruntime, CPU)
  --backend htp   the W8A16 QNN context binary on the Hexagon DSP

ort is the default because the HTP's 16-bit per-tensor activations cost too
much accuracy to keep the top-1 token (logits cosine 0.9688 against fp32, which
reads as noise; int8 weights reach 0.9925).  See qwen35ort.py for the evidence
and qwen35rt.py for the DSP path.

usage:
  qwen35-serve.py --backend ort --onnx /var/lib/qwen35/qwen08b_i8.onnx \
                  --emb /var/lib/qwen35/emb08_f16.npy \
                  --tok tokenizer.json.gz --port 8000
  qwen35-serve.py --selftest ...        # load, generate, report, exit
"""
import argparse
import gzip
import json
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import qwen35rt  # noqa: E402  (Tokenizer/TokenStream/CTX_LEN live there)

MODEL_ID = os.environ.get("QWEN35_MODEL_ID", "qwen3.5-0.8b")


def load_json(path):
    if path.endswith(".gz"):
        with gzip.open(path, "rt") as f:
            return json.load(f)
    with open(path) as f:
        return json.load(f)


# ------------------------------------------------------------------- options
class Sampling(object):
    def __init__(self, temperature=0.0, top_k=0, top_p=1.0):
        self.temperature = float(temperature or 0.0)
        self.top_k = int(top_k or 0)
        self.top_p = float(top_p or 1.0)

    def pick(self, logits, rng):
        if self.temperature <= 0.0:
            return int(np.argmax(logits))
        x = logits / self.temperature
        if self.top_k > 0 and self.top_k < x.size:
            keep = np.argpartition(-x, self.top_k)[:self.top_k]
            mask = np.full(x.size, -np.inf)
            mask[keep] = x[keep]
            x = mask
        x = x - x.max()
        p = np.exp(x)
        p /= p.sum()
        if self.top_p < 1.0:
            order = np.argsort(-p)
            c = np.cumsum(p[order])
            cut = int(np.searchsorted(c, self.top_p)) + 1
            keep = order[:cut]
            mask = np.zeros(p.size)
            mask[keep] = p[keep]
            p = mask / mask.sum()
        return int(rng.choice(p.size, p=p))


# ------------------------------------------------------------------- service
class Service(object):
    def __init__(self, rt, tok):
        self.rt = rt
        self.tok = tok
        self.lock = threading.Lock()
        self.rng = np.random.default_rng(int(os.environ.get("QWEN35_SEED", "1234")))
        self.started = time.time()
        self.requests = 0
        self.tokens = 0
        self.last_prompt_tokens = 0

    def build_prompt(self, messages, think=False):
        parts = []
        for m in messages:
            role = m.get("role", "user")
            content = m.get("content", "")
            if isinstance(content, list):      # OpenAI content parts
                content = "".join(c.get("text", "") for c in content
                                  if isinstance(c, dict))
            if role == "system":
                parts.append("<|im_start|>system\n%s<|im_end|>\n" % content)
            elif role == "assistant":
                parts.append("<|im_start|>assistant\n%s<|im_end|>\n" % content)
            else:
                parts.append("<|im_start|>user\n%s<|im_end|>\n" % content)
        parts.append("<|im_start|>assistant\n")
        parts.append("<think>\n" if think else "<think>\n\n</think>\n\n")
        return "".join(parts)

    def generate(self, messages, max_tokens=256, sampling=None, think=False,
                 stop=None):
        """Yield (new_text, token_id, done_reason)."""
        sampling = sampling or Sampling()
        prompt = self.build_prompt(messages, think)
        ids = self.tok.encode(prompt)
        rt = self.rt
        rt.reset()
        if len(ids) > qwen35rt.CTX_LEN - 1:
            keep = qwen35rt.CTX_LEN - 2
            ids = ids[:keep] + ids[-1:]
        self.last_prompt_tokens = len(ids)
        # The prompt goes through in 64-token blocks when the batched graph is
        # installed: the weights are read once per block instead of once per
        # token, which is 947 ms for 64 tokens against 8.6 s.  Everything else
        # (sampling, stopping, streaming) is unchanged, and prefill() returns
        # None for prompts it cannot take, so the token loop stays the fallback.
        batched = False
        if hasattr(rt, "prefill"):
            batched = rt.prefill(ids[:-1]) is not None
        if not batched:
            for t in ids[:-1]:
                rt.step(t)
        nxt = ids[-1]
        stream = qwen35rt.TokenStream(self.tok)
        text, emitted = "", ""
        texts = [x for x in (stop or []) if x]
        reason = "length"
        for _ in range(max_tokens):
            if rt.pos >= qwen35rt.CTX_LEN - 1:
                reason = "length"
                break
            rt.step(nxt)
            if sampling.temperature <= 0.0 and sampling.top_k == 0:
                nxt = rt.top1()
            else:
                nxt = sampling.pick(rt.logits_f32(), self.rng)
            if nxt == qwen35rt.EOS:
                reason = "stop"
                break
            piece = stream.push(nxt)
            if not piece:
                continue
            text += piece
            cut = None
            for x in texts:
                at = text.find(x)
                if at >= 0:
                    cut = at
                    break
            if cut is not None:
                keep = text[:cut]
                if keep.startswith(emitted):
                    if len(keep) > len(emitted):
                        yield (keep[len(emitted):], nxt, None)
                yield ("", nxt, "stop")
                return
            emitted = text
            yield (piece, nxt, None)
        tail = stream.flush()
        if tail:
            yield (tail, nxt, None)
        yield ("", nxt, reason)

    def chat(self, body):
        messages = body.get("messages") or []
        max_tokens = int(body.get("max_tokens")
                         or body.get("max_completion_tokens") or 256)
        temp = body.get("temperature")
        if temp is None:
            temp = 0.0
        s = Sampling(temp, body.get("top_k") or 0, body.get("top_p") or 1.0)
        stop = body.get("stop")
        if isinstance(stop, str):
            stop = [stop]
        think = bool(body.get("think")
                     or (body.get("chat_template_kwargs") or {}).get("enable_thinking"))
        return self.generate(messages, max_tokens, s, think, stop)


def make_handler(svc):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        server_version = "qwen35-npu/1.0"

        def log_message(self, fmt, *args):
            if os.environ.get("QWEN35_HTTP_LOG") == "1":
                sys.stderr.write("%s %s\n" % (self.address_string(),
                                              fmt % args))

        def _json(self, code, obj):
            data = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if self.path.startswith("/v1/models"):
                return self._json(200, {"object": "list", "data": [{
                    "id": MODEL_ID, "object": "model",
                    "created": int(svc.started), "owned_by": "local"}]})
            if self.path.startswith("/health"):
                st = svc.rt.stats
                return self._json(200, {
                    "ok": True,
                    "model": MODEL_ID,
                    "backend": st.get("backend"),
                    "ms_per_token": round(st.get("mean_ms") or 0.0, 1),
                    "requests": svc.requests,
                    "tokens": svc.tokens,
                    "ctx_len": qwen35rt.CTX_LEN})
            self._json(404, {"error": {"message": "not found"}})

        def _read_body(self):
            n = int(self.headers.get("Content-Length") or 0)
            if not n:
                return {}
            return json.loads(self.rfile.read(n) or b"{}")

        def do_POST(self):
            if not self.path.startswith("/v1/chat/completions"):
                return self._json(404, {"error": {"message": "not found"}})
            try:
                body = self._read_body()
            except ValueError as e:
                return self._json(400, {"error": {"message": str(e)}})
            stream = bool(body.get("stream"))
            rid = "chatcmpl-%d" % int(time.time() * 1000)
            created = int(time.time())
            svc.requests += 1
            if not stream:
                with svc.lock:
                    out = []
                    reason = "length"
                    for delta, tid, why in svc.chat(body):
                        out.append(delta)
                        reason = why or reason
                        svc.tokens += 1
                content = "".join(out)
                return self._json(200, {
                    "id": rid, "object": "chat.completion", "created": created,
                    "model": MODEL_ID,
                    "choices": [{"index": 0, "finish_reason": reason,
                                 "message": {"role": "assistant",
                                             "content": content}}],
                    "usage": {"prompt_tokens": svc.last_prompt_tokens,
                              "completion_tokens": len(out),
                              "total_tokens": len(out)}})
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.end_headers()

            def send(obj):
                data = ("data: " + json.dumps(obj) + "\n\n").encode()
                self.wfile.write(data)
                self.wfile.flush()

            # A client that goes away mid-answer (a browser tab closed, an
            # aborted curl) must not keep the generator busy: at 140 ms/token a
            # forgotten stream would eat a whole core for minutes.  Record it
            # and stop generating instead of filling the log with tracebacks.
            gone = []
            try:
                send({"id": rid, "object": "chat.completion.chunk",
                      "created": created, "model": MODEL_ID,
                      "choices": [{"index": 0, "delta": {"role": "assistant"},
                                   "finish_reason": None}]})
            except (BrokenPipeError, ConnectionResetError):
                gone.append(True)
            reason = "length"
            with svc.lock:
                for delta, tid, why in svc.chat(body):
                    if gone:
                        break
                    reason = why or reason
                    svc.tokens += 1
                    if not delta:
                        continue
                    try:
                        send({"id": rid, "object": "chat.completion.chunk",
                              "created": created, "model": MODEL_ID,
                              "choices": [{"index": 0,
                                           "delta": {"content": delta},
                                           "finish_reason": None}]})
                    except (BrokenPipeError, ConnectionResetError):
                        gone.append(True)
            if gone:
                return
            try:
                send({"id": rid, "object": "chat.completion.chunk",
                      "created": created, "model": MODEL_ID,
                      "choices": [{"index": 0, "delta": {},
                                   "finish_reason": reason}]})
                self.wfile.write(b"data: [DONE]\n\n")
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass

    return Handler


def build_runtime(a):
    """Construct the backend the command line asked for.

    The two runtimes expose the same methods (reset/step/top1/logits_f32/close
    plus pos and stats), so nothing below this function knows which one it is
    driving.  Thread count is left to the ORT runtime when --threads is omitted:
    it reads QWEN35_THREADS and falls back to DEFAULT_THREADS, so the number
    lives in one place instead of three.
    """
    if a.backend == "ort":
        import qwen35ort
        return qwen35ort.Runtime(a.onnx, a.emb, a.threads)
    return qwen35rt.Runtime(a.ctx, a.emb, a.libdir)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--backend", choices=("ort", "htp"),
                    default=os.environ.get("QWEN35_BACKEND", "ort"),
                    help="ort: int8 ONNX on the CPU (default, accurate); "
                         "htp: the QNN context on the Hexagon DSP")
    ap.add_argument("--onnx", default=os.environ.get(
        "QWEN35_ONNX", "/var/lib/qwen35/qwen08b_i8.onnx"))
    ap.add_argument("--threads", type=int, default=None,
        help="ORT intra-op threads; 4 beats 8 on this 4+4 SoC.  The default is"
             " QWEN35_THREADS from the environment, else 4 (see qwen35ort.py)")
    ap.add_argument("--ctx", default=os.environ.get("QWEN35_CTX",
                                                    "/var/lib/qwen35/kvc.bin"))
    ap.add_argument("--emb", default=os.environ.get("QWEN35_EMB",
                                                    "/var/lib/qwen35/emb08_f16.npy"))
    ap.add_argument("--tok", default=os.path.join(HERE, "tokenizer.json.gz"))
    ap.add_argument("--libdir", default="/usr/lib")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8000)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--selftest-tokens", type=int, default=8)
    a = ap.parse_args()

    os.environ.setdefault("ADSP_LIBRARY_PATH", "/usr/lib/rfsa/adsp:/dsp")
    needed = [a.emb, a.tok] + ([a.onnx] if a.backend == "ort" else [a.ctx])
    for p in needed:
        if not os.path.exists(p):
            sys.exit("missing %s (see README: model artifacts are not packaged)"
                     % p)
    t0 = time.time()
    rt = build_runtime(a)
    tok = qwen35rt.Tokenizer(a.tok, loader=load_json)
    qwen35rt._fill_rev(tok)
    load = time.time() - t0
    svc = Service(rt, tok)
    st = rt.stats
    threads = st.get("threads")
    print("loaded in %.1f s: backend %s%s, %d inputs, %d kv tensors"
          % (load, st.get("backend"),
             " (%d threads)" % threads if threads else "",
             st.get("inputs", 0), len(rt.kv)), flush=True)

    if a.selftest:
        return selftest(svc, rt, a)

    srv = ThreadingHTTPServer((a.host, a.port), make_handler(svc))
    srv.daemon_threads = True
    print("listening on http://%s:%d  model=%s" % (a.host, a.port, MODEL_ID),
          flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        rt.close()
    return 0


def selftest(svc, rt, a):
    """Mechanical check plus a quality report.

    Mechanical: the model loads, a step runs at a sane rate, logits decode into
    a finite distribution and the KV ring does not produce NaNs.  Quality:
    whether the top-1 tokens are stable across a repeat of the same prompt, and
    whether they match reference.json (written from a run that was checked by
    hand).  Exit code reflects only the mechanical part, so a re-quantised
    model still starts the service while the mismatch is reported.
    """
    prompt = [{"role": "user", "content": "Hello"}]
    runs = []
    for _ in range(2):
        rt.reset()
        ids = svc.tok.encode(svc.build_prompt(prompt))
        for t in ids[:-1]:
            rt.step(t)
        nxt = ids[-1]
        toks = []
        for _ in range(a.selftest_tokens):
            rt.step(nxt)
            lg = rt.logits_f32()
            nxt = int(np.argmax(lg))
            toks.append(nxt)
        runs.append(toks)
    # The batched prefill feeds 64 tokens in one call; it has to leave the state
    # the single steps would, or the first answer is garbage and every later one
    # inherits it.  Same prompt, both paths, compare logits: quantising the 64
    # lanes together gives ORT one activation range per MatMul where the decode
    # graph had one per token, so the batched int8 path sits at cosine 0.94
    # against fp32 where the single-token path sits at 0.97-0.99 (the fp32
    # exports agree at 0.999996, so this is the quantiser and not the export).
    # 0.91 is what a normal prompt scores here; below 0.85 the state hand-over
    # is broken, which looks nothing like quantisation noise.
    pre = "absent"
    if getattr(rt, "pf", None) is not None:
        rt.reset()
        for t in ids[:-1]:
            rt.step(t)
        one = rt.logits_f32().copy()
        rt.reset()
        if rt.prefill(ids[:-1]) is None:
            pre = "skipped (prompt too short)"
        else:
            many = rt.logits_f32().copy()
            cos = float(one @ many /
                        (np.linalg.norm(one) * np.linalg.norm(many) + 1e-9))
            pre = "cosine %.4f%s" % (cos, "" if cos > 0.85 else " MISMATCH")
        rt.reset()
    finite = bool(np.isfinite(rt.logits_f32()).all())
    ms = rt.stats.get("mean_ms") or 0.0
    ok = bool(finite and ms > 1.0 and "MISMATCH" not in pre)
    verdict = "not verified"
    ref = os.path.join(HERE, "reference.json")
    if os.path.exists(ref):
        want = json.load(open(ref)).get("tokens")
        verdict = ("pass" if want == runs[0] else
                   "MISMATCH (got %s, want %s)" % (runs[0][:8], want))
    print("selftest: %d steps, %.1f ms/token (%s)"
          % (2 * a.selftest_tokens, ms, rt.stats.get("backend")))
    print("selftest: tokens %s" % runs[0])
    print("repeat: %s" % ("stable" if runs[0] == runs[1] else "unstable"))
    print("prefill: %s" % pre)
    print("quality: %s" % verdict)
    print("mechanical: %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
