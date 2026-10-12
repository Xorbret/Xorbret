# MitamaOS — Design Mockups

Early concept mockups for review. Self-contained HTML pages (Rajdhani loads from
Google Fonts; no other dependencies). These are **mockups for discussion**, not
final art or shipping firmware.

## Make them browsable + the web flasher (GitHub Pages via Actions)

The site is now published by the **`firmware` GitHub Actions workflow**, which
also builds the firmware and drops the flashable binaries next to the flasher
page (so the one-click installer can load them same-origin). To turn it on:

1. On GitHub: **Settings → Pages**.
2. **Build and deployment → Source:** **"GitHub Actions"**  (not "Deploy from a
   branch" — the workflow handles deployment).
3. Push anything under `mitamaos/firmware/**` or `docs/**` (or run the workflow
   manually from the **Actions** tab). When it finishes, the site is live at:
   **https://xorbret.github.io/Xorbret/**

Pages:
- `index.html` — design overview + links
- **`flash.html` — the web flasher** → **https://xorbret.github.io/Xorbret/flash.html**

> First run doubles as the **M0 compile test**. If the build is red, the
> binaries (and the flasher) won't appear yet — check the Actions log.
>
> The github-pages *environment* may restrict deployments to the default branch;
> if the `deploy` job is blocked on this feature branch, allow it in
> **Settings → Environments → github-pages**, or merge to the default branch.

### Flashing routes (all fed by the same CI build)
- **Web flasher** (`flash.html`) — one click, **desktop Chrome/Edge only** (Web
  Serial; no phones).
- **Launcher OTA** — add the `m0-test` release's merged `.bin` as a Favorite in
  [Launcher](https://github.com/bmorcelli/Launcher); installs on-device, no PC.
- **esptool / web.esphome.io** — flash the merged `.bin` at `0x0` manually.

## View right now (no setup — htmlpreview)

GitHub shows `.html` as source, so to render a single page without Pages:

| Mockup | View |
|--------|------|
| Index / overview | [open](https://htmlpreview.github.io/?https://github.com/Xorbret/Xorbret/blob/claude/cardputer-adv-os-firmware-okqopr/docs/index.html) |
| Mitama — character | [open](https://htmlpreview.github.io/?https://github.com/Xorbret/Xorbret/blob/claude/cardputer-adv-os-firmware-okqopr/docs/mitama.html) |
| UI screens | [open](https://htmlpreview.github.io/?https://github.com/Xorbret/Xorbret/blob/claude/cardputer-adv-os-firmware-okqopr/docs/ui.html) |
| Icons & glyphs | [open](https://htmlpreview.github.io/?https://github.com/Xorbret/Xorbret/blob/claude/cardputer-adv-os-firmware-okqopr/docs/icons.html) |
| Type scale | [open](https://htmlpreview.github.io/?https://github.com/Xorbret/Xorbret/blob/claude/cardputer-adv-os-firmware-okqopr/docs/type-scale.html) |
| Sound set (interactive) | [open](https://htmlpreview.github.io/?https://github.com/Xorbret/Xorbret/blob/claude/cardputer-adv-os-firmware-okqopr/docs/sound.html) |
| Logo | [open](https://htmlpreview.github.io/?https://github.com/Xorbret/Xorbret/blob/claude/cardputer-adv-os-firmware-okqopr/docs/logo.html) |

> The sound page uses Web Audio — click a card to play (browsers block autoplay until you interact). Via htmlpreview the index's internal links won't navigate; use the per-page links above, or enable Pages for full navigation.

## Or just open them locally

Download the folder and open `index.html` in any browser — everything works
offline except the Google-Fonts request for Rajdhani.

## Files

- `index.html` — overview + links to the rest
- `mitama.html` — character / mascot study
- `ui.html` — screen layouts
- `icons.html` — app sigils + race glyphs
- `type-scale.html` — typography
- `sound.html` — interactive sound set
- `logo.html` — wordmark lockups + app icon

The full written design lives in [`../mitamaos/outline.md`](../mitamaos/outline.md).
