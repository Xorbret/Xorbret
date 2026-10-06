# MitamaOS — Design Mockups

Early concept mockups for review. Self-contained HTML pages (Rajdhani loads from
Google Fonts; no other dependencies). These are **mockups for discussion**, not
final art or shipping firmware.

## Make them browsable (GitHub Pages — recommended)

This folder is laid out to publish directly with GitHub Pages:

1. On GitHub: **Settings → Pages**.
2. **Build and deployment → Source:** "Deploy from a branch".
3. **Branch:** `claude/cardputer-adv-os-firmware-okqopr`  •  **Folder:** `/docs` → **Save**.
4. Wait ~1 minute. The site goes live at:
   **https://xorbret.github.io/Xorbret/**

Then `index.html` is the entry point and all the in-page links work. (When this
branch is merged to your default branch, you can point Pages at that branch
instead.)

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

The full written design lives in [`../netbuddy/outline.md`](../netbuddy/outline.md).
