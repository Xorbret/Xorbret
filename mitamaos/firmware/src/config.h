#pragma once

#define OS_NAME        "MitamaOS"
#define OS_VERSION     "0.1"
#define OS_AUTHOR      "Xorbret"
// MitamaOS is built on the PaperOS core (GPLv2) by Artem76228 — see
// firmware/CREDITS.md. This OS is a defensive, receive-only companion
// firmware for the M5Cardputer ADV; it never transmits attacks.

#define SCREEN_W       240
#define SCREEN_H       135
#define HEADER_H       16
#define FOOTER_H       14
#define CONTENT_Y      (HEADER_H + 1)
#define CONTENT_H      (SCREEN_H - HEADER_H - FOOTER_H - 1)

// --- MitamaOS "Cyberpunk over red bones" palette ---------------------------
// Every app pulls its colors from these macros (header/footer/dialogs via
// OsUI, plus their own list/content drawing), so retheming them here
// reskins the whole OS at once instead of touching every app individually.
// This default palette becomes the first shippable runtime .thm theme later.
//
// Design language (see mitamaos/outline.md "Visual identity"): a bright
// Cyberpunk-2077 surface — cyber-yellow, hot-magenta, vivid-purple, cyan —
// laid over a structural RED "bones" layer that is never removed, only
// mostly covered. RGB565 values are the locked hex colors converted for the
// 8-bit canvas.
//
// Bones (structural red underlay):
//   Bones Red  #E5162B 0xE0A5 · Dim Red #7A0A18 0x7843 · Bright Red #FF1133 0xF886
// Surface:
//   Cyber Yellow #FCEE0A 0xFF61 · Hot Magenta #FF2A6D 0xF94D
//   Vivid Purple #B026FF 0xB13F · Cyan #05D9E8 0x06DD · Neon Green #39FF14 0x3FE2
//   Near-black violet #0A0118 0x0803 · Deep violet #1A0B2E 0x1845
// Text: Muted violet #8A7CA8 0x8BF5 · White #FFFFFF 0xFFFF
#define COL_BG         0x0803  // client-area background - near-black violet
#define COL_BG2        0x1845  // dialog / panel face - deep violet
#define COL_TEXT       0xFFFF  // primary text - white
#define COL_DIM        0x8BF5  // secondary text - muted violet
#define COL_ACCENT     0x06DD  // interactive focus / links - cyan ("door")
#define COL_SELECTED   0xF94D  // selected item highlight - hot magenta ("windows")
#define COL_WARN       0xFF61  // cyber yellow
#define COL_ERR        0xF886  // bright red (bones bleed)
#define COL_OK         0x3FE2  // neon green ("good call")

#define COL_HEADER      0xB13F  // active titlebar - vivid purple (white title reads on it)
#define COL_HEADER_BAD  0xE0A5  // bones red - disconnected / error state titlebar
#define COL_HEADER_WARN 0xA260  // dark amber - 3xx / redirect-not-followed
#define COL_HEADER_LOAD 0x7843  // dim red - "inactive window" look while connecting
#define COL_FOOTER      0x1845  // status bar - deep violet
#define COL_FIELD       0xF94D  // hot magenta - form input placeholders

// Neon "bevel" edges. The core is still a light top/left + dark bottom/right
// pair, but recolored for the dark theme: a lit violet highlight and a
// near-black shadow so panels still read as raised/sunken. (A flatter neon
// border pass comes with the M1 UI restyle.)
#define COL_BEVEL_HI    0x6A53  // highlight - lit violet edge
#define COL_BEVEL_LO    0x3186  // shadow - dark violet edge
#define COL_BEVEL_DKLO  0x0000  // deepest shadow (black), outermost edge
#define COL_DESKTOP     0x0803  // desktop background - near-black violet

#define NUM_TILE_COLORS 8
static const uint16_t TILE_COLORS[NUM_TILE_COLORS] = {
    0xFF61, 0xF94D, 0xB13F, 0x06DD,  // yellow, magenta, purple, cyan
    0x3FE2, 0xE0A5, 0xFF61, 0x06DD,  // green, bones-red, yellow, cyan
};

#define TICK_MS        20
#define KEY_REPEAT_MS  120

#define MAX_APPS       28
#define MAX_WIFI_SLOTS 3

// Feature toggles. All apps are compiled in by default; flip one of these
// to 0 to drop it from the binary if flash/RAM ever gets tight.
#define PAPEROS_ENABLE_BROWSER  0   // removed 2026-10-21 (Robert): text-only, frees flash and RAM
#define PAPEROS_ENABLE_EDITOR3D 0   // removed 2026-10-19 (Robert): frees RAM for Scry
#define PAPEROS_ENABLE_PAINT    0   // removed 2026-10-19
#define PAPEROS_ENABLE_PIANO    0   // removed 2026-10-19
#define PAPEROS_ENABLE_MYAI     1
#define PAPEROS_ENABLE_MUSIC    0   // removed 2026-10-19: ~21KB RAM + MP3 decoder
#define PAPEROS_ENABLE_LUA      1
