# NetBuddy OS

A fully featured, PDA-style handheld OS/firmware for the **M5Cardputer ADV**
(ESP32-S3) with a Navi/Bjorn-style companion "buddy" that surfaces the device's
network environment at a glance.

## Design commitment

NetBuddy is a **defensive** companion. Everything network-facing is
**receive-only / passive**: it detects, observes, and informs — it never
transmits attacks. No deauth, no evil-portal, no credential capture, no exploit
payloads. There is no use of `esp_wifi_80211_tx` anywhere in the codebase, and
that stays true as the project grows.

## Status

Early design + rebuild. The full vision, architecture, detection-engine
strategy, and Dex taxonomy live in [`outline.md`](outline.md). The build order
is in §8 of that doc.

## Platform

M5Cardputer ADV (ESP32-S3, 8MB flash, no PSRAM). PlatformIO + Arduino,
M5Cardputer, vendored Lua 5.4, LittleFS + SD, NimBLE-Arduino. Build with
`pio run`.

## License

**GPLv2.** NetBuddy forks [PaperOS](https://github.com/Artem76228/PaperOS)
(GPLv2) as its OS core, so the combined work is GPLv2 with full source. MIT
components (SquachWatch, AdvanceOS) are absorbed under GPLv2 with attribution
preserved. See `outline.md` §13.

## Credits

- Detection engine, signature tables, pet and Dex from
  [SquachWatch-CYD](https://github.com/skizzophrenic/SquachWatch-CYD) (MIT).
- OS shell, app framework, theme manager, productivity apps and emulator from
  [AdvanceOS-for-cardputer](https://github.com/bomberman30/AdvanceOS-for-cardputer)
  (MIT, © 2025 bomberman30). See `outline.md` §11.
- Multitasking OS core, on-device LLM buddy, Lua app engine + store from
  [PaperOS](https://github.com/Artem76228/PaperOS) (**GPLv2**, © Artem76228).
  See `outline.md` §12. Note: using PaperOS source makes the combined work
  GPLv2 — see the license discussion in §13.
