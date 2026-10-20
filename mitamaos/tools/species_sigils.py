#!/usr/bin/env python3
"""Species sigils for the Compendium, authored once, rendered two ways.

Each sigil is a list of pen ops on the 24-unit grid used by MUI::_Pen
(ui/mitama_ui.h). `python3 species_sigils.py header` writes the C++
switch body; `python3 species_sigils.py svg` prints SVG snippets for previews.
"""
import math, sys, json

def spiral(cx, cy, r0, r1, turns, steps=28, a0=-90):
    pts = []
    for i in range(steps + 1):
        t = i / steps
        a = math.radians(a0 + 360 * turns * t)
        r = r0 + (r1 - r0) * t
        pts += [cx + r * math.cos(a), cy + r * math.sin(a)]
    return pts

def almond(cx, cy, w, h):
    """Eye lids: two arcs meeting at the corners."""
    hw = w / 2
    R = (hw * hw + h * h) / (2 * h)
    half = math.degrees(math.asin(hw / R))
    return [("arc", cx, cy + (R - h), R, 270 - half, 270 + half),
            ("arc", cx, cy - (R - h), R, 90 - half, 90 + half)]

def polar(cx, cy, r, a):
    a = math.radians(a)
    return cx + r * math.cos(a), cy + r * math.sin(a)

def mirror(pts):
    return [24 - v if i % 2 == 0 else v for i, v in enumerate(pts)]

S = {}  # species -> (yokai name, race, meaning, ops)

# ---- Vile (watchers: the eye is theirs alone) ----
ops = almond(12, 12, 9, 2.6) + [("dot", 12, 12, 1.2)]
for k in range(6):
    x, y = polar(12, 12, 8.4, -90 + 60 * k)
    ops += [("ring", x, y, 1.8), ("dot", x, y, 0.5)]
S["FLOCK"] = ("Hyakume", "Vile", "the hundred-eyed: one eye watching, six more around it", ops)

ops = [("rect", 7.5, 2.5, 9, 17), ("line", 7.5, 5.5, 16.5, 5.5),
       ("poly", [13.6, 7.5, 10.4, 12.2, 13.6, 12.2, 10.4, 17.2], False),
       ("line", 12, 19.5, 12, 22.5), ("dot", 12, 4, 0.6)]
S["AXON"] = ("Raijin Fuda", "Vile", "a thunder talisman: the paper ward of a bodycam and its taser", ops)

ops = [("rect", 4, 5, 16, 14), ("line", 12, 5, 12, 19), ("line", 4, 12, 20, 12)]
for (cx, cy) in [(8, 8.5), (16, 8.5), (16, 15.5)]:
    ops += almond(cx, cy, 5, 1.6) + [("dot", cx, cy, 0.7)]
ops += [("arc", 8, 13.6, 2.6, 30, 150)]  # the one eye that is shut
S["ALPR"] = ("Mokumokuren", "Vile", "eyes in a paper screen, reading every plate that passes", ops)

# ---- Night (lenses and doors) ----
oct8 = []
for k in range(8):
    oct8 += list(polar(12, 12, 9.6, 22.5 + 45 * k))
ops = [("poly", oct8, True), ("ring", 12, 12, 4.6), ("dot", 12, 12, 1.2)]
for k in range(8):
    a = 45 * k
    x0, y0 = polar(12, 12, 6.4, a); x1, y1 = polar(12, 12, 7.8, a)
    ops += [("line", x0, y0, x1, y1)]
S["CAMERA"] = ("Bagua Kagami", "Night", "the eight-sided mirror that catches what looks at it", ops)

body = [8.5, 6, 15.5, 6, 17.5, 9.5, 17.5, 14.5, 15.5, 18, 8.5, 18, 6.5, 14.5, 6.5, 9.5]
ops = [("poly", body, True), ("line", 9.5, 4.5, 14.5, 4.5), ("line", 9.5, 19.5, 14.5, 19.5),
       ("line", 12, 2, 12, 4.5), ("line", 6.5, 9.5, 17.5, 9.5),
       ("arc", 12, 11.2, 3.6, 25, 155), ("poly", [11, 14.5, 12.4, 17.4, 13.4, 14.4], False)]
S["RING"] = ("Chochin-obake", "Night", "the paper lantern by the door that grins and sticks its tongue out", ops)

ops = [("arc", 12, 13, 7, 180, 360), ("line", 5, 13, 7.6, 19.5), ("line", 19, 13, 16.4, 19.5),
       ("line", 7.6, 19.5, 12, 22), ("line", 16.4, 19.5, 12, 22),
       ("line", 10, 17.5, 14, 17.5), ("dot", 12, 10.5, 1.1),
       ("arc", 12, 10.5, 3.6, 220, 320), ("arc", 12, 10.5, 6.8, 235, 305), ("arc", 12, 10.5, 9.8, 245, 295)]
S["META"] = ("Satori", "Night", "the mask that reads your thoughts before you speak them", ops)

# ---- Fairy (small lights that follow you) ----
tail_l = [7.17, 13.7, 7.9, 9.8, 10.2, 6.2, 14.2, 2.4]
tail_r = [16.83, 13.7, 16.4, 10.4, 15.2, 6.8, 14.2, 2.4]
ops = [("arc", 12, 15, 5, -15, 195), ("poly", tail_l, False), ("poly", tail_r, False),
       ("dot", 12, 15.6, 1.4)]
S["AIRTAG"] = ("Hitodama", "Fairy", "a soul-flame drifting after you", ops)

ops = [("rect", 4.5, 4.5, 15, 15), ("poly", spiral(12, 12, 0.6, 5.4, 2.25), False),
       ("dot", 4.5, 4.5, 0.8), ("dot", 19.5, 19.5, 0.8)]
S["TILE"] = ("Fuin", "Fairy", "a square seal with a spiral bound inside it", ops)

ops = [("dashed", 12, 12, 7.5, 2, 3.5)]
for k in range(3):
    a = -90 + 120 * k
    x, y = polar(12, 12, 7.5, a)
    ops += [("dot", x, y, 1.9), ("arc", 12, 12, 7.5, a - 50, a - 8)]
ops += [("poly", [9.2, 9, 10.7, 10.9, 13.3, 10.9, 14.8, 9, 14.4, 12.6, 12, 15.2, 9.6, 12.6], True)]
S["SAMSUNG_TAG"] = ("Kitsune-bi", "Fairy", "fox-fire: three wisps circling a fox", ops)

flame = [12, 5.2, 14.8, 9.4, 15.6, 13, 14.2, 16.6, 12, 17.8, 9.8, 16.6, 8.4, 13, 9.2, 9.4]
inner = [12, 10.6, 13.3, 13.5, 12, 15.4, 10.7, 13.5]
ops = [("ring", 12, 12, 9.4), ("poly", flame, True), ("poly", inner, True)]
S["GOOGLE_TAG"] = ("Onibi", "Fairy", "a demon-fire held in a ring", ops)

# ---- Avian ----
ops = [("arc", 12, 15, 9, 198, 342), ("line", 12, 15, 12, 22.5), ("dot", 12, 15, 1.2)]
for a in (210, 238, 270, 302, 330):
    x, y = polar(12, 12 + 3, 9, a)
    ops += [("line", 12, 15, x, y)]
ops += [("line", 10.5, 22.5, 13.5, 22.5)]
S["DRONE"] = ("Tengu Uchiwa", "Avian", "the tengu's feather fan, master of the wind", ops)

# ---- Herald ----
ops = [("arc", 12, 10.5, 3.6, 180, 360), ("line", 8.4, 10.5, 7, 16.5), ("line", 15.6, 10.5, 17, 16.5),
       ("line", 6, 16.5, 18, 16.5), ("dot", 12, 18.8, 1.2), ("line", 12, 6.9, 12, 4),
       ("ring", 12, 2.8, 1.2), ("arc", 12, 12.5, 10.2, -28, 28), ("arc", 12, 12.5, 10.2, 152, 208)]
S["IBEACON"] = ("Hosho Rei", "Herald", "a Taoist ritual bell, always ringing out where it is", ops)

# ---- Foul ----
legs = [[10.2, 11.6, 7, 6.8, 4.4, 8.8], [9.5, 12.8, 5.4, 10.4, 2.8, 13.4],
        [9.5, 14.3, 5.4, 15.4, 3.4, 18.8], [10.2, 15.6, 7.6, 18.8, 6, 21.6]]
ops = [("line", 12, 1.5, 12, 7.6), ("dot", 12, 9.1, 1.5), ("ring", 12, 13.6, 2.8)]
for l in legs:
    ops += [("poly", l, False), ("poly", mirror(l), False)]
S["SKIMMER"] = ("Jorogumo", "Foul", "the spider that waits by the card slot on a thread", ops)

# ---- Jaki ----
ops = [("ring", 12, 12, 9.4)]
for k in range(3):
    a = -90 + 120 * k
    hx, hy = polar(12, 12, 3.6, a)
    ops += [("dot", hx, hy, 2.1)]
    # tail leaves the head's outer edge and spirals out toward the rim
    ops += [("poly", spiral(12, 12, 5.6, 8.2, 115 / 360, steps=8, a0=a - 12), False)]
S["RAVEN"] = ("Mitsudomoe Taiko", "Jaki", "the thunder drum: it hears the bang and spins", ops)

# ---- Fiend (attacks; drawn broken) ----
ropeL = [2.5, 7.2, 4.8, 9, 7.1, 7.2, 9.4, 9]
ropeL2 = [2.5, 9, 4.8, 7.2, 7.1, 9, 9.4, 7.2]
ropeR = [14.6, 10.4, 16.9, 8.6, 19.2, 10.4, 21.5, 8.6]
ropeR2 = [14.6, 8.6, 16.9, 10.4, 19.2, 8.6, 21.5, 10.4]
ops = [("poly", ropeL, False), ("poly", ropeL2, False), ("poly", ropeR, False), ("poly", ropeR2, False),
       ("line", 13.4, 3.5, 10.6, 14.5),
       ("poly", [5.5, 9.5, 7.6, 12.6, 5.5, 14.8, 7.6, 18], False),
       ("poly", [18.4, 11, 16.3, 14.1, 18.4, 16.3, 16.3, 19.5], False)]
S["DEAUTH"] = ("Kirareta Shimenawa", "Fiend", "the sacred rope cut through: your connection, severed", ops)

ops = [("ring", 12, 12, 9.4), ("arc", 12, 7.3, 4.7, -90, 90), ("arc", 12, 16.7, 4.7, 90, 270),
       ("dot", 12, 7.3, 1.5), ("dot", 12, 16.7, 1.5), ("arc", 13.6, 12, 9.4, -55, 55)]
S["EVILTWIN"] = ("Nise Taiji", "Fiend", "a false yin-yang: two of the same where there should be one of each", ops)

face = [6.5, 7, 17.5, 7, 18.5, 13, 15.5, 20, 8.5, 20, 5.5, 13]
hornL = [7, 7, 4.6, 1.8, 10, 7]
ops = [("poly", face, True), ("poly", hornL, False), ("poly", mirror(hornL), False),
       ("line", 7.6, 9.6, 10.6, 11.2), ("line", 16.4, 9.6, 13.4, 11.2),
       ("line", 8.4, 15.4, 15.6, 15.4),
       ("poly", [9.4, 15.4, 10.3, 18, 11.2, 15.4], False),
       ("poly", [12.8, 15.4, 13.7, 18, 14.6, 15.4], False)]
S["HACKER"] = ("Oni-men", "Fiend", "an oni mask: someone working against you", ops)

ORDER = ["FLOCK", "AXON", "ALPR", "CAMERA", "RING", "META", "AIRTAG", "TILE", "SAMSUNG_TAG",
         "GOOGLE_TAG", "DRONE", "IBEACON", "SKIMMER", "RAVEN", "DEAUTH", "EVILTWIN", "HACKER"]

def f(v):
    s = ("%.2f" % v).rstrip("0").rstrip(".")
    return s + ("f" if "." in s else "")

def cpp_ops(ops, ind="            "):
    out, arrs = [], 0
    for op in ops:
        k = op[0]
        if k in ("line", "ring", "dot", "rect", "dashed"):
            out.append(f"p.{k}({', '.join(f(v) for v in op[1:])});")
        elif k == "arc":
            out.append(f"p.arc({', '.join(f(v) for v in op[1:])});")
        elif k == "poly":
            pts, closed = op[1], op[2]
            name = f"q{arrs}"; arrs += 1
            out.append(f"{{ static const float {name}[] = {{ {', '.join(f(v) for v in pts)} }}; "
                       f"p.poly({name}, {len(pts)//2}{'' if closed else ', false'}); }}")
    return "\n".join(ind + l for l in out)

def svg_ops(ops):
    out = []
    for op in ops:
        k = op[0]
        if k == "line":
            out.append('<line x1="%.2f" y1="%.2f" x2="%.2f" y2="%.2f"/>' % op[1:])
        elif k == "ring":
            out.append('<circle cx="%.2f" cy="%.2f" r="%.2f"/>' % op[1:])
        elif k == "dashed":
            x, y, r, on, off = op[1:]
            out.append('<circle cx="%.2f" cy="%.2f" r="%.2f" stroke-dasharray="%.2f %.2f"/>' % (x, y, r, on, off))
        elif k == "dot":
            out.append('<circle class="f" cx="%.2f" cy="%.2f" r="%.2f"/>' % op[1:])
        elif k == "rect":
            out.append('<rect x="%.2f" y="%.2f" width="%.2f" height="%.2f"/>' % op[1:])
        elif k == "arc":
            x, y, r, a0, a1 = op[1:]
            sx, sy = polar(x, y, r, a0); ex, ey = polar(x, y, r, a1)
            large = 1 if (a1 - a0) % 360 > 180 else 0
            out.append('<path d="M%.2f %.2fA%.2f %.2f 0 %d 1 %.2f %.2f"/>' % (sx, sy, r, r, large, ex, ey))
        elif k == "poly":
            pts = " ".join("%.2f" % v for v in op[1])
            out.append('<%s points="%s"/>' % ("polygon" if op[2] else "polyline", pts))
    return "".join(out)

if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "svg"
    if mode == "header":
        for sp in ORDER:
            name, race, meaning, ops = S[sp]
            print(f"        case SP_{sp}: // {name} - {meaning}")
            print(cpp_ops(ops))
            print("            break;")
    else:
        print(json.dumps([{"sp": sp, "name": S[sp][0], "race": S[sp][1], "meaning": S[sp][2],
                           "svg": svg_ops(S[sp][3])} for sp in ORDER]))
