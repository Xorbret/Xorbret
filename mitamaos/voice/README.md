# Retraining the Mitama's voice

This folder retrains the on-device language model (the one MiTalk, formerly myAI, runs from
`/MitamaOS/model.bin`) from scratch, so it talks like the Mitama: a demon
that climbed out of a game, sassy but competent, with small references to
myth, tabletop and JRPG conventions, and code.

The model keeps exactly the shape the firmware's engine expects (byte-level,
12 layers, 192 wide, about 4.7 million weights stored as int8), so the new
`model.bin` drops onto the SD card with no firmware change.

## Easiest way: Google Colab

1. Open `train_mitama.ipynb` in Colab (File > Upload notebook, or open it
   from GitHub in Colab).
2. Runtime > Change runtime type > T4 GPU.
3. Runtime > Run all. It takes about an hour, and prints sample answers as
   it goes.
4. The last cell downloads `model.bin`. Back up the old one, then copy the
   new one to the SD card as `/MitamaOS/model.bin`.

## On your own machine

Needs Python 3.10+, `numpy` and `torch` (a GPU helps a lot).

    python build_corpus.py          # downloads sources, writes data/
    python train.py                 # writes out/model.bin
    python check_export.py out/mitama.pt out/model.bin
    python modelbin.py out/model.bin --prompt "Q: who are you?\nA:"

## Changing what it says

- `mitama_voice.txt` is the voice. Add `Q:` lines with one or more `A:`
  answers. Each pair must fit in 95 characters (the device's limit);
  `python make_voice.py` lists any that are too long.
- The firmware's line bank (`firmware/src/mitama/lines.h`) is folded in
  automatically as event prompts such as `*idle*` and `*seen AIRTAG*`.
- To retune only the voice, keep the pretrained weights:
  `python train.py --resume out/pretrain.pt --pretrain-steps 0`

## Files

| File | What it does |
| --- | --- |
| `build_corpus.py` | downloads and cleans the training text |
| `make_voice.py` | turns the voice file, line bank and SRD into Q/A pairs |
| `mitama_model.py` | the model in PyTorch, shaped like the device engine |
| `train.py` | pretrain then finetune, writes `model.bin` |
| `modelbin.py` | reads/writes `model.bin` and runs it with the device's maths |
| `check_export.py` | confirms the exported file behaves like the trained model |

Sources and licences are listed in `firmware/CREDITS.md`.
