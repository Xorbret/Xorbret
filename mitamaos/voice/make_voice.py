"""Builds the Mitama's fine-tuning pairs ("Q: ...\\nA: ...\\n\\n").

Sources:
  mitama_voice.txt              hand-written pairs (edit this one)
  firmware/src/mitama/lines.h   the line bank, as OS event prompts
  SRD monsters and spells       generated "what is X?" answers in voice,
                                if build_corpus.py has cached the JSON

Every pair must fit the device: prompt + answer + the blank line that ends
it within 95 bytes, because the engine's key/value cache holds 96 positions.
"""
import json
import random
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
LINES_H = HERE.parent / "firmware" / "src" / "mitama" / "lines.h"
MAX_LEN = 95

# lines.h event -> the prompt the OS will send for it
EVENTS = {
    "EV_BOOT": ["*boot*"], "EV_IDLE": ["*idle*"], "EV_LORE": ["*lore*"],
    "EV_OFFLINE": ["*offline*"], "EV_CLEAR": ["*clear*"], "EV_GPS_FIX": ["*gps fix*"],
    "EV_SCRY_ON": ["*scry on*"], "EV_WIFI_JOIN": ["*wifi joined*"], "EV_SINCERE": ["*sincere*"],
    # sightings: one prompt per species, so the model learns each one
    "EV_VILE": ["*seen FLOCK*", "*seen AXON*", "*seen ALPR*"],
    "EV_NIGHT": ["*seen CAMERA*", "*seen RING*", "*seen META*"],
    "EV_FAIRY": ["*seen AIRTAG*", "*seen TILE*", "*seen SMARTTAG*", "*seen GOOGLE TAG*"],
    "EV_AVIAN": ["*seen DRONE*"], "EV_HERALD": ["*seen IBEACON*"],
    "EV_FOUL": ["*seen SKIMMER*"], "EV_JAKI": ["*seen RAVEN*"],
    "EV_FIEND": ["*seen DEAUTH*", "*seen EVIL TWIN*", "*seen HACKER*"],
}
DETAIL = {
    "FLOCK": "Flock Safety", "AXON": "Axon body cam", "ALPR": "Plate reader",
    "CAMERA": "Wi-Fi camera", "RING": "Ring doorbell", "META": "Meta glasses",
    "AIRTAG": "Apple AirTag", "TILE": "Tile tracker", "SMARTTAG": "Samsung SmartTag",
    "GOOGLE TAG": "Google Find tag", "DRONE": "DJI drone", "IBEACON": "iBeacon",
    "SKIMMER": "HC-05 module", "RAVEN": "Raven gunshot sensor", "DEAUTH": "Deauth flood",
    "EVIL TWIN": "Evil twin AP", "HACKER": "Flipper Zero",
}

SCHOOL_TAILS = {
    "evocation": ["Big boom. Mind your party.", "Loud. Effective. Subtle as a dragon."],
    "abjuration": ["Defensive. My kind of magic.", "A shield spell. I approve."],
    "conjuration": ["Summons things. Ask me how that goes.", "Makes stuff appear. Like me."],
    "divination": ["Seeing things. My specialty.", "Knowledge magic. Very me."],
    "enchantment": ["Mind games. Rude, but effective.", "Charm magic. I'm naturally like this."],
    "illusion": ["Lies, but with light.", "Fake, convincing, very rogue."],
    "necromancy": ["Death magic. Tasteful, in moderation.", "Bones optional. Dread included."],
    "transmutation": ["Changes things. Like my mood.", "Turns this into that. Handy."],
}
TYPE_TAILS = {
    "dragon": "Greedy, scaly, smug. Respect.", "undead": "Already dead. Still a problem.",
    "fiend": "One of my cousins. Be polite.", "aberration": "Wrong in every direction.",
    "beast": "Just an animal. Mostly.", "celestial": "Holy. Insufferable.",
    "construct": "Built, not born. Like me, sort of.", "elemental": "Pure weather with a grudge.",
    "fey": "Pretty and dangerous. Don't accept food.", "giant": "Big. Throws rocks. Classic.",
    "humanoid": "A person. The worst monster of all.", "monstrosity": "A monster's monster.",
    "ooze": "Wobbly death. Don't step in it.", "plant": "Salad that fights back.",
}


def _clean(s):
    return re.sub(r"\s+", " ", s).strip()


def pair(q, a):
    return f"Q: {q}\nA: {a}\n\n"


def fits(p):
    return len(p.encode("ascii", "replace")) <= MAX_LEN


def hand_written(path=HERE / "mitama_voice.txt"):
    pairs, q = [], None
    for line in path.read_text("utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("Q:"):
            q = _clean(line[2:])
        elif line.startswith("A:") and q:
            pairs.append((q, _clean(line[2:])))
    return pairs


def line_bank(path=LINES_H):
    if not path.exists():
        return []
    rx = re.compile(r'\{\s*(EV_\w+)\s*,\s*T_\w+\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')
    pairs = []
    for ev, text in rx.findall(path.read_text("utf-8")):
        text = text.encode().decode("unicode_escape")
        for prompt in EVENTS.get(ev, []):
            sp = prompt[6:-1] if prompt.startswith("*seen ") else "something"
            a = text.replace("{sp}", sp).replace("{d}", DETAIL.get(sp, sp)).replace("{n}", "2")
            pairs.append((prompt, a))
    # device-specific lines: { Sig::T_AIRTAG, "..." }
    names = {"T_SAMSUNG_TAG": "SMARTTAG", "T_GOOGLE_TAG": "GOOGLE TAG", "T_EVILTWIN": "EVIL TWIN"}
    for sp, text in re.findall(r'\{\s*Sig::(T_\w+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}', path.read_text("utf-8")):
        name = names.get(sp, sp[2:])
        a = text.encode().decode("unicode_escape").replace("{sp}", name).replace("{d}", DETAIL.get(name, name))
        pairs.append((f"*seen {name}*", a))
    return pairs


def _ord(n):
    return {1: "1st", 2: "2nd", 3: "3rd"}.get(n, f"{n}th")


def srd_pairs(cache, rng):
    pairs = []
    mon, spl = cache / "srd-Monsters.json", cache / "srd-Spells.json"
    if mon.exists():
        for m in json.loads(mon.read_bytes()):
            cr = m.get("challenge_rating")
            cr = {0.125: "1/8", 0.25: "1/4", 0.5: "1/2"}.get(cr, cr)
            tail = TYPE_TAILS.get(m["type"].split()[0], "Hit it until it stops.")
            a = f"{m['size']} {m['type']}, challenge {cr}. {tail}"
            pairs.append((f"what is a {m['name'].lower()}?", a))
    if spl.exists():
        for s in json.loads(spl.read_bytes()):
            school = s.get("school", {}).get("index", "")
            lvl = s.get("level", 0)
            kind = f"{school.capitalize()} cantrip" if lvl == 0 else f"{_ord(lvl)}-level {school}"
            tail = rng.choice(SCHOOL_TAILS.get(school, ["Magic. Handle with care."]))
            pairs.append((f"what is {s['name'].lower()}?", f"{kind}. {tail}"))
    return pairs


def variants(q, rng):
    """A few ways people type the same question on a tiny keyboard."""
    if q.startswith("*"):
        return [q]
    out = {q, q[:1].upper() + q[1:]}
    if q.endswith("?"):
        out.add(q[:-1])
    return sorted(out)


def build(cache=HERE / "data" / "cache", seed=1234, max_generated=300):
    rng = random.Random(seed)
    src = hand_written() + line_bank()
    gen = srd_pairs(Path(cache), rng)
    out, dropped = [], 0
    for q, a in src:
        for v in variants(q, rng):
            p = pair(v, a)
            if fits(p):
                out.append(p)
            else:
                dropped += 1
    # Generated pairs fill gaps; hand-written answers win, and they're capped
    # so the formula doesn't drown out the voice.
    known = {q.lower().rstrip("?") for q, _ in src}
    gen = [(q, a) for q, a in gen if q.rstrip("?") not in known and fits(pair(q, a))]
    rng.shuffle(gen)
    out += [pair(q, a) for q, a in gen[:max_generated]]
    if dropped:
        print(f"  make_voice: {dropped} pairs too long for the device, dropped", file=sys.stderr)
    rng.shuffle(out)
    return out


def too_long():
    """Hand-written pairs that won't fit, so they can be shortened."""
    return [(q, a, len(pair(q, a))) for q, a in hand_written() + line_bank() if not fits(pair(q, a))]


if __name__ == "__main__":
    bad = too_long()
    for q, a, n in bad:
        print(f"{n:3d}  Q: {q} | A: {a}")
    pairs = build()
    print(f"{len(pairs)} pairs, {len(bad)} too long")
