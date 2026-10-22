#!/usr/bin/env python3
"""qwen35-rt.py -- resident decode loop for the Qwen3.5-0.8B HTP context.

Everything runs in one process on the board:

  * the context is loaded once (libqnnres.so); recurrent state and the KV ring
    buffer live in the graph's own input buffers, so a step is one
    graphExecute plus a few kilobytes of writes;
  * the token embedding is read straight from the exported fp16 table (mmap,
    2 KB per token);
  * the KV cache is a ring: the new key/value slice goes into the slot for the
    current position instead of rebuilding the whole 2 MB tensor every token,
    and the attention mask follows the ring.

Through qnn-net-run the same graph costs 1337 ms/step, because that tool
reloads the 764 MB context, converts every tensor through float32 and writes
every output to a file.  Here it is one execute: ~135 ms/token measured.

usage:
  qwen35-rt.py --ctx kvc.bin --emb emb08_f16.npy --prompt "Hello" --steps 32
  qwen35-rt.py --ctx kvc.bin --emb emb08_f16.npy --tokens 760,6511,314 --steps 8

The QNN calls live in qnnlib.py (ctypes); this file is the model loop.
"""
import argparse
import codecs
import gzip
import json
import os
import re
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
CTX_LEN = 2048
EOS = 248046          # <|im_end|>
IM_START = 248045


# --------------------------------------------------------------------- engine
# qnnlib is imported inside Runtime: the ONNX/ORT backend reuses this model loop
# and needs no QNN shared object at all.
sys.path.insert(0, HERE)


# ----------------------------------------------------------------- tokenizer
def _byte_map():
    # The reference (GPT-2 bytes_to_unicode) keeps "!".."~", "\u00a1".."\u00ac" and
    # "\u00ae".."\u00ff" literal and shifts the rest to 256+n.  The middle range
    # is inclusive of \u00ac (0xAC): dropping it (0xA1..0xAB) shifts 0xAC and
    # 0xAD by one, and 0xAD is the most common UTF-8 continuation byte of CJK
    # text, so every character whose last byte is 0xAC or 0xAD -- 语, 记, 该,
    # 就 ... -- was silently mapped onto its predecessor (语 -> 诬) on decode,
    # and encoded as token 0 ("!") on encode.
    bs = (list(range(ord("!"), ord("~") + 1)) + list(range(0xA1, 0xAD)) +
          list(range(0xAE, 0xFF + 1)))
    cs = bs[:]
    n = 0
    for b in range(256):
        if b not in bs:
            bs.append(b)
            cs.append(256 + n)
            n += 1
    return dict(zip(bs, [chr(c) for c in cs]))


class Tokenizer(object):
    """Minimal byte-level BPE, enough for the Qwen chat template.

    The reference implementation splits with Unicode property classes
    (\\p{L}, \\p{N}, \\p{M}); Python's re has no \\p, so letters are written as
    [^\\W\\d_] (Unicode letters, no digits or underscore) which matches for
    Latin/CJK and keeps the "one digit at a time" behaviour that matters for
    numbers.
    """

    # Mirrors the reference Spl pattern with Python's re (no \p{...}): letters
    # are [^\W\d_], and "neither letter nor digit nor space" is [^\s\w] plus
    # the underscore that \w folds in.
    #
    # The optional prefix that keeps a leading space attached to its word is
    # [^\r\n\p{L}\p{N}] in the reference. It cannot be written as
    # [^\r\n\W\d_] here: a space *is* a \W character, so that class excludes
    # exactly the character it is there to allow, the splitter then emits " "
    # as its own chunk, and every word loses its "\u0120" marker -- which turns
    # the prompt into gibberish. [^\r\n\w] is the faithful translation (it
    # also excludes "_", which the reference would allow; harmless).
    SPLIT = re.compile(
        r"(?i:'s|'t|'re|'ve|'m|'ll|'d)"
        r"|[^\r\n\w]?[^\W\d_]+"
        r"|\d"
        r"| ?(?:[^\s\w]|_)+[\r\n]*"
        r"|\s*[\r\n]+"
        r"|\s+(?!\S)"
        r"|\s+")

    def __init__(self, path, loader=None):
        if loader is not None:
            d = loader(path)
        elif path.endswith(".gz"):
            with gzip.open(path, "rt") as f:
                d = json.load(f)
        else:
            with open(path) as f:
                d = json.load(f)
        self.vocab = d["model"]["vocab"]
        self.rank = {tuple(m.split(" ")): i for i, m in enumerate(d["model"]["merges"])}
        self.b2u = _byte_map()
        self.u2b = {v: k for k, v in self.b2u.items()}
        self.added = {}
        for t in d.get("added_tokens", []):
            self.added[t["id"]] = t["content"]
        # special tokens are matched before the split regex, otherwise
        # "<|im_start|>" would be encoded as six ordinary pieces
        self._added_by_text = {v: k for k, v in self.added.items()}
        if self._added_by_text:
            pat = "|".join(re.escape(t) for t in
                           sorted(self._added_by_text, key=len, reverse=True))
            # captured, so re.split keeps the special tokens in the result
            self._added_re = re.compile("(" + pat + ")")
        else:
            self._added_re = None
        # decode() reads the module-level id->token table; fill it here so a
        # fresh Tokenizer can decode without the caller remembering _fill_rev
        _fill_rev(self)

    def _bpe(self, word):
        parts = list(word)
        while len(parts) > 1:
            best, at = None, -1
            for i in range(len(parts) - 1):
                r = self.rank.get((parts[i], parts[i + 1]))
                if r is not None and (best is None or r < best):
                    best, at = r, i
            if at < 0:
                break
            parts[at:at + 2] = [parts[at] + parts[at + 1]]
        return parts

    def encode(self, text):
        if self._added_re is not None:
            ids = []
            for piece in self._added_re.split(text):
                if piece in self._added_by_text:
                    ids.append(self._added_by_text[piece])
                elif piece:
                    ids.extend(self._encode_plain(piece))
            return ids
        return self._encode_plain(text)

    def _encode_plain(self, text):
        ids = []
        for chunk in self.SPLIT.findall(text):
            word = "".join(self.b2u[b] for b in chunk.encode("utf-8"))
            for piece in self._bpe(word):
                i = self.vocab.get(piece)
                if i is not None:
                    ids.append(i)
                else:
                    # Should not happen for a well-formed vocab; a silent 0 here
                    # turns into a "!" token and corrupts the whole prompt, so
                    # say so once instead of guessing.
                    global _WARNED
                    if not _WARNED:
                        _WARNED = True
                        sys.stderr.write("tokenizer: OOV piece %r\n" % (piece,))
                    ids.extend(self.vocab.get(c, 0) for c in piece)
        return ids

    def decode(self, ids):
        parts, buf = [], []

        def flush():
            if not buf:
                return
            b = bytearray()
            for ch in buf:
                v = self.u2b.get(ch)
                if v is None:
                    b.extend(ch.encode("utf-8"))
                else:
                    b.append(v)
            parts.append(b.decode("utf-8", "replace"))
            del buf[:]

        for i in ids:
            if i in self.added:
                flush()
                parts.append(self.added[i])
            else:
                t = _ID2TOK.get(i)
                if t:
                    buf.extend(t)          # a token may be several characters
        flush()
        return "".join(parts)


_ID2TOK = {}
_WARNED = False


class TokenStream(object):
    """Incremental detokenisation.

    A BPE token can end in the middle of a UTF-8 sequence, so decoding the
    growing prefix every step produces U+FFFD in the middle of a character.
    Feeding the bytes to an incremental decoder is exact and lets a server
    stream deltas without ever splitting a character.
    """

    def __init__(self, tok):
        self.tok = tok
        self.dec = codecs.getincrementaldecoder("utf-8")("replace")

    def push(self, token_id):
        tok = self.tok
        if token_id in tok.added:
            head = self.dec.decode(b"", final=False)
            return head + tok.added[token_id]
        t = _ID2TOK.get(token_id) or ""
        b = bytearray()
        for ch in t:
            v = tok.u2b.get(ch)
            if v is None:
                b.extend(ch.encode("utf-8"))
            else:
                b.append(v)
        return self.dec.decode(bytes(b), final=False)

    def flush(self):
        return self.dec.decode(b"", final=True)


def _fill_rev(tok):
    _ID2TOK.clear()
    for t, i in tok.vocab.items():
        _ID2TOK[i] = t


# ------------------------------------------------------------------- runtime
def cos_sin(pos, theta=10000000.0, rot=64):
    inv = 1.0 / (theta ** (np.arange(0, rot, 2, dtype=np.float64) / rot))
    th = pos * inv
    c = np.cos(th).astype(np.float32)
    s = np.sin(th).astype(np.float32)
    return np.concatenate([c, c]), np.concatenate([s, s])


class Runtime(object):
    def __init__(self, ctx, emb, libdir="/usr/lib", mask_fill=-64.0,
                 layout=None):
        import qnnlib  # noqa: F401  (only the HTP backend links libQnnHtp)
        self.eng = qnnlib.Engine(ctx, libdir,
                                 layout=layout or os.path.join(HERE, "layout.json"))
        self.manifest = json.load(open(os.path.join(HERE, "kv_manifest.json")))
        self.emb = np.load(emb, mmap_mode="r")
        self.mask_fill = mask_fill

        self.hidden = self.eng.view_in("hidden_in")
        self.rc = self.eng.view_in("rope_cos")
        self.rs = self.eng.view_in("rope_sin")
        self.mask = self.eng.view_in("attn_mask")
        self.logits = self.eng.view_out("logits")
        for n, v in (("hidden_in", self.hidden), ("rope_cos", self.rc),
                     ("rope_sin", self.rs), ("attn_mask", self.mask),
                     ("logits", self.logits)):
            if v is None:
                sys.exit("graph has no tensor %s" % n)
        L = self.eng.layout
        self.q = {n: (L[n].get("scale"), L[n].get("offset"))
                  for n in ("hidden_in", "rope_cos", "rope_sin", "attn_mask")}
        self.logit_scale = L["logits"].get("out_scale")
        self.logit_offset = L["logits"].get("out_offset")

        # (past view, new-slice name, kind) for the six attention layers
        self.kv = []
        for layer, kinds in sorted(self.manifest.items()):
            for kind in ("k", "v"):
                s = kinds[kind]
                past = self.eng.view_in(s["past_in"])
                if past is None:
                    sys.exit("graph has no tensor %s" % s["past_in"])
                self.kv.append((past, s["new_out"], kind))
        self.slots = self.kv[0][0].shape[1]
        self.pos = 0

    def _quant(self, name, x):
        sc, of = self.q[name]
        return np.clip(np.round(x / sc - of), 0, 65535).astype(np.uint16)

    def reset(self):
        self.pos = 0
        # Not memset: the input buffers hold quantised codes, so zeroing the
        # bytes makes every state tensor start at -scale*offset, not at 0.0.
        self.eng.zero_inputs()

    def write_mask(self):
        """Attention is order-invariant over keys, so the KV cache is a ring.

        The graph concatenates the new slice at the end of past_k, i.e. the
        newest key always lands at index -1, and the 2047 ring slots hold the
        previous positions.  So the mask has to admit exactly those slots that
        have been written (pos of them, they are contiguous until the ring
        wraps) plus the last lane.
        """
        m = np.full(self.mask.size, self.mask_fill, np.float32)
        m[:min(self.pos, self.slots)] = 0.0
        m[-1] = 0.0
        self.mask[...] = self._quant("attn_mask", m.reshape(self.mask.shape))

    def step(self, token):
        row = np.asarray(self.emb[token], np.float32)
        self.hidden[...] = self._quant("hidden_in", row.reshape(self.hidden.shape))
        c, s = cos_sin(self.pos)
        self.rc[...] = self._quant("rope_cos", c.reshape(self.rc.shape))
        self.rs[...] = self._quant("rope_sin", s.reshape(self.rs.shape))
        self.write_mask()
        ms = self.eng.exec()
        self.eng.carry()
        # append the new k/v slice to the ring
        slot = self.pos % self.slots
        for past, out_name, kind in self.kv:
            x = self.eng.view_out(out_name)
            if x is None:
                sys.exit("graph has no output %s" % out_name)
            if x.ndim == 4 and x.shape[1] != 1:      # [1,2,1,256] -> [1,1,256,2]
                x = np.moveaxis(x, 1, -1)
            past[0, slot] = x.reshape(past.shape[2], past.shape[3])
        self.pos += 1
        return ms

    def logits_f32(self):
        x = self.logits.ravel().astype(np.float64)
        if self.logit_scale is not None:
            x = self.logit_scale * (x + self.logit_offset)
        return x

    def top1(self):
        """Greedy pick without the float32/float64 detour.

        The output is scale*(q + offset) with scale > 0, so the argmax of the
        raw codes is the argmax of the values; converting 248k codes to float64
        every token costs ~3 ms for nothing.
        """
        if self.logit_scale is None or self.logit_scale <= 0:
            return int(np.argmax(self.logits_f32()))
        return int(np.argmax(self.logits.ravel()))

    def close(self):
        self.eng.close()

    @property
    def stats(self):
        """Same keys as qwen35ort.Runtime.stats; qwen35-serve.py reads them."""
        return {"mean_ms": self.eng.mean_ms or 0.0,
                "inputs": len(self.eng.ins),
                "backend": "htp"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ctx", default="/var/lib/qwen35/kvc.bin")
    ap.add_argument("--emb", default="emb08_f16.npy")
    ap.add_argument("--libdir", default="/usr/lib")
    ap.add_argument("--tok", default=os.path.join(HERE, "tokenizer.json"))
    ap.add_argument("--prompt", default=None)
    ap.add_argument("--tokens", default=None)
    ap.add_argument("--steps", type=int, default=32)
    ap.add_argument("--topk", type=int, default=5)
    ap.add_argument("--greedy", action="store_true", default=True)
    ap.add_argument("--think", action="store_true")
    ap.add_argument("--raw", action="store_true", help="print top-k ids per step")
    a = ap.parse_args()

    os.environ.setdefault("ADSP_LIBRARY_PATH", "/usr/lib/rfsa/adsp:/dsp")
    rt = Runtime(a.ctx, a.emb, a.libdir)
    print("context loaded, %d kv tensors x %d slots" % (len(rt.kv), rt.slots))

    tok = None
    if a.prompt is not None:
        tok = Tokenizer(a.tok)
        _fill_rev(tok)
        body = "<|im_start|>user\n%s<|im_end|>\n<|im_start|>assistant\n" % a.prompt
        body += "<think>\n" if a.think else "<think>\n\n</think>\n\n"
        ids = tok.encode(body)
        print("prompt: %d tokens" % len(ids))
    elif a.tokens:
        ids = [int(x) for x in a.tokens.split(",")]
    else:
        ids = [760, 6511, 314, 9338, 369]

    t0 = time.time()
    for i, t in enumerate(ids[:-1]):        # prime, no sampling
        rt.step(t)
    prefill_ms = (time.time() - t0) * 1000

    produced = []
    graph_ms = []
    nxt = ids[-1]
    t0 = time.time()
    for i in range(a.steps):
        ms = rt.step(nxt)
        graph_ms.append(ms)
        lg = rt.logits_f32()
        top = np.argsort(-lg)[:a.topk]
        nxt = int(top[0])
        produced.append(nxt)
        if a.raw:
            print("step %2d %6.1f ms pos %4d top1=%-6d %s" %
                  (i, ms, rt.pos - 1, top[0],
                   [(int(x), round(float(lg[x]), 2)) for x in top]))
        if nxt == EOS:
            break
    total = (time.time() - t0) * 1000
    n = max(len(produced), 1)
    print("\nprefill %d tokens: %.0f ms (%.0f ms/token)" %
          (len(ids) - 1, prefill_ms, prefill_ms / max(len(ids) - 1, 1)))
    print("decode %d tokens: %.0f ms wall, %.1f ms/token graph, %.2f tok/s" %
          (n, total, sum(graph_ms) / n, 1000.0 * n / total))
    if tok:
        print("\n--- text ---")
        print(tok.decode(produced))
        print("--- end ---")
    rt.close()


if __name__ == "__main__":
    main()
