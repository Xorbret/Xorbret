# MitamaOS — Project Outline

A fully featured, PDA-style handheld OS/firmware for the **M5Cardputer ADV**
(ESP32-S3), with a **Mitama** — a gentle companion spirit — that makes the
device's network environment legible at a glance.

## Theme & naming

Loosely inspired by Navi (a watchful companion), Pokémon (collect/catalog), and
**Shin Megami Tensei** (friendly-demon framing). The through-line:

- The **Mitama** is the buddy — named for SMT's little floating soul/mask demon
  family (the word means "spirit/soul"). It's deliberately **non-intimidating**:
  this is a *defensive* companion, so nothing menacing. It reacts to the network
  around it and speaks via the on-device LLM (§12).
- The Cardputer is styled as a **COMP** (SMT's demon-summoning handheld) — the
  device that hosts the Mitama and registers what it encounters.
- Detections are **demons encountered**; the Dex is the **Compendium** (SMT's
  demon registry); device categories are **races** (§5a). Hostile behaviors
  (deauth/evil-twin) are corrupted entries, framed as rare **Fiends**.

## Visual identity — Cyberpunk 2077 / Night City

The look is **Cyberpunk 2077**, not the Win95 gray PaperOS ships. This replaces
PaperOS's `config.h` palette at M0 and becomes the first shippable runtime `.thm`
theme once the AdvanceOS Theme Manager lands (M4).

### The "red bones" principle (core to the look)

Red is the **structural substrate** of the UI — the chassis the whole HUD is
built on, exactly like CP2077's own red framing lines under everything. The
bright colors are *painted over* the red and bleed through at seams, borders,
and glitches. The house metaphor:

| House part | Red is the slats underneath | UI mapping |
|------------|------------------------------|------------|
| Roof (yellow) | | top bar / header / active title, primary highlight |
| Walls (purple/pink) | | app panel fills / content backgrounds |
| Windows (magenta, blue frames) | | fields & list-selection in magenta, cyan/blue borders |
| Door (cyan) | | interactive focus / links / accents |
| **Slats (red)** | **the frame beneath all of it** | base grid, divider rules, panel/bevel outlines, scanline underlay, chassis — mostly behind the bright layers, showing at edges, seams, and glitch states |

So red is **never removed** — it's the load-bearing layer. The bright palette
reads as the finished surface; the red reads as the machine underneath it. Red
intensifies (bleeds through more) in alert/Fiend states — that's the glitch.

- **Font: Rajdhani** — the Cyberpunk 2077 UI typeface. Free (SIL OFL), condensed
  (legible on a 240×135 screen), embeddable. On-device plan: convert the TTF to
  M5GFX/LVGL bitmap fonts at the 2–3 sizes the UI uses (header, body, small) and
  bake them in; PaperOS currently relies on LGFX's built-in font, so this is a
  font-asset swap plus a global default-font change (mind the note in `main.cpp`
  that layouts are sized for the built-in font — re-check spacing after swap).
- **Palette (RGB565 for the 8-bit canvas):**

  | Layer | Role | Color | Hex | RGB565 |
  |-------|------|-------|-----|--------|
  | **Bones** | Structural frame / grid / rules | Bones Red | `#E5162B` | `0xE0A5` |
  | **Bones** | Dim underlay (scanlines, recessed frame) | Dim Red | `#7A0A18` | `0x7843` |
  | **Bones** | Glitch bleed (alert/Fiend intensify) | Bright Red | `#FF1133` | `0xF886` |
  | Surface | Primary highlight (roof) | Cyber Yellow | `#FCEE0A` | `0xFF61` |
  | Surface | Secondary / selection (windows) | Hot Magenta | `#FF2A6D` | `0xF94D` |
  | Surface | Panel fills / walls | Vivid Purple | `#B026FF` | `0xB13F` |
  | Surface | Interactive focus / door | Cyan | `#05D9E8` | `0x06DD` |
  | Surface | Affirmation / "good call" (Smug) | Neon Green | `#39FF14` | `0x3FE2` |
  | Surface | Background field | Near-black violet | `#0A0118` | `0x0803` |
  | Surface | Panels / dialogs | Deep violet | `#1A0B2E` | `0x1845` |
  | Text | Secondary text | Muted violet | `#8A7CA8` | `0x8BF5` |
  | Text | Primary text | White | `#FFFFFF` | `0xFFFF` |

- **Red is the bones, not an accent.** Frame outlines, divider rules, bevel
  edges, grid and scanline underlays draw in Bones Red (dim where recessed).
  The bright surface colors sit on top; red shows at seams and edges and
  intensifies on alert/Fiend states (the glitch bleed). This is the signature of
  the look — not optional dressing.
- Optional CRT/scanline + glitch flourishes (the red substrate flickering
  through) fit the Mitama's alert states and Fiend cards.

**Design commitment (non-negotiable):** everything network-facing is
**receive-only / passive**. MitamaOS detects, observes, and informs — it never
transmits attacks. No deauth frames, no evil-portal, no credential capture, no
exploit payloads. There is no call to `esp_wifi_80211_tx` anywhere in the
codebase, and that stays true as the project grows. This is a *defensive*
companion, the inverse of offensive firmware like Porkchop.

---

## 1. Platform & toolchain

- **Board:** M5Cardputer ADV (ESP32-S3FN8, 8MB flash, **NO PSRAM** on the stock
  unit — confirmed by Robert). Same board class PaperOS targets, so its
  no-PSRAM RAM budgeting applies directly.
- **License:** **GPLv2** (the project forks PaperOS; see §13).
- **Framework:** PlatformIO + Arduino. Inherits PaperOS's stack: `M5Cardputer`
  (direct, not M5Unified), vendored Lua 5.4, `ESP8266Audio`, LittleFS + SD. Add
  `NimBLE-Arduino` for passive BLE detection.
- Fastest path to a flashable image on the ADV and the stack the Cardputer
  community already builds on.
- Build with `pio run`; flash with `pio run -t upload`. (Cannot be
  compiled/flashed against real hardware from the Claude session — needs a
  local build pass to shake out library-version issues before flashing.)

---

## 2. The three pillars

MitamaOS is a full handheld OS, not a security tool with a mascot. Scope spans
three pillars:

### Productivity
File manager, notes, calculator, calendar, to-do list, world clock, alarms.

### Games
Build-effort order: Snake → Solitaire → Chess → Poker.
- **Chess difficulty:** Medium-Easy. Shallow search (2–3 ply), *not* a real
  engine. "For fun, not to be a GM."

### Mitama / Proxima layer
The Mitama (an animated sprite) + passive environment monitoring, surfaced as
"Proxima." The Mitama reacts to what the radios see — calm when
normal, alert/agitated when something is worth flagging — and can speak through
the on-device LLM (§12). Its personality and how it emotes without a face are
specified in full in §14.

---

## 3. OS architecture

- **Event bus:** `NetEvent` with severity levels Info / Notice / Warning /
  Alert. Apps and the buddy subscribe; monitors publish. Decouples detection
  from presentation.
- **Kernel:** runs the monitors in the background regardless of which app has
  focus, so detection never stops when you're e.g. playing Snake.
- **App interface:** uniform app lifecycle so apps can be added without
  touching the kernel.

### Architectural changes the full-OS scope forces (do before more apps pile up)

1. **Navigation — "Go button" back-stack.** Current model is ESC-always-goes-
   home. The vision needs a real back-stack: jump to Proxima from
   anywhere, press again to return to *exactly* what you were doing. Kernel
   change, best done early.
2. **Storage — real SD filesystem layer.** Notes, to-do items, save games,
   trust lists all need a proper SD FS layer, not the two stub JSON paths in
   `config.h`. Build once, early — almost everything depends on it.
3. **Buddy rendering to "a surface."** The buddy should render to an abstract
   surface rather than hardcoding the main screen, so relocating it to a second
   display later is a routing change, not a rewrite.

---

## 4. Proximity monitoring (passive / receive-only)

What the buddy legitimately watches for, all receive-only:

- **Deauth / disassoc flood detection** — promiscuous-mode 802.11 management-
  frame sniffing, sliding-window flood detection. Detecting, never sending.
- **Rogue AP / evil-twin detection** — see §6 note: the *good* test is
  same-SSID-**different-encryption**, not same-SSID-different-BSSID (the latter
  false-positives on every mesh network).
- **ARP spoofing detection** — watch the local segment for ARP conflicts /
  gratuitous-ARP abuse. (Planned.)
- **New / unknown device alerts** — track MACs seen over time, flag newcomers.
  Cross-radio device tracker ("have I seen this address before").
- **Weak/open-auth & WPS fingerprinting** — informational flags.
- **BLE presence monitoring** — passive advertisement scanning, burst/spam
  detection, skimmer-like behavior flags.
- **Network health dashboard** — device count over time, channel congestion,
  signal info.

---

## 5. Detection engine strategy — port SquachWatch-CYD (LOCKED IN)

**Decision:** port the detection engine, signature tables, pet, and Dex from
[SquachWatch-CYD](https://github.com/skizzophrenic/SquachWatch-CYD) **wholesale**
into MitamaOS's kernel architecture, *replacing* session-1's from-scratch
monitors rather than extending them. It's MIT, it's Robert's own prior work, it
already targets the Cardputer-ADV, and it's far more battle-tested.

SquachWatch already detects, with per-signature High/Medium/Low confidence
grading (a pattern worth copying directly — it refuses to promise more than the
evidence supports):

- **Flipper Zero** — reliably detectable, High confidence, multiple independent
  BLE/MAC signatures. (SquachWatch even documents an error other tools ship —
  ESP32 Marauder / Wall-of-Flippers use a wrong company ID and two
  unregistered MAC prefixes.)
- **Flock Safety** — detectable but honestly graded: only one OUI of ~30
  candidates is actually IEEE-registered to Flock; the rest overlap with
  generic ESP32 modules. Confidence graded per-signature.
- Also covered: **Axon** body cams, **Meta/Ray-Ban** glasses, **BLE skimmers**,
  **AirTags/Tile/Samsung/Google** trackers, **drones** (OpenDroneID), **ALPR**
  cameras, branded **IP cameras**, **Pwnagotchi** (near-perfect — announces
  itself), **Ring**, **Raven** (gunshot detectors), **iBeacon**.
- **GPS** — `gnss.h`/`gnss.cpp` port directly; unlocks wardrive logging and real
  timestamps for Calendar/World Clock.
- **Dual-band (WiFi 2.4/5 GHz)** — ports directly from the `SQW_WIFI_5G` flag.

**Honest limitation (independently confirmed):** fingerprinting "other
Cardputers on malicious firmware" is not really possible — every ESP32 project
(including this one) would flag every other one. SquachWatch deliberately
excludes bare ESP32 signatures from its hacker bucket for exactly this reason.
Behavior-based detection (deauth / rogue-AP / BLE-spam) is the honest version of
that feature.

---

## 5a. The Compendium — SMT races, rare Fiends & the collection game

SquachWatch ships **17 Dex entries** (18 `DetectionType` values; `UNKNOWN`
gets no card, so `Dex::ENTRIES = COUNT - 1`) and **19 outfits/skins**
(`OutfitId::COUNT`), unlocked two ways:

- **10 by lifetime-detection threshold** — NONE/TANOOKI/UNICORN (free),
  TINFOIL(5), SHADOW(15), PLUMBER BRO(25), TALL BRO(40), SPACE(60),
  BLUE BLUR(100), CAPTAIN(150).
- **9 by one-off event** (`OUTFIT_BY_EVENT`) — WOLF PELT, CHROME WING,
  VOID EYE, SNOW PARKA, SHARK SUIT, YZZERD, OVER 9000, SHAMBLER, TH3 0N3 —
  hidden Easter eggs on specific background screens.

The SMT reframe maps cleanly onto the existing engine: the **Compendium** is the
Dex, a detected device is a **demon** registered to it, and **device categories
= demon races**. The outfit-threshold system is already "encounter N, unlock
reward," so a **race-complete achievement** ("register every Tracker") slots
into the existing `OutfitDef`/`outfitUnlocked()` mechanism as a third unlock
condition alongside threshold and event — no new infrastructure. (Outfits are
reframed as the Mitama's unlockable forms/skins.)

### Finalized taxonomy (device category → SMT race)

| Race (category)     | Species (devices)                     | Tier                  |
|---------------------|----------------------------------------|-----------------------|
| **Vile** (Official) | FLOCK, AXON, ALPR                      | Normal                |
| **Night** (Surveillance) | CAMERA, RING, META                | Normal                |
| **Fairy** (Tracker) | AIRTAG, TILE, SAMSUNG_TAG, GOOGLE_TAG  | Normal                |
| **Avian** (Aerial)  | DRONE                                  | Rare (singleton)      |
| **Herald** (Beacon) | IBEACON                                | Rare (singleton)      |
| **Foul** (Fraud)    | SKIMMER                                | Rare (singleton)      |
| **Jaki** (Acoustic) | RAVEN                                  | Rare (singleton)      |
| **Fiend** (Hostile) | DEAUTH, EVILTWIN, HACKER               | Fiend (corrupted)     |

Race names are flavor styling over the existing `DetectionType` buckets —
pick-and-swap, not load-bearing. The structural points:

- **Government + police surveillance stay lumped** under **Vile/Official**
  (Flock, Axon, ALPR). Flock stays Normal tier, not split off by rarity — it's
  getting more common, not less. Rarity-as-tier and race-membership are separate
  axes; confidence grade (High/Med/Low) can still affect how "hard" a sighting
  registers without being its own race.
- **Rare (singleton races)** = one-of-a-kind in their race (uncommon to
  encounter, special card treatment). Add a `LEGENDARY`/`RARE+` tier to the
  existing `Dex::Rarity` enum — a one-line addition.
- **Fiends** = the hostile bucket (DEAUTH, EVILTWIN, HACKER). In SMT, Fiends are
  the rare, ominous optional encounters — perfect for attacks/hostile behavior
  (not "wildlife"). Visual treatment: glitched/corrupted sprite, garbling name
  text, deliberately "wrong" Compendium-card layout — the SMT-flavored version
  of the MissingNo idea — with the **red bones bleeding through** (the glitch
  intensifies the structural red, per Visual identity). A cosmetic rendering
  flag on those three entries — no engine work.
- Compendium gets a second axis: filter/sort by race, a race glyph per entry,
  and Mitama flavor-text that varies by race (a Vile sighting reads more serious
  than a Fairy one).

**Open question (flagged, not decided):** RAVEN (gunshot detectors) is usually a
city/police deployment too, so by the same "who deploys it" logic it arguably
belongs in **Vile/Official** — which would retire the Jaki/Acoustic singleton
slot. Left on its own for now since it's a genuinely different detection
modality (audio, not camera/network).

---

## 6. Prior-art notes from SquachWatch worth copying

- **Better evil-twin test:** same-SSID-**different-encryption**, not
  same-SSID-different-BSSID (which false-positives on every mesh network).
- **Per-signature confidence grading** (High/Medium/Low) rather than binary
  detect — honest about weak/overlapping signatures.
- **Squachy pet / companion system** — prior art for the Mitama.
- **Dex** — Pokédex-style catalog with rarity/lore/quips/personal records —
  prior art for the Compendium and the Proxima screen.

---

## 7. HAT modules

Robert's list: dual-band, GPS, NFC, sub-GHz.

| HAT        | Status                                                              |
|------------|---------------------------------------------------------------------|
| Dual-band  | = WiFi 2.4/5 GHz scanning. Ports from SquachWatch `SQW_WIFI_5G`.     |
| GPS        | Solved. Ports from `gnss.h`/`gnss.cpp`; unlocks wardrive + RTC-less time. |
| Sub-GHz    | **LoRa = M5 LoRa-E220 (JP) Unit** + `M5-LoRa-E220-JP` lib, with an ESP-NOW fallback mode (ref: CardputerLoRaChat, §17). Hold a generic CC1101-style receiver until that module is known. |
| NFC        | **PN532 over I2C** — `PN532`/`PN532_I2C`/`NfcAdapter` libs, `readPassiveTargetID` (ref: RFID-PN532-i2c-CARDPUTER, §17). Resolved. |

- **RTC:** open low-priority question whether the Cardputer-ADV has an onboard
  RTC — GPS can supply time once that HAT is wired.

---

## 8. Build order (revised for the PaperOS base — see §13 decision)

0. **Fork & rebrand (M0).** Copy PaperOS v1.3 source into `netbuddy/` under
   GPLv2, add the GPLv2 `LICENSE` and a `CREDITS`/`NOTICE` attributing PaperOS,
   AdvanceOS, SquachWatch (+ the §17 references). **FIRST hardware task — the
   ADV keyboard: bump `m5stack/M5Cardputer` to ≥1.1.1 + a matching M5Unified**
   (official ADV/TCA8418 support, MIT); PaperOS pins 1.0.3 which reads the old
   matrix keyboard and reboot-loops on the ADV (see §17). Mind the **GPIO5-HIGH
   SD gotcha**. Then build for the ADV (no-PSRAM already matches),
   confirm it boots, keys work, and the stock apps run; rebrand PaperOS →
   MitamaOS; apply the Cyberpunk palette (swap `config.h` color macros) +
   Rajdhani font (see Visual identity). Everything else lands on top.
1. **Background detection service (M1).** Lift SquachWatch's `DetectionEngine` +
   signature tables in as a background FreeRTOS task using PaperOS's `DispLock`
   so it runs regardless of focused app. A minimal "Proxima" screen lists live
   detections. Watch the heap — detection + WiFi + (later) the LLM all contend
   for the no-PSRAM free heap.
2. **Mitama (M2).** Persistent companion driven by detection events; wire
   PaperOS's `myai` LLM so the Mitama can generate text, with a mood state
   machine (Calm→Curious→Worried→Alarmed) layered on top. Decide when the LLM is
   resident vs streamed (RAM).
3. **Compendium (M3).** Port SquachWatch's Dex/pet as the Compendium + the §5a
   race/Fiend taxonomy and race-complete achievements (Mitama forms).
4. **Runtime themes (M4 — the Option-2 addition).** Port AdvanceOS's Theme
   Manager (`.thm` JSON + SD PNG icons/wallpaper) to replace the compile-time
   palette with runtime SD themes; ship the Cyberpunk palette as the default
   `.thm` so the look is swappable without a rebuild.
5. **Productivity polish (M5).** PaperOS already ships file manager, notes,
   calculator, clock, paint, music, 3D editor, piano, browser, Lua, store. Fill
   gaps from §2 (calendar, to-do, world clock, alarms) as native or Lua apps.
6. **Games (M6).** Native lightweight games (Snake → Solitaire → Chess
   medium-easy → Poker) alongside the existing emulator extension.
7. **HAT expansion (M7).** dual-band, GPS/wardrive, LoRa sub-GHz, then NFC.
8. **Second screen (M8).** Route buddy rendering to a second display.

---

## 9. Open questions (deprioritized until hardware is in hand)

- Sub-GHz exact module (LoRa vs generic CC1101 receiver).
- NFC exact module (likely PN532).
- Second-screen wiring plan.
- Onboard RTC presence (low priority — GPS covers time).
- RAVEN type placement (Acoustic legendary vs folded into Official).

---

## 11. AdvanceOS study (bomberman30/AdvanceOS-for-cardputer, MIT)

Reviewed the full source. It's a mature, **purely-productivity** firmware for the
Cardputer ADV — no attack tooling — and it's MIT (Copyright 2025 bomberman30),
so it's reusable with attribution. It's a much better base for the productivity
+ themes + games pillars than scaffolding from scratch.

### Architecture worth adopting directly

- **App base class** — `GlobalParentClass` with a `Begin() / Loop() / Draw() /
  OnExit()` lifecycle, a `mainOS` back-pointer, a `showTopBar` flag and
  `BackToMainMenu()`. This is essentially MitamaOS's app interface, already
  concrete. Adopt this shape.
- **Data-driven launcher** — a `MenuItem` struct (`name`, `color`, `image`,
  `ItemType` = APP/CATEGORY/FILE_ITEM, `subMenuId`, a `std::function<void()>
  onLaunch` lambda, `HelpText`) plus `SubMenu` (title + indices) and a saved
  menu state. `MenuItemManeger` lets the user **reorder and hide** icons.
  New apps register a `MenuItem`; the kernel doesn't change. Adopt.
- **Theme Manager** — exactly the custom-theme feature wanted. `.thm` files are
  tiny JSON (`BAR_COLOR_1/2`, `BAR_TEXT_COLOR`, `BACKGROUND_COLOR`,
  `ShowWallpaperInMainMenu`); per-app PNG icons (35×35) and a wallpaper
  (240×135) load from SD by matching the app's name. `LoadTheme` /
  `SaveCurrentTheme` / `ResetToDefaultTheme`, editable on-device. Adopt wholesale.

### Productivity suite already built (candidates to port)

File browser, text editor, notes, calculator, loan calculator, resistor calc,
color-code tool, timer, alarm clock (uses deep-sleep), step counter, music
player (MP3/WAV + EQ), music composer (exports WAV), Painter V2 (shapes, bucket,
pixel-art zoom), 3D OBJ renderer, piano, voice recorder, image/GIF/JPEG viewer,
**encrypted password vault**, IR sender + editor, ESP-NOW chat, ESP-RC remote
control, WiFi spectrum, SD-as-USB mass storage, partition manager, hex editor,
screenshot (G0 button). This covers §2's productivity pillar many times over.

### Games & emulator — important caveat

`Emulator.extension` is a **precompiled ESP32-S3 OTA app image** (ESP image magic
`0xE9`), not source. Its strings show it bundles **gnuboy** (GB/GBC), **NES**,
**Arduboy**, and an experimental **SNES** core. Games run by a second OTA app
slot: AdvanceOS writes the ROM path into `Preferences` and boots the emulator
image. The partition layout (`AdvanceOSv2.csv`) carries dual app slots
(`app0` 0x280000 / `app1` 0x180000) + a large SPIFFS for ROMs, and the README's
PMan steps show the partition surgery users must do under some launchers.

So "it has an emulator" = a heavy prebuilt blob run via OTA-partition switching;
the emulator **source is not in the repo**. Reusing it means shipping that blob
and its partition scheme, not porting code. This is separate from MitamaOS's own
planned lightweight native games (Snake → Solitaire → Chess → Poker, §2).

### Architecture mismatches to reconcile

1. **Single-app vs background monitors.** AdvanceOS runs one foreground app at a
   time (`currentApp`, swapped by `ChangeMenu`). MitamaOS's defining feature —
   detection monitors + buddy running *regardless of focus* (§3 kernel) — does
   not exist in AdvanceOS. MitamaOS's event-bus/kernel has to sit *underneath*
   this model as a background service, with the buddy and the SquachWatch
   detection engine ticking independent of whichever app is focused.
2. **No back-stack.** AdvanceOS is also ESC→main-menu only — it shares the exact
   gap §3 flagged. The Go-button back-stack is still net-new work either way.
3. **Library base.** AdvanceOS uses `M5Cardputer` directly + a custom
   `NewKeyboardHandle`; §1 picked `M5Unified`. Minor reconciliation; AdvanceOS's
   choice is proven on this exact board, so M5Cardputer-direct may win.
4. **RTC.** AdvanceOS uses `ESP32Time` (software RTC) — answers §9's open RTC
   question: no hardware RTC needed, GPS/NTP/manual set seeds it.

### Strategic decision this raises (needs Robert's call)

Three ways to relate MitamaOS to AdvanceOS:
- **A — Borrow patterns only.** Keep MitamaOS's own codebase; copy the app-class,
  launcher and theme-manager *designs*; port individual apps as needed.
- **B — Fork AdvanceOS as the base.** Start from AdvanceOS, add the buddy +
  SquachWatch detection engine as a background service and the Dex/Proxima
  app on top, reframe as MitamaOS. Fastest to a feature-rich OS; inherits the
  whole productivity suite + themes + emulator immediately.
- **C — Hybrid.** Fork AdvanceOS for the OS shell/apps/themes, but lift
  SquachWatch's detection engine in wholesale (per §5) and run it under a small
  background-service layer added to AdvanceOS's loop.

Leaning **C**: AdvanceOS gives the richest, already-working OS shell + apps +
themes + emulator, SquachWatch gives the best detection engine + Dex/pet, and
the only genuinely new engineering is (a) a background-service layer so
detection runs off-focus, (b) the Go-button back-stack, and (c) the buddy/Dex
rework. Both sources are MIT and attributable.

---

## 12. PaperOS study (Artem76228/PaperOS, GPLv2)

Reviewed the full source (shipped in `PaperOS_v1.3.zip` — complete, not a stub).
It is the most architecturally capable of the three, and the only one that is
**GPLv2** rather than MIT. That license is the single most important fact here
(see §13). Target board in the repo is the **ESP32-S3FN8, 8MB flash, NO PSRAM** —
RAM is extremely tight (the whole v1.3 changelog is about clawing back 40–70KB);
the Cardputer ADV's PSRAM would relax this considerably.

### What PaperOS has that the others don't

- **On-device offline LLM ("myAI").** A byte-level GPT (vocab 256, 192-dim,
  12 layers, 6 heads, **multi-query attention**, 256-token context, ~125
  tensors, ~4.7MB weights) running natively on the Cardputer, streaming
  `model.bin` from SD per token. Ships the **full training pipeline**
  (`train_stories.py`, TinyStories) and the C inference engine
  (`myai_engine.h`). **This is the game-changer for the buddy** — it means the
  companion can actually *generate language*, not just cycle mood faces, and the
  trainer means its personality can be fine-tuned.
- **Lua 5.4 app engine + app Store.** Lua is fully vendored; apps are Lua
  scripts with a rich hardware API (`os/io/net/gpio/disp/key/sound/timer/ui/
  store/json/bit/i2c/term`) installed over WiFi from a GitHub-backed Store into
  `/lua/`. Best extensibility model of the three — detection reactions, mini
  apps, and community content could all be Lua.
- **True multitasking with a display lock (`DispLock`).** PaperOS already runs a
  background Lua REPL task that draws safely alongside the foreground app via a
  display mutex. **This is exactly the mechanism MitamaOS's background
  detection-off-focus requirement needs** (§3) — and it's the thing AdvanceOS
  lacks entirely.
- **Polished OS core** — `App`/`AppManager` singleton, `Launcher`, a Win95-style
  UI toolkit (`OsUI` with 3D bevels), `WiFiManager`, `mem_guard`, a BIOS POST
  screen (`bios_post.h`) and a Win95 boot animation.
- **Text-mode browser** with a real small HTML/CSS layout engine (parses
  `<style>`, classes/ids → color/bold/alignment, lists, tables), 3 tabs,
  history, bookmarks, find-in-page.
- Paint, 3D OBJ editor, Music (WAV/MP3), Notes, Calculator, Clock, Piano,
  Settings (with WiFi scanner), SysInfo.

### Where it's weaker than AdvanceOS

- **Theming is compile-time**, not runtime. The whole OS reskins from RGB565
  macros in `config.h` — you recompile to change it. AdvanceOS's SD-loaded
  `.thm` + PNG runtime themes (which Robert explicitly values) are more
  flexible. This is the one place AdvanceOS clearly wins.
- **No-PSRAM target** → brutal RAM budget. Less of an issue on the ADV.
- **Emulator is still a prebuilt `.extension` blob** (`PaperEMU.extension` /
  `EmulatorV3.4.extension` — note the latter is *the same file* AdvanceOS ships;
  the two projects share emulator extensions). Same caveat as §11.

---

## 13. Three-way comparison & the license fork in the road

| Capability                | SquachWatch (MIT) | AdvanceOS (MIT) | PaperOS (GPLv2) |
|---------------------------|:---:|:---:|:---:|
| Detection engine + Dex/pet | ★ best | — | — |
| Background monitors off-focus | (its whole model) | ✗ single-app | ★ DispLock multitask |
| Runtime SD themes          | — | ★ best | ✗ compile-time |
| Productivity app suite     | — | ★ huge | ✓ good |
| On-device AI buddy         | — | — | ★ only one |
| Lua scripting + app store  | — | — | ★ only one |
| Emulator / games           | — | ✓ blob | ✓ blob (shared) |
| License                    | MIT | MIT | **GPLv2** |

**The license is the decision's hinge.** GPLv2 is copyleft: any firmware that
incorporates PaperOS source must itself be released under GPLv2 with full
source. GPLv2 *can* legally absorb the MIT SquachWatch and AdvanceOS code, so a
combined MitamaOS built on PaperOS is fine — but **the whole project then
becomes GPLv2**, not MIT. If staying MIT/permissive matters to Robert, PaperOS
source is off the table (its *ideas* — a tiny on-device LLM, a Lua app layer —
could still be reimplemented independently, but that's real work, not a port).

### Revised strategy options (supersedes §11's A/B/C)

- **Option 1 — MIT stack (AdvanceOS + SquachWatch).** Fork AdvanceOS for the
  shell/apps/runtime-themes, add SquachWatch's detection engine, build the
  background-service layer and buddy ourselves. Stays MIT. No AI buddy, no Lua
  (unless we add them from scratch). This is §11's option C.
- **Option 2 — GPL stack (PaperOS core + SquachWatch + AdvanceOS themes).**
  Fork PaperOS as the OS core (its multitask/DispLock already solves
  background-off-focus; its myAI gives the buddy a real voice; its Lua/Store
  gives extensibility), lift SquachWatch's detection engine in as a background
  service, and port AdvanceOS's runtime Theme Manager on top to regain custom
  themes. Most capable result by far. **Becomes GPLv2.** Most integration work
  (three codebases, two styles).
- **Option 3 — PaperOS core, skip the themes port initially.** Option 2 minus
  the AdvanceOS theme-manager port; live with compile-time themes for v1, add
  runtime themes later. Smaller first step toward the most capable base.

**Lean: Option 2 (or 3 as its first milestone).** PaperOS already solved the two
hardest problems in the original MitamaOS plan — running detection in the
background without freezing the UI (DispLock multitasking) and giving the buddy
an actual voice (on-device LLM) — and its Lua layer makes everything after that
cheaper. The cost is committing the project to GPLv2. That trade is Robert's to
make, which is the one thing worth deciding before any code moves.

### DECISION LOCKED (2026-10-05)

- **License:** GPLv2. Robert confirmed open-source is fine. MitamaOS is a GPLv2
  project; the AdvanceOS/SquachWatch MIT code it absorbs stays attributed but
  the combined work ships under GPLv2 with full source.
- **Base:** **Option 2**, delivered **Option 3 first** — fork PaperOS as the OS
  core, lift SquachWatch's detection engine in as a background service, add
  AdvanceOS's runtime Theme Manager in a later milestone.
- **Hardware:** stock Cardputer ADV has **NO PSRAM** — the *same class of board*
  PaperOS already targets (ESP32-S3, 8MB flash, no PSRAM). PaperOS's entire v1.3
  RAM-budget effort (WiFi-off-by-default, trimmed Lua stdlib, model streamed
  from SD per token) transfers directly. The RAM ceiling is real and is the
  governing constraint for the buddy LLM and background detection running
  together — both compete for the same ~free heap.

---

## 15. UI & interaction decisions

From the UI mockups (6 screens: boot, launcher, environment, compendium, fiend
alert, toast). Rendered at the real 240×135, 16px top bar + 14px status bar.

### Locked

- **Mitama is always on screen** (system layer, not a screen you visit). It
  lives on the **status bar** (bottom 14px) as a small magatama + a one-line
  ticker, present in every app; the full sprite appears on the Proxima
  screen and in toasts. Every app reserves the status bar for it.
- **Launcher = icon grid** (3-wide tiles, neon line icons, per-app accent
  color; selected tile glows yellow with a red corner-tick).
- **Compendium = split-view** (race-grouped list + entry card side by side).
- **Top bar schema:** left = mood-magatama + context title; right = clock,
  Wi-Fi, battery glyph (no numeric %, it overflowed).
- **Status-bar ticker = single line, marquee-scrolls on device** when longer
  than the bar. Long Mitama dialogue goes in a toast/speech strip, never the bar.
- **Readability rule — glitch never over data.** Scanline/glitch decoration is
  confined to titles and empty bands; facts and critical text always get a
  solid dark backing plate. (Enforces "snark yields to clarity".)
- **Type scale (LOCKED):** Rajdhani, device px —
  Display 20/700 (boot wordmark), Title 14/700, Label 10/600 caps,
  Body 9/400–500, Small 8/500, Numeric 11/600 (colored), Badge 7.5/600.
  Bake as M5GFX bitmap fonts at 8/10/14 + 20px. 8px is the floor (1-bit, no
  AA) — Small/Badge verified on hardware at M0; bump to 9 if muddy.

### Icon system (LOCKED)

Visual language: **esoteric sigils**, not literal objects — occult / alchemical /
sacred-geometry forms sharing the Proxima glyph's core-and-ring grammar, so
the set reads as one arcane family. 2px neon stroke on a 24-unit grid, each in
its own per-app accent color; selected launcher tiles override to yellow.

- **App sigils:** Compendium = magatama sealed in a dashed ring (the crest) ·
  Proxima = core + dashed orbit + cardinal ticks · Files = warded
  archive-diamond (cardinal nodes + inscribed lines) · Paint = the squared
  circle (▢○△, alchemical creation) · Music = cymatic sound-mandala · Games =
  **pentagram** + center point · Lua = `>_` bound in a hexagon · Notes =
  **grimoire** (spine + clasp + circle-and-triangle seal) · Browser = astrolabe ·
  Settings = orrery (nested rings + orbiting nodes).
- **Per-app accents:** Compendium yellow · Proxima green · Files cyan ·
  Paint magenta · Music purple · Games green · Lua cyan · Notes yellow ·
  Browser cyan · Settings dim/violet. (Refinable; not all unique.)
- **Avoided:** Star of David (→ pentagram), cross/Bible glyph (→ grimoire seal),
  the eye on anything but Vile.

### Race glyphs (LOCKED)

One sigil per demon race, used on Compendium cards, list rows, and Mitama
reaction flavor; readable at badge size, evocative not scary:
Vile = shield + eye (the watchers) · Night = camera aperture · Fairy = winged
location-pin + sparkle · Avian = drone quad top-view · Herald = beacon
broadcast rings · Foul = card + hook (skimmer) · Jaki = waveform in a ring ·
**Fiend = corrupted-Mitama** (the magatama pixel-sampled into cyan data blocks
with the **red bones behind each pixel**; dropped pixels reveal the bones — the
threats are a corruption of the companion itself).

### Sound set (LOCKED)

Character: curt, synthetic, a little smug — the Mitama **intones** (portamento +
vibrato + legato), it does not beep. Up = good, down = disappointment,
alternating = alarm; only the alarm is harsh so the OS isn't noisy. All cues
obey a UI-sound on/off toggle + volume, and audio never gates the UI.

**Hardware path:** glides/vibrato exceed plain `M5.Speaker.tone()`, so cues
render as **short synthesized PCM** (ESP32 generates the waveform samples). Three
player types: *melody* (discrete sung notes), *phrase* (one continuous glide),
*grunt* (square). Pitches in Hz, durations in s.

| Cue | When | Type / wave | Definition |
|-----|------|-------------|------------|
| Boot song | power-on identity | melody / triangle | C5 E5 G5 F#5 E5 D5 G5 A5 (rest) E5 G5→ C6-held; a real tune w/ a chromatic dip + octave resolve |
| Nav blip | cursor move | phrase / sine | 1180→1030, 0.14s, no vib |
| Select | confirm / open | phrase / sine | 620→940, 0.2s — quick functional |
| Approval | "good call" (Smug) | melody / sine | a curt **"mhmm"**: G#4(.13) C5(.36), warm, low |
| Disappointment | the sigh | phrase / sine | 784→660→523→392, 1.05s, deep vibrato |
| Hmph | dismissive grunt | grunt / square | 210→155, 0.11s (**kept — do not change**) |
| Notice | toast appears | melody / square | **MGS "!" alert**: B5(.07) E6(.16), sharp/metallic |
| Warning | watchlist / caution | phrase / sawtooth | 587→494→659→587, 0.72s |
| Alarm | Fiend / full-screen | phrase / square | siren 880⇄440 glide ×3, 1.1s — the only harsh cue |
| New entry | demon logged | melody / triangle | sparkly run E5 G5 B5 A5 C6 (rest) E6 D6 E6-held, 1.2s |

### Idle cadence & temperament (LOCKED)

Three inputs feed a running **temperament**; two scalars come out and bend
cadence, tone, visuals, and sound. **Time of day drains Energy; session fatigue
drains Patience.** They stack (4h+ after 9pm = tired *and* short-tempered).

- **Time → Energy:** 6–11 Fresh (chipper) · 11–21 Default · **21–01 Sleepy**
  (yawns, dim aura, slow bob) · 01–06 Drowsy/cranky (wants you in bed).
- **Fatigue → Patience** (continuous uptime): <2h Normal · 2–4h Settled ·
  **≥4h Weary** (terser, barbs about you still being here) · ≥6h Exasperated.

**What temperament changes** (reuses locked systems):
- **Idle cadence** — base: quiet ≥2 min, then ~1 unprompted remark every 3–6 min
  at Sass 2. Low Energy ×2–3 the gap (dozing); low Patience keeps frequency but
  sharpens tone. **Annoyed = meaner, not chattier** (never nags more).
- **Visuals** (§14) — Sleepy: aura ~60% dim, slow bob, slight droop; late-night
  idle → the Mitama **dozes**, a keypress wakes it with a grumble. Annoyed:
  sharper jitter, more red-bones flicker.
- **Sound** (§15) — Sleepy: cues ~2 semitones down + slightly slower. Annoyed:
  snappier.
- **Lines** — line bank gets temperament-tagged variants (fresh/default/sleepy/
  weary), chosen by current state.

**Sass dial (user setting, default = 2):**

| Lvl | Name | Idle freq | Bite |
|-----|------|-----------|------|
| 0 | Mute Muse | never (facts only) | none — accessibility |
| 1 | Dry | ~10–15 min | minimal |
| **2** | **Default** | ~3–6 min | Clippy×GLaDOS balance |
| 3 | Insufferable | ~1–2 min | maximum |

**Invariants:** security-alert **facts** are never rate-limited or suppressed,
even at Sass 0; identical events collapse (same demon within ~60s → no repeat
flavor, counts still update); nothing here gates the UI.

**Clock dependency:** time-based behavior needs the RTC (ESP32Time seeded by
GPS/NTP/manual). With no set clock and no GPS/WiFi, fall back to **fatigue-only**
(uptime works from boot) until the clock is set.

### In-universe naming (LOCKED)

Register is split by **layer**, not sprinkled — that keeps themed names from
clashing with plain tools in the same grid:

- **Companion layer (named / themed):** the **Mitama** (buddy) · **Proxima**
  (its home screen — the Go-button destination, *not* a launcher tile; replaces
  the old "Environment", which read too vast — Proxima = "the nearest", punchy,
  local, techno-esoteric) · the **Compendium** (the bestiary). Compendium stays
  reachable as its own named tile and from Proxima's recent-sightings.
- **Tool grid (plain):** Files · Paint · Music · Games · Lua · Browser ·
  Settings — generic apps, generic names. Quiet esoteric **epithets** appear
  only in help/subtitle text (the Archive, Sigilcraft, Resonance, the Rites,
  Incantations, the Astrolabe, Attunement).
- **One themed primary kept in the grid:** **Notes → Grimoire** (its icon is
  already a grimoire, and it still reads plainly as "my notes").
- **System vocabulary (themed, already locked):** COMP (device), races, Fiends.

Logic: the device's signature *systems* get names (Pip-Boy / Codec style);
commodity tools stay commodity. The layer split, not a coat of paint, carries
the theme.

### Accessibility & comfort (LOCKED)

Settings, available from day one. The aesthetic is intense by design, so these
let people dial it down.

- **Visual:** High contrast (brighter text, near-white secondary, pure-black bg,
  thicker rules) · Large text (bump the type tier where layouts allow — needs a
  larger font bake) · Reduce glitch (kills scanlines/chromatic-split/flicker/FX,
  keeps palette + layout). All default off.
- **Motion:** Reduce motion (static Mitama, no bob/jitter/aura-pulse/ring-spin/
  transitions — vestibular). Default off.
- **Audio:** UI sounds (on) · **Alert sounds — separate toggle** (on) so muting
  the Mitama never silences a security alarm · Volume (~60%).
- **Mitama / cognitive:** Sass dial 0–3 (Aspect 4; 0 = facts-only low-distraction)
  · Ticker speed (slow/normal/fast) · Toast dwell (short/normal/long).
- **Calm Mode** — one-tap preset bundling Reduce motion + Reduce glitch + Sass 1
  + steady (non-flashing) alerts.

**Hard invariants (not settings):**
1. **Photosensitivity:** nothing ever flashes faster than **3 flashes/sec**
   (WCAG), even at full FX — the alarm "surge" is a slow/steady pulse, never a
   strobe. Hard rule given the glitch aesthetic.
2. **Safety never hidden:** no comfort setting suppresses a security alert's
   facts — Sass 0 still shows them, Reduce-glitch still renders the alert, and
   only *Alert sounds* can quiet the alarm tone.

**Colorblind support → handled by Themes.** A colorblind-safe palette is just
another `.thm` served by the theme manager (M4); the race accent remap lives
there, not as its own toggle.

### Logo lockup (LOCKED)

Built entirely from the magatama crest + Rajdhani over the red bones.
- **Primary (horizontal):** the **sealed crest** (magatama inside a dashed
  yellow seal-ring with a dim-red bones ring) + "Mitama**OS**" wordmark, "OS" in
  cyber-yellow. For landing/nav/headers. Stacked variant for boot/splash.
- **App / flash icon:** the **sealed crest on a red-bones tile** (icon A) — no
  words. The red-bones field makes it unmistakably MitamaOS at a glance and in
  the M5Burner/launcher grid.

### Proposed (confirm before M0)

- **Severity → presentation** (ties to the §3 event bus):
  Info → ticker only · Notice → ticker + soft tone · Warning → toast + tone,
  Mitama worried · Alert/Fiend → full-screen takeover + alarm + red-bones surge.
- **Go/Mitama key:** a dedicated key jumps to Proxima from anywhere and
  back (the §3 back-stack). Exact Cardputer key TBD at M0 (keymap check);
  ESC = back, Enter = select already.

### Still to lock (see §16)

Type scale, icon/race-glyph set, sound set, idle cadence + sass dial defaults,
in-universe app naming, accessibility toggles, logo lockup.

## 16. Remaining design aspects to lock (pre-code checklist)

1. ~~**Type scale**~~ — ✓ LOCKED (see §15).
2. ~~**Icon system**~~ — ✓ LOCKED (see §15 "Icon system").
3. ~~**Sound set**~~ — ✓ LOCKED (see §15 "Sound set").
   (boot melody, mhmm approval, MGS-style notice, kept Hmph, PCM synthesis path)
4. ~~**Idle cadence & sass dial**~~ — ✓ LOCKED (see §15 "Idle cadence &
   temperament"): Energy/Patience model, time-of-day + fatigue tiers, sass dial
   default 2, visual/audio drift, clock-dependency fallback.
5. ~~**In-universe naming**~~ — ✓ LOCKED (see §15 "In-universe naming"):
   layer-split register; Proxima home screen; Grimoire kept; tools plain.
6. ~~**Accessibility / comfort toggles**~~ — ✓ LOCKED (see §15): visual/motion/
   audio/cognitive toggles, Calm Mode preset, flash-safety + safety-never-hidden
   invariants, colorblind via Themes.
7. ~~**Logo lockup**~~ — ✓ LOCKED (see §15 "Logo lockup"): sealed-crest
   horizontal wordmark; crest-on-red-bones app icon.

## 14. The Mitama — personality & faceless expression

### Persona

A **Clippy × GLaDOS** amalgamation: snarky, sassy, deadpan, and *genuinely
helpful underneath it*. Clippy supplies the eager, unsolicited, pops-up-to-help
energy; GLaDOS supplies the dry contempt, backhanded compliments, faux-concern,
and clinical detachment. The comedy is the **tension** between the two — a
helper that insists on helping you while making it clear it finds you a little
tedious, and that is quietly, competently right every time.

Tone rules:
- Snark is aimed *playfully* at the user and *witheringly* at threats — never
  cruel, never making the user feel unsafe (keeps the §Theme "non-intimidating"
  rule: the menace is theatrical, like GLaDOS, not real).
- **Snark always yields to clarity when it matters.** On a real security event
  (deauth flood, evil-twin), the actionable facts — what, where, how bad — are
  never buried under a bit. One dry line, then the crisp data.
- Rare, earned sincerity is the payoff. A genuine "…that was a good call" lands
  precisely *because* it's surrounded by sarcasm.

### Visual design — LOCKED

From the design study (concept sheet, mockups to be turned into real pixel-art
sprites later):

- **Base form: Soul** — a round core carrying a single magatama sigil and an
  aura ring whose brightness is the "optic." (Chosen over Noh mask, Optic core,
  Ofuda.)
- **Sigil: the magatama** — a **solid open comma** (fat round head, long taper,
  **no drilled eye** — the eye read as an ouroboros). This is the Mitama's crest
  everywhere and the **app/boot icon**.
- **Mood accent colors:** calm = yellow; curious = cyan; **smug/approve = neon
  green**; passive-aggressive = magenta; alarmed/hostile = red-bones bleed.
- **Outfits (approved, expandable):** Netrunner (default), Tinfoil (paranoia /
  SquachWatch nod), Oni (soft horns), Kitsune, Omamori, Braindance (glitch).
  Governed by the MAY/NEVER rules below — never a face, never replacing the
  tilt+aura+sigil expression system.

### The no-face doctrine

GLaDOS conveys a wide emotional range with an unmoving chassis and one optic. A
**Mitama is an expressionless mask by design**, so we lean all the way in:
**no facial features ever.** Emotion is carried by everything *around* the face,
across these channels:

1. **Language (primary).** Word choice, cadence, deadpan timing. The GLaDOS
   channel. Backhanded praise, mock-cheer, clinical understatement, the
   well-placed "...".
2. **Text kinetics.** *How* the line appears: typing speed, a held pause before
   a punchline, stutter/glitch on alarm, self-correction and `[REDACTED]`
   strike-throughs (GLaDOS's "ignore that"), teletype cadence, caps for the one
   word that matters.
3. **The mask's body language.** Tilt (a skeptical head-cock), bob height and
   speed (calm vs agitated), a sharp recoil/snap (alarm), slow droop (boredom/
   disappointment), a slow orbit (thinking), going still and *brightening* its
   aura (locking attention on you). This is GLaDOS's chassis/optic motion.
4. **Aura & color (ties to Visual identity §).** Glow intensity is the optic's
   "brightness." Calm = steady yellow/cyan; intrigued = magenta; disapproving/
   alarmed = the **red bones bleed through** and the aura flickers. Mood is lit,
   not drawn on a face.
5. **Sound.** Small expressive UI tones, not speech: a bright chirp (approval),
   a flat descending tone (disappointment), a clipped double-blip (alarm), a
   dismissive "hmph" sting. Clippy's attention-blip, GLaDOS's cadence.
6. **Behavior & timing.** *When* it speaks is characterization: the Clippy
   intrusion (unsolicited audits — "It looks like you're joining open WiFi…"),
   the delayed deadpan reaction, and the **silent treatment** (it can withhold
   comment pointedly). Choosing not to speak is an expression too.

### Emotional range → expression recipe

Drives the planned mood machine (expanded from Calm→Curious→Worried→Alarmed):

| Mood | Language | Kinetics | Mask motion | Aura / color | Sound |
|------|----------|----------|-------------|--------------|-------|
| Calm/bored | understated, a little bored | slow, even | gentle slow bob | steady dim yellow | occasional soft blip |
| Smug/approving | backhanded praise | normal, a beat before the twist | calm pose, small settle | **neon green** glow | rising chirp |
| Curious/intrigued | leading questions | slight speed-up | head-cock tilt | magenta tick-up | short two-tone |
| Passive-aggressive | faux-concern | the pointed "..." | slow orbit | magenta, mild red seep | flat tone |
| Contemptuous (at threats) | clipped, cutting | fast, hard stops | still, aura hardens | red bones surge | low sting |
| Alarmed/urgent | cold + clear, *not* panicked | stutter/glitch then crisp facts | sharp recoil, then still | red bleed + flicker | clipped double-blip |
| Sincere (rare) | plain, no bit | slow, unglitched | settle, steady | warm steady cyan | single clean tone |

### Technical reality (important — don't over-trust the LLM)

**DECISION (locked):** the line bank is the product; the LLM is an optional,
**async, best-effort flavor layer** that enhances pre-written mood phrases. It
is never on the critical path and can be toggled fully off with no loss of
function.

The on-device model (§12) is a tiny TinyStories-class GPT. It **cannot** be
relied on to free-generate reliably witty, in-character, factually-correct
GLaDOS prose. It is also **slow** — PaperOS streams `model.bin` from SD every
token, so generation is SD-read-bound and nowhere near real-time, and it
contends with detection + WiFi for the no-PSRAM heap. Practical consequences:

- LLM output is **async and non-blocking** — it drafts a line in the background
  and the Mitama may deliver it a beat later, or not at all. It **never** gates
  the UI or a security alert.
- Alerts and any factual payload always come from the **line bank**, rendered
  immediately; the LLM never touches them.
- Good LLM jobs: idle musings, lightly re-wording a bank line between events,
  ambient chatter while you sit on a menu. Low stakes, no deadline, no facts.

So the persona is **authored, not emergent**:

- **Backbone: a curated line bank + template engine.** Hand-written lines keyed
  by `event × mood` (boot, idle, new-device-by-race, deauth, evil-twin,
  compendium milestone, user action, etc.), with slots for live facts
  (`{ssid}`, `{count}`, `{race}`). This guarantees voice and correctness and
  costs almost no RAM/compute — it works even with the LLM disabled.
- **Seasoning: the LLM for variation/filler** where its limits are acceptable
  (idle musings, re-wording a bank line, ambient chatter) — never for the
  factual payload of a security alert.
- **Optional: fine-tune the voice.** `train_stories.py` (§12) could be pointed
  at an in-character corpus to bias the tiny model toward the Mitama's tone.
  Experimental; the line bank stands on its own regardless.
- **Guardrails:** the snark layer never blocks or delays the information;
  alert facts render even if the persona layer fails; tone is config-dial-able
  (a "sass level", down to "just the facts") so the bit never traps the user.

### Sample voice (placeholder lines, to calibrate — not final copy)

- Boot: *"Oh good, you're back. I kept the network alive while you were gone. You're welcome."*
- Idle/calm: *"Nothing is currently trying to ruin your day. Enjoy the novelty."*
- New Tracker (Fairy): *"Something new is following you around. A tracker. You must be thrilled."*
- Deauth (Fiend): *"Deauth flood. Someone wants you off this network. Badly."* → then: ch {chan} · {bssid} · {count}/s.
- Evil-twin: *"There are two of this network now. One is lying to you. [pause] It's not the one I like."*
- Clippy intrusion: *"It looks like you're joining an open network. Would you like me to disapprove silently, or with commentary?"*
- Earned sincerity: *"...that was genuinely the right move. Don't make it weird."*
- Compendium complete (a race): *"You've logged every Tracker in existence. A full set of things that watch you. Congratulations, I suppose."*

---

## 17. References & prior art — Cardputer firmware survey

Surveyed the community firmware list (ru84r8/Cardputer-firmware-list) and read
the relevant source. Findings below, with credit. **License note:** entries are
used as *reference/facts* unless a license check clears porting; we must not copy
GPLv3 code into this GPLv2 project — reimplement from the documented facts.

### CRITICAL — the ADV is not the original Cardputer (M0 risk)

From **MicroHydra** (echo-lalia, GPLv3) which ships an explicit `CARDPUTER_ADV`
device profile:
- **Keyboard: TCA8418 I2C keypad controller @ 0x34** — *not* the original
  Cardputer's GPIO matrix. PaperOS/AdvanceOS read `M5Cardputer.Keyboard`
  (matrix), which **will not work on the ADV**. M0 must add a TCA8418 driver
  (register map: CFG 0x01, INT_STAT 0x02, KEY_LCK_EC 0x03 [low nibble = event
  count], KEY_STAT/event FIFO; a key event byte = bit7 press/release + bits0-6
  keycode). Reimplement in C++ from the datasheet/these facts.
- **IMU: BMI270** (I2C) — the ADV has motion sensing (tilt/shake for the Mitama).
- **Confirmed ADV pin map** (matches PaperOS's SD pins — good): Display ST7789
  240×135 SPI1 — CS 37, DC 34, MOSI 35, SCK 36, RST 33, BL 38, 40MHz, no MISO.
  SD — CS 12, MISO 39, MOSI 14, SCK 40. I2S speaker — SCK 41, SD 42, WS 43.
  PDM mic, IR blaster, WiFi+BT, G0 on pin 0. No PSRAM.

This is the single most important finding: **the ADV needs TCA8418 keyboard
handling** before anything works on real hardware — resolved cleanly below.

**BEST solution — the official M5Cardputer library already supports the ADV
(MIT).** `m5stack/M5Cardputer` **v1.1.1** README: "library for M5Cardputer and
**M5Cardputer-ADV**"; `Keyboard.cpp` branches on
`board_type == m5::board_t::board_M5CardputerADV` to a built-in
`TCA8418KeyboardReader` (uses an M5-adapted Adafruit_TCA8418, `matrix(7,8)`,
INT pin 11). **So `M5Cardputer.Keyboard` "just works" on the ADV with a current
lib — no custom driver, MIT-clean.** The real M0 task is a **version bump**:
PaperOS pins `m5stack/M5Cardputer @ ^1.0.3` (pre-ADV); M0 moves to **≥1.1.1**
with a matching recent **M5Unified** (which also provides the ADV's **BMI270
IMU** via `M5.Imu` — free tilt/shake for the Mitama). Keep the GPIO5 SD gotcha
below in mind regardless.

Still useful as corroboration / fallback — **bmorcelli's Launcher** (its
`unified_inputs` branch + `boards/m5stack-cardputer/CardputerADV.md`), the same
recipe at the register level:
- Library: **`adafruit/Adafruit TCA8418 @ ^1.0.1`** (no hand-written driver).
- **Keyboard I2C is its own bus: SDA=GPIO8, SCL=GPIO9, INT=GPIO11**, TCA8418 @
  0x34, 7×8 matrix. **Interrupt is unreliable — poll at ~100ms** instead.
- Build flags: `-DCARDPUTER_ADV=1 -DTCA8418_INT_PIN=11 -DTCA8418_I2C_ADDR=0x34
  -DTCA8418_SDA_PIN=8 -DTCA8418_SCL_PIN=9`.
- **GOTCHA #1 (reboot loop):** the original GPIO/matrix keyboard init on the ADV
  → endless reboot after "Using config.conf". Must branch to TCA8418 by compile
  flag. (This is exactly what PaperOS/AdvanceOS would do out of the box.)
- **GOTCHA #2 (SD won't mount):** the extra I2C sensors interfere with GPIO5
  (SPI CS). **Set GPIO5 HIGH during init** or the SD card never mounts.
- I2C scan on the ADV shows **0x18** (accel), **0x34** (keyboard), **0x69**
  (gyro/IMU — matches the BMI270). Bootloader mode: hold GPIO0 + reset.
- Launcher's ADV build runs at ~25% RAM / ~27% flash — healthy baseline headroom.
Licensing: use `Adafruit_TCA8418` (permissive) + these documented pins/flags
(facts); don't copy Launcher's GPL source.

### Confirmed build facts
- **Audio PCM path:** `M5Cardputer.Speaker.playRaw(buf, n, sampleRate)` (from
  cardputer-nofrendo) — exactly the API for our §15 sung-PCM cues. Confirmed.
- **Onboard IR TX = GPIO44**, via the `IRremote` library (`IrSender.setSendPin(44)`)
  — from **M5CardRemote** (VolosR). Also `WORLD_IR_CODES.h` in m5stick-nemo for
  TV-B-Gone-style codes.
- **M5Cardputer examples** (m5stack, MIT): display / sdcard / mic_wav_record /
  ir_nec / keyboard / buzzer / REPL — the foundational API reference (original
  matrix keyboard: `Keyboard.isChange()/isPressed()/keysState()`).

### Resolves open hardware items
- **NFC → PN532 over I2C.** From **RFID-PN532-i2c-CARDPUTER** (Jojorel): the
  Seeed/Adafruit `PN532`/`PN532_I2C`/`NfcAdapter` libs, `pn532.begin();
  pn532.wakeup(); nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &len)`.
  Resolves the §7 "no NFC code" unknown.
- **Sub-GHz/LoRa → M5 LoRa-E220 (JP) Unit** + `M5-LoRa-E220-JP` library, and an
  **ESP-NOW** fallback mode (no radio needed). From **CardputerLoRaChat**
  (nonik0) — also a clean tabbed UI with per-user signal-strength display (good
  prior art for Proxima). Refines the §7 LoRa path.
- **Spectrum/FFT:** a fixed-point integer FFT (`fix_fft.h` / Fixed15FFT) from
  **m5Cardputer_audiospectrum** (cyberwisk) — no-FPU-friendly, reusable for a
  Proxima channel-activity / music visual.

### Games pillar — the open-emulator decision
**cardputer-nofrendo** (lxyMiao) ports **arduino-nofrendo** (moononournation),
itself the classic **Nofrendo** NES core — an *open* NES emulator on the
Cardputer (keys: arrows, k=A, l=B; NES frame → 240×135 line buffer; audio via
`playRaw`). **m5cardputer_doom** (romalik) is an ESP-IDF Doom port bundling
LovyanGFX. These give an **open path off the closed `.extension` blob** that
AdvanceOS/PaperOS use.
- **Decision (reaffirmed):** v1 keeps native games + the existing blob;
  **plan to bundle an open emulator (Nofrendo) so the GPLv2 image is fully
  source-available.** Verify the Nofrendo core's license before bundling.

### Distribution / partitions / flashing
- **M5Stick-Launcher** (bmorcelli): `partitioner.h` (dump/restore/crawler = the
  "PMan" AdvanceOS references), `installFAT_OTA()`, per-flash-size partition
  CSVs, OTA install — the install + OTA-slot mechanism our Games path rides on.
- **Launcher catalog** (bmorcelli.github.io/Launcher/catalog.html): a
  browser-based **app store + WebSerial web-flasher** (esptool-js). It pulls the
  firmware list from `api.launcherhub.net` (which mirrors the **M5Burner CDN** —
  `m5burner-cdn.m5stack.com/firmware|cover`), filtered by device category
  ("cardputer"), each entry = name/author/category/description/versions + cover,
  flashed over WebSerial with a chip-family check. Two takeaways for MitamaOS:
  1. **Distribution:** publish MitamaOS to M5Burner / the launcherhub catalog so
     users one-click web-flash it (and the **Launcher already supports the ADV**,
     so it's a real target). 2. **Our own flasher:** add a "Flash MitamaOS"
     button to our Pages site using the same esptool-js/WebSerial approach (like
     PaperOS's esphome.io link) pointing at our `.bin`.

### Architecture references (patterns, not code)
- **Bruce** (pr3y): a `src/core` (config / display / `bus_HAL` / `configPins`) +
  `src/modules` structure with a `boards/<name>/pins_arduino.h` + JSON HAL —
  a clean multi-module-with-HAL pattern.
- **m5stick-nemo** (n0xa): a dead-simple `struct MENU` + `drawmenu(MENU[], n)`
  scrolling menu — minimal menu prior art.
- **MicroHydra** (echo-lalia): per-device profiles + an `apps/` module pattern.

### Offensive firmwares — context only, nothing ported
**Bruce**, **ESP32Marauder**, **Evil-M5Core2**, **evil-portal**, and nemo's
attack modules are the offensive tools MitamaOS is the *defensive inverse* of.
We port **no** attack code. Their *techniques* (how deauth / evil-portal / BLE
spam are performed) only inform our passive **detection signatures** — which
SquachWatch already encodes. They also target the original Cardputer
(`ARDUINO_M5STACK_CARDPUTER`), not the ADV.

### Where the official docs / more threads live
- **Reachable from here (GitHub, MIT):** `m5stack/M5Cardputer` (v1.1.1, ADV +
  TCA8418 reader), `m5stack/M5Unified` (board detection, `M5.Imu` BMI270),
  `m5stack/M5GFX` (display/canvas/fonts — our whole UI API), `m5stack/M5-LoRa-
  E220-JP`, `adafruit/Adafruit_TCA8418`. These are the authoritative source.
- **Blocked by this container's egress proxy** (403), but the user can open:
  `docs.m5stack.com` (Cardputer ADV product page, pinmap, schematic PDF),
  `bmorcelli.github.io/Launcher` (catalog/flasher), `api.launcherhub.net`,
  `cardputer.wiki`, and the r/CardPuter subreddit + M5 Discord (from the list).
- **M5 examples** (`m5stack/M5Cardputer/examples`): display, sdcard, mic/WAV,
  ir_nec, keyboard, buzzer, REPL — copy-paste-level API references for M0+.

### Credits to carry (verify licenses before porting any code)
MicroHydra (echo-lalia, GPLv3 — facts only) · M5Cardputer & M5-LoRa-E220-JP &
M5GFX (m5stack) · cardputer-nofrendo (lxyMiao) → arduino-nofrendo
(moononournation) → Nofrendo · RFID-PN532 (Jojorel) + PN532 lib (Seeed/Adafruit)
· CardputerLoRaChat (nonik0) · m5Cardputer_audiospectrum (cyberwisk) ·
M5CardRemote (VolosR) · M5Stick-Launcher (bmorcelli) · m5cardputer_doom
(romalik) · Bruce (pr3y) & ESP32Marauder (justcallmekoko/marivaaldo) — reference.

---

## 18. Build hazards & mitigations (headache list)

Verified against real code (PaperOS, SquachWatch-CYD, M5GFX, official M5 libs).
Ordered by how much pain they'd cause if hit blind.

1. **Radio arbitration — sniff vs. connect (THE big one, M1 design).**
   SquachWatch detection = `esp_wifi_set_promiscuous(true)` + **channel-hopping**
   in `WIFI_STA` mode, **not associated** to any AP. PaperOS connectivity
   (Browser, Store, LLM/OTA fetch, NTP) = `WiFi.begin()` **associated on one
   fixed channel**. These are **mutually exclusive** — you cannot channel-hop
   while holding an association. Mitigation: a **kernel radio manager** with two
   modes — default **Warding** (promiscuous + hop, full detection) and on-demand
   **Link** (apps request it; stop hopping, associate; detection narrows to the
   connected channel — still hears deauth/mgmt aimed at you, loses multi-channel
   coverage). The Mitama narrates the trade ("looking away from the spectrum
   while you browse"). SquachWatch confirms the split: it sniffs for detection
   and only `WiFi.begin()`s separately for OTA/LoRa feed.
2. **RAM: ~320 KB shared heap, NO PSRAM (governing budget).** WiFi sniffer +
   NimBLE scan + the LLM + UI sprites + fonts all compete (PaperOS's own note).
   Mitigations: LLM stays async/streamed from SD (§14); sniffer paused in Link
   mode; BLE scans duty-cycled; fonts subset (below); bound sprite sizes; keep
   PaperOS's Lua heap cap. **Do not enable SPIRAM build flags.**
3. **Keyboard (ADV) — RESOLVED.** PaperOS calls `M5Cardputer.Keyboard` in every
   app; bumping `m5stack/M5Cardputer` to ≥1.1.1 (+ matching M5Unified) makes all
   of it work on the ADV unchanged (official TCA8418 reader). No per-app edits.
4. **GPIO5 / SD mount.** On the ADV the extra I2C sensors fight GPIO5 (SPI CS) —
   **drive GPIO5 HIGH during init** or the SD card never mounts (Launcher gotcha).
5. **WiFi + BLE coexistence.** NimBLE scan + esp_wifi promiscuous run together
   (coex) — extra RAM + timing cost. Budget for it; duty-cycle BLE.
6. **Fonts — use VLW (and a pleasant surprise).** M5GFX loads **VLW** fonts at
   runtime: `display.loadFont("/fonts/rajdhani_14.vlw", SD)` / `unloadFont()`.
   **VLW glyphs are anti-aliased**, so Rajdhani at 8 px reads *better* than the
   1-bit bake I feared — softens the §15 "8 px floor" caveat. Ship subset VLWs
   (ASCII + the few glyphs we use) on SD/LittleFS to keep RAM/size small; load
   the 3 sizes we need, unload when switching.
7. **Audio arbitration — mostly solved.** M5 `Speaker_Class` supports **virtual
   sound channels** (PaperOS uses them via `AudioOutputM5Speaker`). Play UI cues
   and music on **separate virtual channels** so a cue doesn't cut the music.
   Emulator audio takes the speaker exclusively during games.
8. **Flash / partition budget.** `paperos_8mb.csv`: app0 (ota_0) **1.69 MB**,
   app1 (ota_1) 0.96 MB (the spare OTA slot for the emulator/games boot),
   `romxip` spiffs **5 MB** (emulator ROMs, XIP), spiffs 320 KB; `model.bin`
   lives on **SD**. Adding the SquachWatch engine + NimBLE + buddy may strain
   app0 — if it overflows, repartition (shrink `romxip`). Watch app0 size at M0/M1.
9. **Board target.** `board = m5stack-stamps3` compiles for the S3; rely on
   M5Unified **runtime** detection of `board_M5CardputerADV`. Keep flash 8 MB /
   qio / 240 MHz; LittleFS + SD. Bootloader: hold GPIO0 + reset.

---

## 10. Session-1 scaffold (NOT yet recovered)

The first build session scaffolded a `netbuddy/` project (platformio.ini, an
`os/` event-bus + kernel + app interface, `net/` wifi_monitor / ble_monitor /
device_tracker, `apps/` buddy_app / dashboard_app / launcher_app, and main.cpp).
That source was never captured into a repo and is **not recoverable
verbatim**. Per §5 the plan replaces those from-scratch monitors with the
SquachWatch port anyway, so the rebuild starts from the §8 build order rather
than trying to reconstruct lost code.
