# MitamaOS

A fully featured, PDA-style handheld OS/firmware for the **M5Cardputer ADV**
(ESP32-S3) with a **Mitama** — a gentle, non-intimidating companion spirit —
that surfaces the device's network environment at a glance.

Loosely inspired by Navi, Pokémon, and Shin Megami Tensei: the Cardputer is a
**COMP** that hosts the Mitama; detected devices are **demons** registered to a
**Compendium**, sorted by **race**. Defensive and receive-only throughout.

Styled after **Cyberpunk 2077** — the **Rajdhani** font and a Night City palette
(cyber-yellow / hot-magenta / purple / cyan) painted over a structural **red
"bones"** layer that frames the UI and bleeds through at seams and glitches. See
`outline.md` for the full design.

## Design commitment

MitamaOS is a **defensive** companion. Everything network-facing is
**receive-only / passive**: it detects, observes, and informs — it never
transmits attacks. No deauth, no evil-portal, no credential capture, no exploit
payloads. There is no use of `esp_wifi_80211_tx` anywhere in the codebase, and
that stays true as the project grows.

## Status

**M0 — foundation (in progress).** The PaperOS v1.3 core is vendored under
[`firmware/`](firmware/), rebranded to MitamaOS, bumped to the ADV-capable
M5Cardputer library, SD mount patched for ADV hardware, and reskinned to the
Cyberpunk palette. Everything else (the Mitama companion, Proxima home,
Compendium, Scry/Ward/Augury) lands on top in later milestones. The full
vision, architecture, detection strategy, and Compendium taxonomy live in
[`outline.md`](outline.md); the build order is §8.

## Platform

M5Cardputer **ADV** (ESP32-S3, 8MB flash, no PSRAM). PlatformIO + Arduino,
M5Cardputer ≥ 1.1.1 (native ADV keyboard), M5Unified/M5GFX, vendored Lua 5.4,
LittleFS + SD.

```sh
cd firmware
pio run                 # build
pio run -t upload       # flash over USB (hold G0 + reset for bootloader)
```

The on-device assistant model (`model.bin`, ~4.7 MB) is **not** in this repo —
it ships via GitHub Releases and is copied to `/MitamaOS/model.bin` on the SD
card. The OS boots and runs fully without it (the Mitama just skips LLM
flavoring). All other SD data lives under `/MitamaOS/` so the card can be
shared with other firmwares (e.g. via Launcher).

## License

**GPLv2.** MitamaOS builds on [PaperOS](https://github.com/Artem76228/PaperOS)
(GPLv2) as its OS core, so the combined work is GPLv2 with full source — which
keeps it open: derivatives can be sold but cannot be taken closed-source. MIT
components (SquachWatch, AdvanceOS, Bjorn) are absorbed under GPLv2 with
attribution preserved. The project name and logo are held separately. Full
text in [`firmware/LICENSE`](firmware/LICENSE); see also `outline.md` §13.

## Credits

Full third-party notices are in [`firmware/CREDITS.md`](firmware/CREDITS.md).
In brief:

- **OS core** (multitasking launcher, app framework, on-device myAI, Lua
  engine + store) — [PaperOS](https://github.com/Artem76228/PaperOS)
  (**GPLv2**, © Artem76228). *Vendored.*
- **Detection engine** (passive, receive-only Wi-Fi scanning) —
  [SquachWatch-CYD](https://github.com/skizzophrenic/SquachWatch-CYD) (MIT).
  *Planned.*
- **Theme manager & productivity apps** —
  [AdvanceOS-for-cardputer](https://github.com/bomberman30/AdvanceOS-for-cardputer)
  (MIT, © bomberman30). *Planned.*
- **Companion architecture inspiration** —
  [Bjorn](https://github.com/infinition/Bjorn) (MIT), inverted from offensive
  to defensive. *Planned.*
