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

### Mitama / Environment layer
The Mitama (an animated sprite) + passive environment monitoring, surfaced as
"Environment Status." The Mitama reacts to what the radios see — calm when
normal, alert/agitated when something is worth flagging — and can speak through
the on-device LLM (§12).

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
   home. The vision needs a real back-stack: jump to Environment Status from
   anywhere, press again to return to *exactly* what you were doing. Kernel
   change, best done early.
2. **Storage — real SD filesystem layer.** Notes, to-do items, save games,
   trust lists all need a proper SD FS layer, not the two stub JSON paths in
   `config.h`. Build once, early — almost everything depends on it.
3. **Buddy rendering to "a surface."** The buddy should render to an abstract
   surface rather than hardcoding the main screen, so relocating it to a second
   display later is a routing change, not a rewrite.

---

## 4. Environment monitoring (passive / receive-only)

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
  of the MissingNo idea. Reinforces SquachWatch's red/hostile-vs-cyan/passive
  color logic. A cosmetic rendering flag on those three entries — no engine work.
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
  prior art for the Compendium and the Environment Status screen.

---

## 7. HAT modules

Robert's list: dual-band, GPS, NFC, sub-GHz.

| HAT        | Status                                                              |
|------------|---------------------------------------------------------------------|
| Dual-band  | = WiFi 2.4/5 GHz scanning. Ports from SquachWatch `SQW_WIFI_5G`.     |
| GPS        | Solved. Ports from `gnss.h`/`gnss.cpp`; unlocks wardrive + RTC-less time. |
| Sub-GHz    | Build the **LoRa** path now (ports directly, near-zero new work); hold a generic CC1101-style receiver until the exact module is known. "Both / not sure yet." |
| NFC        | No existing code to lean on. Hold off until a module is picked (likely PN532). |

- **RTC:** open low-priority question whether the Cardputer-ADV has an onboard
  RTC — GPS can supply time once that HAT is wired.

---

## 8. Build order (revised for the PaperOS base — see §13 decision)

0. **Fork & rebrand (M0).** Copy PaperOS v1.3 source into `netbuddy/` under
   GPLv2, add the GPLv2 `LICENSE` and a `CREDITS`/`NOTICE` attributing PaperOS,
   AdvanceOS, SquachWatch. Build for the ADV (no-PSRAM target already matches),
   confirm it boots and the stock apps run, rebrand PaperOS → MitamaOS. This is
   the foundation; everything else lands on top.
1. **Background detection service (M1).** Lift SquachWatch's `DetectionEngine` +
   signature tables in as a background FreeRTOS task using PaperOS's `DispLock`
   so it runs regardless of focused app. A minimal "Environment" app lists live
   detections. Watch the heap — detection + WiFi + (later) the LLM all contend
   for the no-PSRAM free heap.
2. **Mitama (M2).** Persistent companion driven by detection events; wire
   PaperOS's `myai` LLM so the Mitama can generate text, with a mood state
   machine (Calm→Curious→Worried→Alarmed) layered on top. Decide when the LLM is
   resident vs streamed (RAM).
3. **Compendium (M3).** Port SquachWatch's Dex/pet as the Compendium + the §5a
   race/Fiend taxonomy and race-complete achievements (Mitama forms).
4. **Runtime themes (M4 — the Option-2 addition).** Port AdvanceOS's Theme
   Manager (`.thm` JSON + SD PNG icons/wallpaper) to replace PaperOS's
   compile-time Win95 palette with runtime SD themes.
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
  SquachWatch detection engine as a background service and the Dex/Environment
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

## 10. Session-1 scaffold (NOT yet recovered)

The first build session scaffolded a `netbuddy/` project (platformio.ini, an
`os/` event-bus + kernel + app interface, `net/` wifi_monitor / ble_monitor /
device_tracker, `apps/` buddy_app / dashboard_app / launcher_app, and main.cpp).
That source was never captured into a repo and is **not recoverable
verbatim**. Per §5 the plan replaces those from-scratch monitors with the
SquachWatch port anyway, so the rebuild starts from the §8 build order rather
than trying to reconstruct lost code.
