"""Trains the Mitama's model, then writes model.bin for the Cardputer.

Two phases:
  pretrain   reads data/pretrain.txt (myth, SRD, code, the voice) as
             96-byte windows, so the model learns English and the lore
  finetune   teaches the voice: each "Q: ...\\nA: ...\\n\\n" pair from
             data/voice.txt starts at position 0, exactly as the device
             runs it, and only the answer is scored; some pretrain text
             stays in the mix so the lore isn't forgotten

Typical run on a Colab T4 (about an hour all told):
  python build_corpus.py
  python train.py                      # both phases, writes out/model.bin
Quick check that everything runs (CPU, a minute):
  python train.py --pretrain-steps 30 --finetune-steps 20 --batch 8
"""
import argparse
import math
import random
import sys
import time
from pathlib import Path

import numpy as np
import torch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from mitama_model import MitamaGPT, DEVICE_CTX  # noqa: E402
import modelbin  # noqa: E402

BLOCK = DEVICE_CTX  # 96: the device never attends further back than this


def load_pretrain(path):
    data = np.frombuffer(Path(path).read_bytes(), dtype=np.uint8)
    starts = np.flatnonzero((data[:-2] == 10) & (data[1:-1] == 10)) + 2   # after a blank line
    starts = starts[starts < len(data) - BLOCK - 1]
    return data, starts


def load_pairs(path):
    pairs = []
    for blk in Path(path).read_text("ascii").split("\n\n"):
        if not blk.strip():
            continue
        b = (blk.strip("\n") + "\n\n").encode("ascii")
        if len(b) > BLOCK - 1:
            continue
        a = b.find(b"\nA:")
        if a < 0:
            continue
        pairs.append((b, a + 3))        # score from just after "A:"
    return pairs


def pretrain_batch(data, starts, n, rng):
    x = np.empty((n, BLOCK), np.int64)
    y = np.empty((n, BLOCK), np.int64)
    for i in range(n):
        # half the windows begin at a paragraph, as device prompts begin at 0
        s = int(rng.choice(starts)) if rng.random() < 0.5 else rng.integers(0, len(data) - BLOCK - 1)
        w = data[s:s + BLOCK + 1]
        x[i], y[i] = w[:-1], w[1:]
    return torch.from_numpy(x), torch.from_numpy(y), None


def voice_batch(pairs, n, rng):
    x = np.zeros((n, BLOCK), np.int64)
    y = np.zeros((n, BLOCK), np.int64)
    m = np.zeros((n, BLOCK), np.bool_)
    for i in range(n):
        b, a = pairs[rng.integers(len(pairs))]
        seq = np.frombuffer(b, np.uint8)
        L = len(seq) - 1
        x[i, :L], y[i, :L] = seq[:-1], seq[1:]
        m[i, a - 1:L] = True             # targets from the answer's first byte to the end
    return torch.from_numpy(x), torch.from_numpy(y), torch.from_numpy(m)


def lr_at(step, total, peak, warmup):
    if step < warmup:
        return peak * (step + 1) / warmup
    p = (step - warmup) / max(1, total - warmup)
    return peak * (0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * p)))


@torch.no_grad()
def preview(model, dev, prompts, rng, n_new=70):
    model.eval()
    for p in prompts:
        ids = list(p.encode())
        for _ in range(n_new):
            if len(ids) >= BLOCK:
                break
            logits, _ = model(torch.tensor([ids], device=dev))
            pr = torch.softmax(logits[0, -1].float() / 0.85, -1).cpu().numpy().astype(np.float64)
            pr[[i for i in range(256) if not (32 <= i < 127 or i == 10)]] = 0
            tok = int(rng.choice(256, p=pr / pr.sum()))
            ids.append(tok)
            if ids[-2:] == [10, 10]:
                break
        out = bytes(ids[len(p):]).decode("latin-1").strip().replace("\n", " / ")
        print(f"   {p.strip()!r:>24} -> {out}")
    model.train()


def run_phase(name, model, opt, dev, steps, peak_lr, batch_fn, args, rng, prompts):
    if steps <= 0:
        return
    print(f"== {name}: {steps} steps")
    use_amp = dev.type == "cuda"
    amp_dtype = torch.bfloat16 if use_amp and torch.cuda.is_bf16_supported() else torch.float16
    scaler = torch.amp.GradScaler("cuda", enabled=use_amp and amp_dtype == torch.float16)
    t0, avg = time.time(), None
    for step in range(steps):
        for g in opt.param_groups:
            g["lr"] = lr_at(step, steps, peak_lr, min(200, steps // 10 + 1))
        x, y, m = batch_fn()
        x, y = x.to(dev), y.to(dev)
        m = m.to(dev) if m is not None else None
        with torch.autocast(dev.type, dtype=amp_dtype, enabled=use_amp):
            _, loss = model(x, y, m)
        opt.zero_grad(set_to_none=True)
        scaler.scale(loss).backward()
        scaler.unscale_(opt)
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        scaler.step(opt)
        scaler.update()
        avg = loss.item() if avg is None else 0.98 * avg + 0.02 * loss.item()
        if step % args.log_every == 0 or step == steps - 1:
            print(f"   step {step:6d}  loss {avg:.3f}  {time.time() - t0:6.0f}s", flush=True)
        if args.preview_every and step and step % args.preview_every == 0:
            preview(model, dev, prompts, rng)
    preview(model, dev, prompts, rng)


def import_bin(model, path):
    """Starts from an existing model.bin (int8, so a little lossy)."""
    with torch.no_grad():
        for (_, p), w in zip(model.tensors(), modelbin.read(path)):
            p.copy_(torch.from_numpy(w))


def export_bin(model, path):
    modelbin.write(path, [t.detach().float().cpu().numpy() for _, t in model.tensors()])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", default=str(HERE / "data"))
    ap.add_argument("--out", default=str(HERE / "out"))
    ap.add_argument("--init", help="start from this model.bin instead of random weights")
    ap.add_argument("--resume", help="start from a checkpoint (.pt) written by this script")
    ap.add_argument("--pretrain-steps", type=int, default=20000)
    ap.add_argument("--finetune-steps", type=int, default=3000)
    ap.add_argument("--batch", type=int, default=64)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--finetune-lr", type=float, default=2e-4)
    ap.add_argument("--mix", type=float, default=0.25, help="share of pretrain text during finetune")
    ap.add_argument("--log-every", type=int, default=100)
    ap.add_argument("--preview-every", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=1337)
    a = ap.parse_args()

    torch.manual_seed(a.seed)
    rng = np.random.default_rng(a.seed)
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    data, starts = load_pretrain(Path(a.data) / "pretrain.txt")
    pairs = load_pairs(Path(a.data) / "voice.txt")
    print(f"device {dev}; pretrain {len(data) / 1e6:.2f} MB; {len(pairs)} voice pairs")

    model = MitamaGPT().to(dev)
    if a.init:
        import_bin(model, a.init)
        print(f"started from {a.init}")
    if a.resume:
        model.load_state_dict(torch.load(a.resume, map_location=dev))
        print(f"resumed from {a.resume}")
    print(f"{sum(p.numel() for p in model.parameters()) / 1e6:.2f}M parameters")

    # no weight decay on LayerNorm, as usual; decay keeps int8 outliers down
    decay = [p for n, p in model.named_parameters() if p.dim() >= 2]
    other = [p for n, p in model.named_parameters() if p.dim() < 2]
    groups = [{"params": decay, "weight_decay": 0.1}, {"params": other, "weight_decay": 0.0}]
    fused = dev.type == "cuda"
    prompts = ["Q: who are you?\nA:", "Q: what is a kitsune?\nA:", "Q: *idle*\nA:", "Q: *seen AIRTAG*\nA:"]

    opt = torch.optim.AdamW(groups, lr=a.lr, betas=(0.9, 0.95), fused=fused)
    run_phase("pretrain", model, opt, dev, a.pretrain_steps, a.lr,
              lambda: pretrain_batch(data, starts, a.batch, rng), a, rng, prompts)
    if a.pretrain_steps > 0:
        torch.save(model.state_dict(), out / "pretrain.pt")

    def mixed():
        if rng.random() < a.mix:
            return pretrain_batch(data, starts, a.batch, rng)
        return voice_batch(pairs, a.batch, rng)

    opt = torch.optim.AdamW(groups, lr=a.finetune_lr, betas=(0.9, 0.95), fused=fused)
    run_phase("finetune", model, opt, dev, a.finetune_steps, a.finetune_lr, mixed, a, rng, prompts)

    torch.save(model.state_dict(), out / "mitama.pt")
    export_bin(model, out / "model.bin")
    print(f"wrote {out / 'model.bin'} - copy it to the SD card as /MitamaOS/model.bin")
    return 0


if __name__ == "__main__":
    sys.exit(main())
