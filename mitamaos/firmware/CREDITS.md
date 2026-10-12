# MitamaOS — Credits & Third-Party Notices

MitamaOS is **free and open source** under the **GNU General Public License,
version 2** (see [`LICENSE`](LICENSE)). It is a defensive, **receive-only**
companion firmware for the M5Cardputer ADV — it observes the surrounding
radio environment and never transmits attacks.

Because the OS core is GPLv2, the combined firmware is distributed under GPLv2.
That is deliberate: it keeps MitamaOS open. Anyone may use, study, modify and
redistribute it (including selling copies), but derivative firmware must also
ship its source under GPLv2 — it cannot be taken closed-source. The project
**name and logo** ("MitamaOS", the magatama mark) are held separately by the
project and are not licensed for use on derivative distributions.

---

## Vendored source (ships in this tree)

- **PaperOS** — the OS core (launcher, app framework, UI chrome, apps, the
  vendored Lua runtime, the on-device myAI engine). © Artem76228, licensed
  **GPLv2**. MitamaOS is built on this core.
  https://github.com/Artem76228/PaperOS
  The GPLv2 text in [`LICENSE`](LICENSE) is PaperOS's own license file.

- **Lua 5.4** — vendored under `lib/lua/`. © Lua.org, PUC-Rio, **MIT License**.
  https://www.lua.org/

- **minimp3** — single-header MP3 decoder (`src/libs/minimp3.h`).
  © Lion (lieff), **CC0 / public domain**.
  https://github.com/lieff/minimp3

- **AudioOutputM5Speaker** (`src/libs/AudioOutputM5Speaker.h`) — adapter
  bridging ESP8266Audio to the M5 Speaker. Derived from M5Stack example code,
  **MIT License**.

## Build-time dependencies (fetched by PlatformIO, not vendored)

- **M5Cardputer** (≥ 1.1.1) — © M5Stack, **MIT License**. Version 1.1.1 is the
  first release with native M5Cardputer **ADV** support (TCA8418 I2C keypad).
- **M5Unified / M5GFX** — © M5Stack & lovyan03, **MIT License**.
- **ESP8266Audio** — © Earle F. Philhower III, **GPLv3 / LGPL** components.
- **Arduino-ESP32 / ESP-IDF** — © Espressif & contributors, **LGPL / Apache-2.0**.

## Planned / design-inspiration sources (not yet integrated)

These inform later milestones and are credited now so provenance is clear:

- **SquachWatch-CYD** — © its authors, **MIT License**. Source of the planned
  passive Wi-Fi detection engine (promiscuous channel-hopping, receive-only).
  Lifted as a background service; no transmit paths are ported.
- **AdvanceOS for Cardputer** — © bomberman30, **MIT License**. Reference for
  the theme manager and productivity apps.
- **Bjorn** — © its authors, **MIT License**. Companion-architecture
  inspiration (its line-bank + orchestrator pattern), inverted from an
  offensive "raider" into a defensive "guardian." No offensive behavior is
  carried over.

## What MitamaOS adds on top

The companion spirit ("the Mitama"), the Proxima home + Compendium, the
Scry / Ward / Augury postures, the Cyberpunk-over-red-bones visual identity,
the Rajdhani typography, the sound set, and all original art and writing are
new work by the MitamaOS project, © Xorbret, under GPLv2.

> If a credit here is wrong or incomplete, please open an issue — correct
> attribution matters to this project.
