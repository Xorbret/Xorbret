"""Bake a TTF into an LGFX/TFT_eSPI .vlw (anti-aliased) font, as bytes or a C header."""
import sys, struct, freetype

def bake(ttf, px, chars=None):
    face = freetype.Face(ttf)
    face.set_pixel_sizes(0, px)
    chars = chars or [chr(c) for c in range(0x20, 0x7F)]
    glyphs = []
    for ch in chars:
        face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT)
        g = face.glyph; b = g.bitmap
        w, h = b.width, b.rows
        data = bytes(b.buffer[r * b.pitch + c] for r in range(h) for c in range(w))
        glyphs.append((ord(ch), h, w, (g.advance.x + 32) >> 6, g.bitmap_top, g.bitmap_left, data))
    asc = face.size.ascender >> 6
    desc = abs(face.size.descender >> 6)
    out = bytearray(struct.pack('>6i', len(glyphs), 11, px, 0, asc, desc))
    for u, h, w, adv, top, left, _ in glyphs:
        out += struct.pack('>7i', u, h, w, adv, top, left, 0)
    for *_, data in glyphs:
        out += data
    return bytes(out), glyphs, asc, desc

def header(name, blob, note):
    lines = [f"// {note}", f"static const uint8_t {name}[] PROGMEM = {{"]
    for i in range(0, len(blob), 20):
        lines.append("  " + ",".join(f"0x{b:02X}" for b in blob[i:i+20]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"

if __name__ == "__main__":
    ttf, px, name, note = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    blob, *_ = bake(ttf, px)
    sys.stdout.write(header(name, blob, note))
