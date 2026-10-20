"""Builds the training text for the Mitama's model.

Everything here is public domain or openly licensed (see CREDITS.md):
  - myth and folklore from Project Gutenberg (public domain in the US)
  - the D&D 5e System Reference Document 5.1 (CC-BY-4.0), via the
    5e-bits/5e-database JSON (MIT)
  - code: a few Python standard library modules (PSF License) and
    MitamaOS's own firmware source (GPLv2, this project)
  - the Mitama's own voice: voice/mitama_voice.txt plus the firmware's
    line bank and some generated question/answer pairs (make_voice.py)

Output (in --out, default voice/data/):
  pretrain.txt   plain text, paragraphs separated by blank lines
  voice.txt      "Q: ...\\nA: ...\\n\\n" pairs in the Mitama's voice

Only bytes the Cardputer prints are kept (ASCII 32-126 and newline).
"""
import argparse
import inspect
import json
import os
import re
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIRMWARE = HERE.parent / "firmware" / "src"

# (Gutenberg id, expected title fragment)
GUTENBERG = [
    (4928, "Bulfinch"),                     # Bulfinch's Mythology
    (1210, "Kwaidan"),                      # Lafcadio Hearn
    (4018, "Japanese Fairy Tales"),         # Yei Theodora Ozaki
    (45723, "Myths & Legends of Japan"),    # F. Hadland Davis
    (14726, "Edda"),                        # Elder and Younger Eddas
    (2591, "Grimms' Fairy Tales"),
    (1727, "Odyssey"),                      # Butler
    (2199, "Iliad"),                        # Butler
]
GUTENBERG_URL = "https://www.gutenberg.org/cache/epub/{0}/pg{0}.txt"

# Pinned so the corpus is reproducible.
SRD_COMMIT = "a6212beb4b278917c2cff41c2b04c4ef4f3e0d6f"
SRD_URL = "https://raw.githubusercontent.com/5e-bits/5e-database/" + SRD_COMMIT + "/src/2014/en/5e-SRD-{}.json"
SRD_FILES = ["Monsters", "Spells", "Magic-Items", "Conditions", "Rule-Sections", "Races", "Classes"]

PY_MODULES = ["bisect", "heapq", "textwrap", "fnmatch", "colorsys", "shlex", "statistics",
              "string", "queue", "glob", "calendar", "random", "functools", "base64"]

ASCII_MAP = {"‘": "'", "’": "'", "“": '"', "”": '"', "—": " - ",
             "–": "-", "…": "...", " ": " ", "æ": "ae", "Æ": "Ae",
             "œ": "oe", "ß": "ss", "×": "x"}


def ascii_only(s):
    import unicodedata
    for k, v in ASCII_MAP.items():
        s = s.replace(k, v)
    s = unicodedata.normalize("NFKD", s)
    s = "".join(c for c in s if c == "\n" or 32 <= ord(c) < 127)
    return s


def fetch(url, cache):
    cache.parent.mkdir(parents=True, exist_ok=True)
    if cache.exists() and cache.stat().st_size > 0:
        return cache.read_bytes()
    req = urllib.request.Request(url, headers={"User-Agent": "MitamaOS-corpus/1.0"})
    with urllib.request.urlopen(req, timeout=60) as r:
        data = r.read()
    cache.write_bytes(data)
    return data


def gutenberg_text(raw):
    """Strips the Project Gutenberg header/licence and re-flows paragraphs."""
    t = raw.decode("utf-8", "replace").replace("\r\n", "\n")
    a = re.search(r"\*\*\* ?START OF (THE|THIS) PROJECT GUTENBERG.*?\*\*\*", t)
    b = re.search(r"\*\*\* ?END OF (THE|THIS) PROJECT GUTENBERG", t)
    t = t[a.end() if a else 0: b.start() if b else len(t)]
    paras = []
    for p in re.split(r"\n\s*\n", t):
        p = " ".join(line.strip() for line in p.split("\n")).strip()
        p = re.sub(r"\s+", " ", p)
        p = re.sub(r"\[(Illustration|Footnote)[^\]]*\]", "", p).strip()
        if len(p) < 40 or p.isupper():          # headings, page furniture
            continue
        paras.append(p)
    return paras


def _desc(x):
    d = x.get("desc", "")
    return " ".join(d) if isinstance(d, list) else str(d)


def _ord(n):
    return {1: "1st", 2: "2nd", 3: "3rd"}.get(n, f"{n}th")


def srd_paragraphs(name, items):
    out = []
    for x in items:
        if name == "Monsters":
            ac = x.get("armor_class", [{}])
            ac = ac[0].get("value") if isinstance(ac, list) and ac else ac
            s = (f"{x['name']}. {x['size']} {x['type']}, {x.get('alignment', 'unaligned')}. "
                 f"Armor Class {ac}, {x.get('hit_points')} hit points, challenge {x.get('challenge_rating')}.")
            for key in ("special_abilities", "actions", "legendary_actions"):
                for a in x.get(key, []) or []:
                    s += f" {a['name']}: {_desc(a)}"
            out.append(s)
        elif name == "Spells":
            lvl = x.get("level", 0)
            kind = "cantrip" if lvl == 0 else f"{_ord(lvl)}-level"
            school = x.get("school", {}).get("name", "").lower()
            s = (f"{x['name']}. {school} {kind} spell. Casting time {x.get('casting_time')}, "
                 f"range {x.get('range')}, duration {x.get('duration')}. {_desc(x)}")
            out.append(s if lvl else f"{x['name']}. {school} cantrip. {_desc(x)}")
        elif name == "Rule-Sections":
            for p in re.split(r"\n\s*\n", _desc(x)):
                p = re.sub(r"[#*|]+", " ", p)
                p = re.sub(r"\s+", " ", p).strip()
                if len(p) > 40:
                    out.append(p)
        elif name == "Races":
            out.append(f"{x['name']}. " + " ".join(str(x.get(k, "")) for k in
                       ("alignment", "age", "size_description", "language_desc")))
        elif name == "Classes":
            out.append(f"{x['name']}. Hit die d{x.get('hit_die')}. " +
                       ", ".join(p["name"] for p in x.get("proficiencies", [])) + ".")
        else:
            out.append(f"{x['name']}. {_desc(x)}")
    return [re.sub(r"\s+", " ", p).strip() for p in out if p]


def code_paragraphs(budget=400_000):
    """Some code, capped so it stays a seasoning rather than the meal."""
    out = []
    for m in PY_MODULES:
        try:
            src = inspect.getsource(__import__(m))
        except (OSError, TypeError, ImportError):
            continue
        out += [blk.strip("\n") for blk in re.split(r"\n\s*\n", src) if len(blk.strip()) > 40]
    for path in sorted(FIRMWARE.rglob("*.h"))[:400]:
        if "ca_bundle" in path.name or "oui_table" in path.name or "lines.h" in path.name:
            continue
        txt = path.read_text("utf-8", "replace")
        out += [blk.strip("\n") for blk in re.split(r"\n\s*\n", txt) if 40 < len(blk.strip()) < 3000]
    import random
    random.Random(7).shuffle(out)
    kept, n = [], 0
    for blk in out:
        if n > budget:
            break
        kept.append(blk)
        n += len(blk)
    return kept


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(HERE / "data"))
    ap.add_argument("--offline", action="store_true", help="skip downloads; use cached files only")
    a = ap.parse_args()
    out = Path(a.out)
    cache = out / "cache"
    pre, stats = [], {}

    for gid, want in GUTENBERG:
        try:
            raw = fetch(GUTENBERG_URL.format(gid), cache / f"pg{gid}.txt") if not a.offline \
                else (cache / f"pg{gid}.txt").read_bytes()
        except Exception as e:
            print(f"  skipping Gutenberg {gid}: {e}", file=sys.stderr)
            continue
        head = raw[:4000].decode("utf-8", "replace")
        if want.lower() not in head.lower():
            print(f"  skipping Gutenberg {gid}: title doesn't mention {want!r}", file=sys.stderr)
            continue
        paras = gutenberg_text(raw)
        stats[f"gutenberg {gid} ({want})"] = sum(map(len, paras))
        pre += paras

    for name in SRD_FILES:
        try:
            raw = fetch(SRD_URL.format(name), cache / f"srd-{name}.json") if not a.offline \
                else (cache / f"srd-{name}.json").read_bytes()
            paras = srd_paragraphs(name, json.loads(raw))
        except Exception as e:
            print(f"  skipping SRD {name}: {e}", file=sys.stderr)
            continue
        stats[f"SRD {name}"] = sum(map(len, paras))
        pre += paras

    code = code_paragraphs()
    stats["code"] = sum(map(len, code))
    pre += code

    # The voice pairs go into pretraining too, so the voice isn't new to it.
    sys.path.insert(0, str(HERE))
    import make_voice
    voice = make_voice.build(out / "cache")
    stats["voice pairs"] = sum(map(len, voice))

    out.mkdir(parents=True, exist_ok=True)
    text = ascii_only("\n\n".join(pre + voice)) + "\n\n"
    text = re.sub(r"\n{3,}", "\n\n", text)
    (out / "pretrain.txt").write_text(text, encoding="ascii")
    (out / "voice.txt").write_text(ascii_only("".join(voice)), encoding="ascii")

    for k, v in stats.items():
        print(f"  {k:<40} {v / 1e6:6.2f} MB")
    print(f"pretrain.txt {len(text) / 1e6:.2f} MB, voice.txt {len(voice)} pairs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
