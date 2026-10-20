"""The Mitama's language model, in PyTorch, shaped exactly like the
on-device engine (firmware/src/apps/myai/myai_engine.h) so a trained model
can be exported straight to /MitamaOS/model.bin.

Byte-level GPT: 256 tokens (one per byte), 192 wide, 12 layers, 6 query
heads sharing one key/value head (multi-query attention), no biases in the
linear layers, LayerNorm eps 1e-5, tanh GELU, learned positions (256 rows,
of which the device only ever uses the first 96).
"""
import math
import torch
import torch.nn as nn
import torch.nn.functional as F

VOCAB = 256
N_EMBD = 192
N_LAYER = 12
N_HEAD = 6
HEAD_DIM = 32
CTX_LEN = 256      # rows in the position table
DEVICE_CTX = 96    # KV_MAX_POS: the device never sees more than this
FF_DIM = 4 * N_EMBD


class Block(nn.Module):
    def __init__(self):
        super().__init__()
        self.ln1 = nn.LayerNorm(N_EMBD, eps=1e-5)
        self.q = nn.Linear(N_EMBD, N_EMBD, bias=False)
        self.k = nn.Linear(N_EMBD, HEAD_DIM, bias=False)
        self.v = nn.Linear(N_EMBD, HEAD_DIM, bias=False)
        self.proj = nn.Linear(N_EMBD, N_EMBD, bias=False)
        self.ln2 = nn.LayerNorm(N_EMBD, eps=1e-5)
        self.ff1 = nn.Linear(N_EMBD, FF_DIM, bias=False)
        self.ff2 = nn.Linear(FF_DIM, N_EMBD, bias=False)

    def forward(self, x):
        B, T, _ = x.shape
        h = self.ln1(x)
        q = self.q(h).view(B, T, N_HEAD, HEAD_DIM).transpose(1, 2)    # B,H,T,D
        k = self.k(h).unsqueeze(1).expand(B, N_HEAD, T, HEAD_DIM)     # shared
        v = self.v(h).unsqueeze(1).expand(B, N_HEAD, T, HEAD_DIM)
        a = F.scaled_dot_product_attention(q, k, v, is_causal=True)   # scale 1/sqrt(32)
        a = a.transpose(1, 2).reshape(B, T, N_EMBD)
        x = x + self.proj(a)
        x = x + self.ff2(F.gelu(self.ff1(self.ln2(x)), approximate="tanh"))
        return x


class MitamaGPT(nn.Module):
    def __init__(self):
        super().__init__()
        self.tok_emb = nn.Embedding(VOCAB, N_EMBD)
        self.pos_emb = nn.Embedding(CTX_LEN, N_EMBD)
        self.blocks = nn.ModuleList(Block() for _ in range(N_LAYER))
        self.ln_f = nn.LayerNorm(N_EMBD, eps=1e-5)
        self.head = nn.Linear(N_EMBD, VOCAB, bias=False)
        self.apply(self._init)
        # GPT-2 style: shrink the residual projections with depth
        for b in self.blocks:
            nn.init.normal_(b.proj.weight, std=0.02 / math.sqrt(2 * N_LAYER))
            nn.init.normal_(b.ff2.weight, std=0.02 / math.sqrt(2 * N_LAYER))

    @staticmethod
    def _init(m):
        if isinstance(m, (nn.Linear, nn.Embedding)):
            nn.init.normal_(m.weight, std=0.02)

    def forward(self, idx, targets=None, mask=None):
        B, T = idx.shape
        assert T <= CTX_LEN
        pos = torch.arange(T, device=idx.device)
        x = self.tok_emb(idx) + self.pos_emb(pos)
        for b in self.blocks:
            x = b(x)
        logits = self.head(self.ln_f(x))
        if targets is None:
            return logits, None
        loss = F.cross_entropy(logits.view(-1, VOCAB), targets.view(-1), reduction="none")
        if mask is not None:
            m = mask.view(-1).float()
            loss = (loss * m).sum() / m.sum().clamp(min=1)
        else:
            loss = loss.mean()
        return logits, loss

    def tensors(self):
        """(name, tensor) in the exact order model.bin stores them."""
        out = [("tok_emb", self.tok_emb.weight), ("pos_emb", self.pos_emb.weight)]
        for i, b in enumerate(self.blocks):
            p = f"l{i}."
            out += [(p + "ln1.w", b.ln1.weight), (p + "ln1.b", b.ln1.bias),
                    (p + "q", b.q.weight), (p + "k", b.k.weight), (p + "v", b.v.weight),
                    (p + "proj", b.proj.weight),
                    (p + "ln2.w", b.ln2.weight), (p + "ln2.b", b.ln2.bias),
                    (p + "ff1", b.ff1.weight), (p + "ff2", b.ff2.weight)]
        out += [("ln_f.w", self.ln_f.weight), ("ln_f.b", self.ln_f.bias), ("head", self.head.weight)]
        return out
