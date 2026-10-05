# NetBuddy OS — Project Outline

A fully featured, PDA-style handheld OS/firmware for the **M5Cardputer ADV**
(ESP32-S3), with a Navi/Bjorn-style companion "buddy" that makes the device's
network environment legible at a glance.

**Design commitment (non-negotiable):** everything network-facing is
**receive-only / passive**. NetBuddy detects, observes, and informs — it never
transmits attacks. No deauth frames, no evil-portal, no credential capture, no
exploit payloads. There is no call to `esp_wifi_80211_tx` anywhere in the
codebase, and that stays true as the project grows. This is a *defensive*
companion, the inverse of offensive firmware like Porkchop.

---

## 1. Platform & toolchain

- **Board:** M5Cardputer ADV (ESP32-S3, PSRAM).
- **Framework:** PlatformIO + Arduino, using the M5 ecosystem
  (`M5Unified`, `M5Cardputer`) plus `NimBLE-Arduino` for passive BLE.
- Fastest path to a flashable image on the ADV and the stack the Cardputer
  community already builds on.
- Build with `pio run`; flash with `pio run -t upload`. (Cannot be
  compiled/flashed against real hardware from the Claude session — needs a
  local build pass to shake out library-version issues before flashing.)

---

## 2. The three pillars

NetBuddy is a full handheld OS, not a security tool with a mascot. Scope spans
three pillars:

### Productivity
File manager, notes, calculator, calendar, to-do list, world clock, alarms.

### Games
Build-effort order: Snake → Solitaire → Chess → Poker.
- **Chess difficulty:** Medium-Easy. Shallow search (2–3 ply), *not* a real
  engine. "For fun, not to be a GM."

### Buddy / Environment layer
A Navi-style animated sprite + passive environment monitoring, surfaced as
"Environment Status." The buddy reacts to what the radios see — calm when
normal, alert/agitated when something is worth flagging.

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
into NetBuddy's kernel architecture, *replacing* session-1's from-scratch
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

## 5a. Dex taxonomy — Pokémon-style types, legendaries & glitches

SquachWatch ships **17 Dex entries** (18 `DetectionType` values; `UNKNOWN`
gets no card, so `Dex::ENTRIES = COUNT - 1`) and **19 outfits/skins**
(`OutfitId::COUNT`), unlocked two ways:

- **10 by lifetime-detection threshold** — NONE/TANOOKI/UNICORN (free),
  TINFOIL(5), SHADOW(15), PLUMBER BRO(25), TALL BRO(40), SPACE(60),
  BLUE BLUR(100), CAPTAIN(150).
- **9 by one-off event** (`OUTFIT_BY_EVENT`) — WOLF PELT, CHROME WING,
  VOID EYE, SNOW PARKA, SHARK SUIT, YZZERD, OVER 9000, SHAMBLER, TH3 0N3 —
  hidden Easter eggs on specific background screens.

The Pokémon idea maps cleanly: **device categories = types**, **device types =
species**. The outfit-threshold system is already "catch N, unlock reward," so a
**type-complete achievement** ("catch all 4 Trackers") slots into the existing
`OutfitDef`/`outfitUnlocked()` mechanism as a third unlock condition alongside
threshold and event — no new infrastructure.

### Finalized taxonomy

| Type         | Species                                   | Tier                     |
|--------------|-------------------------------------------|--------------------------|
| Official     | FLOCK, AXON, ALPR                         | Normal                   |
| Surveillance | CAMERA, RING, META                        | Normal                   |
| Tracker      | AIRTAG, TILE, SAMSUNG_TAG, GOOGLE_TAG     | Normal                   |
| Aerial       | DRONE                                     | Legendary (singleton)    |
| Beacon       | IBEACON                                   | Legendary (singleton)    |
| Fraud        | SKIMMER                                   | Legendary (singleton)    |
| Acoustic     | RAVEN                                     | Legendary (singleton)    |
| Glitch       | DEAUTH, EVILTWIN, HACKER                  | Corrupted / MissingNo    |

- **Government + police surveillance are lumped together** under **Official**
  (Flock, Axon, ALPR). Flock stays Normal tier, not split off by rarity — it's
  getting more common, not less. Rarity-as-Dex-tier and type-membership are
  separate axes; confidence grade (High/Med/Low) can still affect how "hard" a
  catch registers without being its own type.
- **Legendaries** = singleton-type species (one-of-a-kind in their category,
  rare encounters, special card treatment). Add a `LEGENDARY` tier above `RARE`
  in the existing `Dex::Rarity` enum — a one-line addition.
- **Glitches** = the HACKER bucket (DEAUTH, EVILTWIN, HACKER). These are
  attacks/hostile behavior, not "wildlife," so MissingNo fits: scrambled/
  glitch-art sprite, garbling name text, deliberately "wrong" Dex-card layout.
  Reinforces SquachWatch's existing red/hostile-vs-cyan/passive color logic. A
  cosmetic rendering flag on those three entries — no engine work.
- Dex gets a second axis: filter/sort by type, a type icon per entry, and
  buddy flavor-text that varies by type (an Official hit reads more serious than
  a Tracker hit).

**Open question (flagged, not decided):** RAVEN (gunshot detectors) is usually a
city/police deployment too, so by the same "who deploys it" logic it arguably
belongs in **Official** — which would retire the Acoustic legendary slot. Left
as its own legendary for now since it's a genuinely different detection modality
(audio, not camera/network).

---

## 6. Prior-art notes from SquachWatch worth copying

- **Better evil-twin test:** same-SSID-**different-encryption**, not
  same-SSID-different-BSSID (which false-positives on every mesh network).
- **Per-signature confidence grading** (High/Medium/Low) rather than binary
  detect — honest about weak/overlapping signatures.
- **Squachy pet / companion system** — prior art for NetBuddy's buddy.
- **Dex** — Pokédex-style catalog with rarity/lore/quips/personal records —
  prior art for the Dex and the Environment Status screen.

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

## 8. Build order

1. **Foundation** — SD storage layer + Go-button back-stack navigation.
2. **Port the detection engine** — SquachWatch `DetectionEngine`,
   `signatures.cpp`, pet, Dex into the kernel (replaces session-1 monitors).
3. **Buddy / Dex rework** — surface-based rendering, type/legendary/glitch
   taxonomy, type-complete achievements.
4. **Productivity apps** — file manager, notes, calculator, calendar, to-do,
   world clock, alarms.
5. **Games** — Snake → Solitaire → Chess (medium-easy) → Poker.
6. **HAT expansion** — dual-band, GPS/wardrive, LoRa sub-GHz, then NFC.
7. **Second screen** — route buddy rendering to a second display.

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
  `BackToMainMenu()`. This is essentially NetBuddy's app interface, already
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
and its partition scheme, not porting code. This is separate from NetBuddy's own
planned lightweight native games (Snake → Solitaire → Chess → Poker, §2).

### Architecture mismatches to reconcile

1. **Single-app vs background monitors.** AdvanceOS runs one foreground app at a
   time (`currentApp`, swapped by `ChangeMenu`). NetBuddy's defining feature —
   detection monitors + buddy running *regardless of focus* (§3 kernel) — does
   not exist in AdvanceOS. NetBuddy's event-bus/kernel has to sit *underneath*
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

Three ways to relate NetBuddy to AdvanceOS:
- **A — Borrow patterns only.** Keep NetBuddy's own codebase; copy the app-class,
  launcher and theme-manager *designs*; port individual apps as needed.
- **B — Fork AdvanceOS as the base.** Start from AdvanceOS, add the buddy +
  SquachWatch detection engine as a background service and the Dex/Environment
  app on top, reframe as NetBuddy. Fastest to a feature-rich OS; inherits the
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

## 10. Session-1 scaffold (NOT yet recovered)

The first build session scaffolded a `netbuddy/` project (platformio.ini, an
`os/` event-bus + kernel + app interface, `net/` wifi_monitor / ble_monitor /
device_tracker, `apps/` buddy_app / dashboard_app / launcher_app, and main.cpp).
That source was never captured into a repo and is **not recoverable
verbatim**. Per §5 the plan replaces those from-scratch monitors with the
SquachWatch port anyway, so the rebuild starts from the §8 build order rather
than trying to reconstruct lost code.
