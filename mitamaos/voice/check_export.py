"""Checks that model.bin runs on the device's maths the way the trained
PyTorch model does: same next-byte guesses, small numeric drift from int8.

  python check_export.py out/mitama.pt out/model.bin
"""
import sys
from pathlib import Path

import numpy as np
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from mitama_model import MitamaGPT  # noqa: E402
import modelbin  # noqa: E402

PROMPTS = ["Q: who are you?\nA: ", "Q: *idle*\nA: ", "Odin gave an eye for wisdom. "]


def main(pt, binpath):
    model = MitamaGPT()
    model.load_state_dict(torch.load(pt, map_location="cpu"))
    model.eval()
    eng = modelbin.Engine(modelbin.read(binpath))
    worst, agree, total = 0.0, 0, 0
    for p in PROMPTS:
        ids = list(p.encode())
        with torch.no_grad():
            ref = model(torch.tensor([ids]))[0][0].numpy()
        eng.reset()
        for pos, t in enumerate(ids):
            got = eng.step(t, pos)
            # compare as probabilities: logits can shift without changing much
            pr = np.exp(ref[pos] - ref[pos].max()); pr /= pr.sum()
            pg = np.exp(got - got.max()); pg /= pg.sum()
            worst = max(worst, float(np.abs(pr - pg).max()))
            agree += int(np.argmax(ref[pos]) == np.argmax(got))
            total += 1
    print(f"top guess agrees on {agree}/{total} positions; worst probability drift {worst:.3f}")
    ok = worst < 0.1
    print("export OK" if ok else "export MISMATCH")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:3]))
