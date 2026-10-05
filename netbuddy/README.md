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

PlatformIO + Arduino, M5Unified / M5Cardputer, NimBLE-Arduino. Build with
`pio run`.

## Credits

Detection engine, signature tables, pet and Dex are ported from
[SquachWatch-CYD](https://github.com/skizzophrenic/SquachWatch-CYD) (MIT).
