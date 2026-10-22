"""Build a *batched prefill* ONNX graph for Qwen3.5 (DeltaNet + gated attention).

The decode graph (export_2b.py) takes one token at a time, so every prompt token
re-reads all 754 MB of int8 weights: prefill runs at the same ~135 ms/token as
decode and a 100-token prompt costs 13.7 s of time-to-first-token.

This graph takes S tokens at once.  All the weight-bound work (the five per-layer
projections, the MLP, the attention scores, the lm_head) is done as one matmul
per layer over the whole block, so the weights are read once per layer instead
of S times.  Only the DeltaNet recurrence keeps a Python loop: its state is
[heads, 128, 128] and each step is a handful of small ops, nothing bandwidth
bound.

Padding: prompts are left-padded to S, which keeps the next-token logits at
position S-1 whatever the real length is.  A `pad` input (1.0 real, 0.0 pad)
makes the block exactly equivalent to a shorter one:

  * the gate is scaled by pad, so a pad step decays the state by exp(0) = 1,
  * beta is scaled by pad, so a pad step writes delta = 0 into the state,
  * the qkv written into the conv history is zeroed, so a pad step leaves the
    three-token conv window untouched (and a zero window is what a fresh
    sequence starts with anyway),
  * attention relies on the caller's additive mask, which masks pad keys.

rope_cos / rope_sin / attn_mask are inputs, so the caller supplies the *true*
position of every real token; left padding does not shift the rotation.
"""
import argparse
from pathlib import Path

import torch
import torch.nn as nn
import torch.nn.functional as F


def rotate_half(x):
    x1, x2 = x[..., : x.shape[-1] // 2], x[..., x.shape[-1] // 2 :]
    return torch.cat((-x2, x1), dim=-1)


def rms_zero_centered(x, w, eps):
    xf = x.float()
    out = xf * torch.rsqrt(xf.pow(2).mean(-1, keepdim=True) + eps)
    return (out * (1.0 + w.float())).to(x.dtype)


def rms_gated(x, z, w, eps):
    xf = x.float()
    out = xf * torch.rsqrt(xf.pow(2).mean(-1, keepdim=True) + eps)
    out = out * w.float()
    return (out * F.silu(z.float())).to(x.dtype)


def l2norm(x, eps=1e-6):
    return x * torch.rsqrt(x.pow(2).sum(-1, keepdim=True) + eps)


class PrefillGraph(nn.Module):
    def __init__(self, layers, final_norm, lm_head, cfg, seq):
        super().__init__()
        self.layers = layers
        self.final_norm = final_norm
        self.lm_head = lm_head
        self.types = list(cfg.layer_types)
        self.eps = cfg.rms_norm_eps
        self.n_kv = cfg.num_key_value_heads
        self.head_dim = cfg.head_dim
        self.groups = cfg.num_attention_heads // cfg.num_key_value_heads
        self.n_v = cfg.linear_num_value_heads
        self.n_k = cfg.linear_num_key_heads
        self.k_dim = cfg.linear_key_head_dim
        self.v_dim = cfg.linear_value_head_dim
        self.conv_dim = self.k_dim * 2 * self.n_k + self.v_dim * self.n_v
        self.seq = seq
        self.kernel = 4  # linear_conv_kernel_dim

    def state_spec(self, ctx):
        spec = []
        for i, t in enumerate(self.types):
            if t == "linear_attention":
                spec.append((f"layer_{i:02d}_conv_state_in", [1, self.conv_dim, self.kernel - 1]))
                spec.append((f"layer_{i:02d}_rec_state_in", [1, self.n_v, self.k_dim, self.v_dim]))
            else:
                spec.append((f"layer_{i:02d}_past_k_in", [1, self.n_kv, ctx - 1, self.head_dim]))
                spec.append((f"layer_{i:02d}_past_v_in", [1, self.n_kv, ctx - 1, self.head_dim]))
        return spec

    def forward(self, hidden_in, rope_cos, rope_sin, attn_mask, pad, *states):
        S = self.seq
        h = hidden_in
        outs = []
        idx = 0
        for i, t in enumerate(self.types):
            layer = self.layers[i]
            res = h
            x = rms_zero_centered(h, layer.input_layernorm.weight, self.eps)
            if t == "linear_attention":
                conv_state = states[idx]; idx += 1
                rec = states[idx]; idx += 1
                m = layer.linear_attn

                # --- weight-bound work: one matmul per projection, S tokens wide
                qkv = m.in_proj_qkv(x)                       # [1,S,conv_dim]
                z = m.in_proj_z(x).reshape(1, S, self.n_v, self.v_dim)
                b = m.in_proj_b(x)                           # [1,S,n_v]
                a = m.in_proj_a(x)
                w = m.conv1d.weight.squeeze(1)               # [C,kernel]
                # a pad step must not enter the three-token conv window
                hist = torch.cat([conv_state.transpose(1, 2),
                                  qkv * pad], dim=1)         # [1,S+3,C]
                conv = sum(w[:, j] * hist[:, j:j + S, :] for j in range(self.kernel))
                conv = F.silu(conv)                          # [1,S,C]
                new_conv = hist[:, S:S + self.kernel - 1, :].transpose(1, 2)

                q, k, v = torch.split(
                    conv, [self.k_dim * self.n_k, self.k_dim * self.n_k,
                           self.v_dim * self.n_v], dim=-1)
                q = q.reshape(1, S, self.n_k, self.k_dim)
                k = k.reshape(1, S, self.n_k, self.k_dim)
                v = v.reshape(1, S, self.n_v, self.v_dim)
                beta = torch.sigmoid(b) * pad                        # [1,S,n_v]
                g = (-m.A_log.float().exp()
                     * F.softplus(a.float() + m.dt_bias.float())) * pad
                rep = self.n_v // self.n_k
                if rep > 1:
                    q = q.repeat_interleave(rep, dim=2)
                    k = k.repeat_interleave(rep, dim=2)
                qf = l2norm(q.float()) * (self.k_dim ** -0.5)  # [1,S,H,K]
                kf = l2norm(k.float())
                vf = v.float()

                # --- the only sequential part: the gated delta rule
                core = []
                for s in range(S):
                    st = rec.float() * g[:, s][:, :, None, None].exp()
                    mem = (kf[:, s].unsqueeze(-2) @ st).squeeze(-2)   # [1,H,V]
                    delta = (vf[:, s] - mem) * beta[:, s][:, :, None]
                    rec = st + kf[:, s].unsqueeze(-1) * delta.unsqueeze(-2)
                    core.append((qf[:, s].unsqueeze(-2) @ rec).squeeze(-2))
                core = torch.stack(core, dim=1)              # [1,S,H,V]

                core = rms_gated(core, z, m.norm.weight, self.eps)
                attn_out = m.out_proj(core.to(x.dtype).reshape(1, S, -1))
                outs += [new_conv, rec]
            else:
                pk = states[idx]; pv = states[idx + 1]; idx += 2
                a_m = layer.self_attn
                hd = self.head_dim
                qg = a_m.q_proj(x).reshape(1, S, -1, hd * 2)
                q, gate = torch.chunk(qg, 2, dim=-1)
                q = rms_zero_centered(q, a_m.q_norm.weight, self.eps)
                q = q.transpose(1, 2)                        # [1,H,S,hd]
                k = rms_zero_centered(a_m.k_proj(x).reshape(1, S, -1, hd),
                                      a_m.k_norm.weight, self.eps).transpose(1, 2)
                v = a_m.v_proj(x).reshape(1, S, self.n_kv, hd).transpose(1, 2)
                rot = rope_cos.shape[-1]
                qr, qp = q[..., :rot], q[..., rot:]
                kr, kp = k[..., :rot], k[..., rot:]
                cos = rope_cos.transpose(1, 2)               # [1,1,S,rot]
                sin = rope_sin.transpose(1, 2)
                q = torch.cat([qr * cos + rotate_half(qr) * sin, qp], dim=-1)
                k = torch.cat([kr * cos + rotate_half(kr) * sin, kp], dim=-1)
                k_all = torch.cat([pk, k], dim=2)            # [1,n_kv,ctx-1+S,hd]
                v_all = torch.cat([pv, v], dim=2)
                ke = k_all.repeat_interleave(self.groups, dim=1)
                ve = v_all.repeat_interleave(self.groups, dim=1)
                scores = (q @ ke.transpose(2, 3)) * (hd ** -0.5) + attn_mask
                probs = F.softmax(scores.float(), dim=-1).to(q.dtype)
                o = (probs @ ve).transpose(1, 2).reshape(1, S, -1)
                attn_out = a_m.o_proj(o * torch.sigmoid(gate.reshape(1, S, -1)))
                outs += [k_all[:, :, S:], v_all[:, :, S:]]
            h = res + attn_out
            res = h
            x = rms_zero_centered(h, layer.post_attention_layernorm.weight, self.eps)
            mlp = layer.mlp
            h = res + mlp.down_proj(F.silu(mlp.gate_proj(x)) * mlp.up_proj(x))
        h = rms_zero_centered(h[:, S - 1:S], self.final_norm.weight, self.eps)
        return (self.lm_head(h), *outs)


def load_parts(model_dir, dtype=torch.float32):
    from transformers import Qwen3_5ForConditionalGeneration
    m = Qwen3_5ForConditionalGeneration.from_pretrained(model_dir, dtype=dtype)
    m.eval()
    inner = m.model.language_model
    return m, inner.layers, inner.norm, m.lm_head, m.config.text_config


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--ctx", type=int, default=2048)
    ap.add_argument("--seq", type=int, default=64)
    ap.add_argument("--fp16", action="store_true")
    args = ap.parse_args()

    m, layers, norm, head, cfg = load_parts(
        str(args.model), torch.float16 if args.fp16 else torch.float32)
    g = PrefillGraph(layers, norm, head, cfg, args.seq).eval()
    spec = g.state_spec(args.ctx)
    H = cfg.hidden_size
    rot = int(cfg.head_dim * cfg.rope_parameters["partial_rotary_factor"])
    S = args.seq
    names = ["hidden_in", "rope_cos", "rope_sin", "attn_mask", "pad"] + [n for n, _ in spec]
    shapes = ([[1, S, H], [1, S, 1, rot], [1, S, 1, rot], [1, 1, S, args.ctx - 1 + S],
               [1, S, 1]] + [s for _, s in spec])
    dt = torch.float16 if args.fp16 else torch.float32
    inputs = tuple(torch.zeros(s, dtype=dt) for s in shapes)
    inputs[4].fill_(1.0)
    with torch.no_grad():
        out = g(*inputs)
    print(f"graph: S={S}, {len(names)} inputs, {len(out)} outputs")
    print("  logits", tuple(out[0].shape), "state0", tuple(out[1].shape))
    torch.onnx.export(g, inputs, str(args.out), input_names=names,
                      output_names=["logits"] + [n.replace("_in", "_out") for n, _ in spec],
                      opset_version=17, do_constant_folding=True, dynamo=False)
    print("wrote", args.out, f"{args.out.stat().st_size / 1048576:.1f} MB")


if __name__ == "__main__":
    main()
