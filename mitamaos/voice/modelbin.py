"""model.bin reader/writer and a NumPy copy of the on-device engine.

Pure NumPy (no PyTorch), so a model can be checked and sampled on any PC
exactly the way the Cardputer will run it: int8 weights with one scale per
tensor, int8 key/value cache with one scale per vector, 96 positions, and
the same sampling rules (temperature 0.85, top-k 40, top-p 0.92,
repetition penalty 1.2, no repeated 3-grams, printable bytes only).

File layout (little-endian), matching model_init() in myai_engine.h:
    u32 header[6] = vocab, embd, heads, layers, ctx, tensors
    per tensor:   u32 byte size, then that many int8 values ([out][in] rows)
    then:         one float32 scale per tensor  (real value = int8 * scale)

Usage:
    python modelbin.py model.bin --prompt "Q: who are you?\\nA:"
"""
import argparse
import struct
import sys
import numpy as np

VOCAB, N_EMBD, N_HEAD, N_LAYER, CTX_LEN = 256, 192, 6, 12, 256
HEAD_DIM, FF_DIM, KV_MAX_POS = 32, 768, 96
N_TENSORS = 2 + N_LAYER * 10 + 3

def shapes(embd=N_EMBD, layers=N_LAYER):
    """Tensor shapes in file order for a model of this width and depth."""
    ff = 4 * embd
    s = [(VOCAB, embd), (CTX_LEN, embd)]
    for _ in range(layers):
        s += [(embd,), (embd,), (embd, embd), (HEAD_DIM, embd), (HEAD_DIM, embd),
              (embd, embd), (embd,), (embd,), (ff, embd), (embd, ff)]
    return s + [(embd,), (embd,), (VOCAB, embd)]


SHAPES = shapes()
assert len(SHAPES) == N_TENSORS


def quantize(w):
    """Symmetric per-tensor int8: scale = max|w| / 127."""
    w = np.asarray(w, dtype=np.float32)
    sc = float(np.abs(w).max()) / 127.0
    if sc == 0.0:
        sc = 1e-8
    q = np.clip(np.rint(w / sc), -127, 127).astype(np.int8)
    return q, sc


def write(path, tensors):
    """tensors: list of float arrays in engine order and shape."""
    embd = np.asarray(tensors[0]).shape[1]
    layers = (len(tensors) - 5) // 10
    want = shapes(embd, layers)
    assert len(tensors) == len(want)
    scales = []
    with open(path, "wb") as f:
        f.write(struct.pack("<6I", VOCAB, embd, embd // HEAD_DIM, layers, CTX_LEN, len(want)))
        for w, shape in zip(tensors, want):
            w = np.asarray(w, dtype=np.float32)
            assert w.shape == shape, (w.shape, shape)
            q, sc = quantize(w)
            f.write(struct.pack("<I", q.size))
            f.write(q.tobytes())
            scales.append(sc)
        f.write(np.asarray(scales, dtype="<f4").tobytes())


def read(path):
    """Returns the tensors dequantized to float32, in engine order."""
    with open(path, "rb") as f:
        hdr = struct.unpack("<6I", f.read(24))
        vocab, embd, heads, layers, ctx, n = hdr
        ok = (vocab == VOCAB and embd % HEAD_DIM == 0 and embd <= N_EMBD and heads == embd // HEAD_DIM
              and 1 <= layers <= N_LAYER and ctx == CTX_LEN and n == 2 + 10 * layers + 3)
        if not ok:
            raise ValueError(f"model.bin header {hdr} isn't a model the engine can run")
        want = shapes(embd, layers)
        raw = []
        for shape in want:
            (n,) = struct.unpack("<I", f.read(4))
            if n != int(np.prod(shape)):
                raise ValueError(f"tensor size {n} != {shape}")
            raw.append(np.frombuffer(f.read(n), dtype=np.int8).reshape(shape))
        scales = np.frombuffer(f.read(4 * len(want)), dtype="<f4")
    return [r.astype(np.float32) * s for r, s in zip(raw, scales)]


class Engine:
    """Token-at-a-time forward pass, the same arithmetic as model_forward()."""

    def __init__(self, tensors):
        self.embd = tensors[0].shape[1]
        self.heads = self.embd // HEAD_DIM
        self.n_layer = (len(tensors) - 5) // 10
        t = iter(tensors)
        self.tok, self.pos = next(t), next(t)
        self.layers = [[next(t) for _ in range(10)] for _ in range(self.n_layer)]
        self.lnf_w, self.lnf_b, self.head = next(t), next(t), next(t)
        self.reset()

    def reset(self):
        self.kq = np.zeros((self.n_layer, KV_MAX_POS, 2, HEAD_DIM), np.int8)
        self.ks = np.ones((self.n_layer, KV_MAX_POS, 2), np.float32)

    @staticmethod
    def _ln(x, w, b):
        m = x.mean()
        v = ((x - m) ** 2).mean()
        return (x - m) / np.sqrt(v + 1e-5) * w + b

    @staticmethod
    def _gelu(x):
        return 0.5 * x * (1 + np.tanh(0.7978845608 * (x + 0.044715 * x ** 3)))

    def _kv_write(self, l, slot, kv, vec):
        sc = max(1e-9, float(np.abs(vec).max())) / 127.0
        self.ks[l, slot, kv] = sc
        self.kq[l, slot, kv] = np.clip(np.rint(vec / sc), -127, 127).astype(np.int8)

    def step(self, tok, pos):
        ctx = min(pos + 1, KV_MAX_POS)
        slot = pos % KV_MAX_POS
        x = self.tok[tok] + self.pos[pos % CTX_LEN]
        for l, (l1w, l1b, Q, K, V, P, l2w, l2b, F1, F2) in enumerate(self.layers):
            h = self._ln(x, l1w, l1b)
            q, k, v = Q @ h, K @ h, V @ h
            self._kv_write(l, slot, 0, k)
            self._kv_write(l, slot, 1, v)
            keys = self.kq[l, :ctx, 0].astype(np.float32) * self.ks[l, :ctx, 0, None]
            vals = self.kq[l, :ctx, 1].astype(np.float32) * self.ks[l, :ctx, 1, None]
            att = np.empty(self.embd, np.float32)
            for hd in range(self.heads):
                s = keys @ q[hd * HEAD_DIM:(hd + 1) * HEAD_DIM] / np.sqrt(HEAD_DIM)
                s = np.exp(s - s.max())
                s /= s.sum()
                att[hd * HEAD_DIM:(hd + 1) * HEAD_DIM] = s @ vals
            x = x + P @ att
            x = x + F2 @ self._gelu(F1 @ self._ln(x, l2w, l2b))
        return self.head @ self._ln(x, self.lnf_w, self.lnf_b)


def _printable(t):
    return 32 <= t < 127 or t == 10


def _repeats(recent, cand, n=3):
    if len(recent) < n - 1:
        return False
    tail = recent[-(n - 1):]
    for i in range(len(recent) - (n - 1) + 1):
        if recent[i:i + n - 1] == tail and i + n - 1 < len(recent) and recent[i + n - 1] == cand:
            return True
    return False


def sample(logits, recent, rng, temp=0.85, top_k=40, top_p=0.92, rep_pen=1.2):
    """do_sample() from the engine."""
    t = logits.astype(np.float64).copy()
    for r in recent:
        t[r] = t[r] * rep_pen if t[r] < 0 else t[r] / rep_pen
    t /= max(temp, 1e-6)
    idx = list(np.argsort(-t, kind="stable")[:top_k])
    p = np.exp(t[idx] - t[idx[0]])
    p /= p.sum()
    cum = np.cumsum(p)
    cut = int(np.searchsorted(cum, top_p, side="right")) + 1
    p[cut:] = 0
    p /= p.sum()
    r, acc = rng.random(), 0.0
    for i, tok in enumerate(idx):
        if p[i] <= 0 or not _printable(tok) or _repeats(recent, tok):
            continue
        acc += p[i]
        if r <= acc:
            return int(tok)
    for tok in idx:
        if _printable(tok):
            return int(tok)
    return int(idx[0])


def generate(engine, prompt, rng, max_new=KV_MAX_POS):
    """ai_task(): encode the prompt, then sample until a blank line or byte 0."""
    engine.reset()
    recent, pos, logits = [], 0, None
    for ch in prompt.encode("latin-1", "replace")[:KV_MAX_POS - 1]:
        logits = engine.step(ch, pos)
        recent = (recent + [ch])[-128:]
        pos += 1
    out, newlines = [], 0
    while pos < KV_MAX_POS - 1 and len(out) < max_new:
        tok = sample(logits, recent, rng)
        recent = (recent + [tok])[-128:]
        if tok == 0:
            break
        if tok == 10:
            newlines += 1
            if newlines >= 2:
                break
        else:
            newlines = 0
        out.append(tok)
        logits = engine.step(tok, pos)
        pos += 1
    return bytes(out).decode("latin-1").strip()


def main():
    ap = argparse.ArgumentParser(description="Run model.bin the way the Cardputer does.")
    ap.add_argument("model")
    ap.add_argument("--prompt", action="append", help='e.g. "Q: who are you?\\nA:" (repeatable)')
    ap.add_argument("-n", type=int, default=3, help="samples per prompt")
    ap.add_argument("--seed", type=int, default=0)
    a = ap.parse_args()
    eng = Engine(read(a.model))
    rng = np.random.default_rng(a.seed)
    prompts = [p.encode().decode("unicode_escape") for p in (a.prompt or ["Q: who are you?\\nA:"])]
    for p in prompts:
        print(p.replace("\n", "\\n"))
        for _ in range(a.n):
            print("   ", generate(eng, p, rng))
    return 0


if __name__ == "__main__":
    sys.exit(main())
