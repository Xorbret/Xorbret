#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <vector>
#include "../config.h"
#include "disp_lock.h"

// Shared header/footer and dialog helpers - styled as Windows 95 chrome
// (navy titlebar, sunken/raised 3D bevel panels). Every app in PaperOS goes
// through these, so this is most of where the "Windows 95" look lives.
class OsUI {
public:

    // --- 3D bevel primitives (the core Win95 look) --------------------
    // raised = button/panel sticking "out" (light top+left, dark bottom+
    // right). Pass raised=false for a sunken/inset panel (status bars,
    // text fields) - edges flip.
    static void drawBevel(LGFX_Sprite& canvas, int x, int y, int w, int h, bool raised) {
        uint16_t hi = raised ? (uint16_t)COL_BEVEL_HI : (uint16_t)COL_BEVEL_DKLO;
        uint16_t lo = raised ? (uint16_t)COL_BEVEL_DKLO : (uint16_t)COL_BEVEL_HI;
        canvas.drawFastHLine(x, y, w, hi);
        canvas.drawFastVLine(x, y, h, hi);
        canvas.drawFastHLine(x, y + h - 1, w, lo);
        canvas.drawFastVLine(x + w - 1, y, h, lo);
    }

    static void drawPanel(LGFX_Sprite& canvas, int x, int y, int w, int h, bool raised,
                           uint16_t fill = COL_BG2) {
        canvas.fillRect(x, y, w, h, fill);
        drawBevel(canvas, x, y, w, h, raised);
    }

    // Small Win95-style push button. `pressed` draws it sunken (as if the
    // key/selection is currently activating it).
    static void drawButton(LGFX_Sprite& canvas, int x, int y, int w, int h,
                            const char* label, bool pressed) {
        canvas.fillRect(x, y, w, h, COL_BG2);
        drawBevel(canvas, x, y, w, h, !pressed);
        canvas.setTextColor(COL_TEXT, COL_BG2);
        canvas.setTextSize(1);
        int tw = strlen(label) * 6;
        canvas.setCursor(x + (w - tw) / 2 + (pressed ? 1 : 0), y + (h - 8) / 2 + (pressed ? 1 : 0));
        canvas.print(label);
    }

    static void drawHeader(LGFX_Sprite& canvas,
                           const char*  appTitle,
                           uint16_t     color = COL_HEADER)
    {
        // Cheap horizontal gradient (navy -> a lighter blue) to echo the
        // Win95 active-titlebar gradient without per-pixel cost.
        for (int i = 0; i < 6; i++) {
            int x0 = i * SCREEN_W / 6, x1 = (i + 1) * SCREEN_W / 6;
            uint16_t band = _lerp565(color, 0x2E9F, i / 5.0f);
            canvas.fillRect(x0, 0, x1 - x0, HEADER_H - 2, band);
        }
        canvas.fillRect(0, HEADER_H - 2, SCREEN_W, 2, COL_BG2);
        drawBevel(canvas, 0, HEADER_H - 2, SCREEN_W, 2, false);

        // Small title-bar "window icon"
        canvas.fillRect(3, 3, 10, 10, COL_BG2);
        drawBevel(canvas, 3, 3, 10, 10, true);
        canvas.setTextColor(COL_TEXT, COL_BG2);
        canvas.setTextSize(1);
        canvas.setCursor(6, 5);
        canvas.print(appTitle[0] ? appTitle[0] : '*');

        int bat = constrain(
            map(M5Cardputer.Power.getBatteryVoltage(), 3300, 4200, 0, 100),
            0, 100
        );
        const int BX = 218, BY = 4;
        canvas.drawRect(BX, BY, 18, 8, 0xFFFF);
        canvas.fillRect(BX + 18, BY + 2, 2, 4, 0xFFFF);
        int bw = map(bat, 0, 100, 0, 16);
        uint16_t bcol = bat > 30 ? (uint16_t)0x07E0 : (bat > 15 ? (uint16_t)COL_WARN : (uint16_t)COL_ERR);
        if (bw > 0) canvas.fillRect(BX + 1, BY + 1, bw, 6, bcol);

        // WiFi bars
        _drawRSSI(canvas, 197, 3);

        char titbuf[32];
        strncpy(titbuf, appTitle, 31);
        titbuf[31] = '\0';
        canvas.setClipRect(17, 0, 176, HEADER_H - 2);
        canvas.setTextColor(0xFFFF); // transparent-bg: sits on the gradient, no boxy fill
        canvas.setTextSize(1);
        canvas.setCursor(17, 4);
        canvas.print(titbuf);
        canvas.clearClipRect();
    }

    static void drawFooter(LGFX_Sprite& canvas, const char* hint) {
        drawPanel(canvas, 0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, false, COL_FOOTER);
        char buf[40];
        strncpy(buf, hint, 38);
        buf[38] = '\0';
        canvas.setTextColor(COL_DIM, COL_FOOTER);
        canvas.setTextSize(1);
        canvas.setCursor(6, SCREEN_H - FOOTER_H + 3);
        canvas.print(buf);
    }

    // Chunky segmented Win95-style progress bar, sunken groove with solid
    // blue blocks (rather than one continuous fill).
    static void drawProgressBar(LGFX_Sprite& canvas, int percent) {
        if (percent <= 0 || percent >= 100) return;
        int x = 2, y = HEADER_H, w = SCREEN_W - 4, h = 4;
        canvas.fillRect(x, y, w, h, COL_BG);
        drawBevel(canvas, x, y, w, h, false);
        int filled = map(percent, 0, 100, 0, w - 2);
        int seg = 4, gap = 1;
        for (int bx = 0; bx < filled; bx += (seg + gap)) {
            int bw = min(seg, filled - bx);
            canvas.fillRect(x + 1 + bx, y + 1, bw, h - 2, COL_ACCENT);
        }
    }

    static bool confirmExit(LGFX_Sprite& canvas, const char* msg = "Exit to home?") {
        waitRelease();
        bool sel = false;  // false=No, true=Yes

        while (true) {
            M5Cardputer.update();

            const int bx = 18, by = 36, bw = SCREEN_W - 36, bh = 58;
            drawPanel(canvas, bx, by, bw, bh, true, COL_BG2);
            // title strip
            canvas.fillRect(bx + 2, by + 2, bw - 4, 11, COL_HEADER);
            canvas.setTextColor(0xFFFF, COL_HEADER);
            canvas.setCursor(bx + 5, by + 4);
            canvas.print(OS_NAME);

            canvas.setTextSize(1);
            canvas.setTextColor(COL_TEXT, COL_BG2);
            canvas.setCursor(bx + 8, by + 18);
            canvas.print(msg);

            // [NO] / [YES] buttons
            int btw = 50, bth = 16;
            int ny = by + bh - bth - 6;
            int nx = bx + 14, yx = bx + bw - 14 - btw;

            drawButton(canvas, nx, ny, btw, bth, "NO", !sel);
            drawButton(canvas, yx, ny, btw, bth, "YES", sel);
            // dotted focus rect around the active button, Win95-style
            _focusRect(canvas, sel ? yx : nx, ny, btw, bth);

            { DispLock _lk; canvas.pushSprite(0, 0); }

            if (M5Cardputer.Keyboard.isKeyPressed(',') ||
                M5Cardputer.Keyboard.isKeyPressed(';')) {
                sel = !sel; delay(150);
            }
            if (M5Cardputer.Keyboard.isKeyPressed('/') ||
                M5Cardputer.Keyboard.isKeyPressed('.')) {
                sel = !sel; delay(150);
            }
            if (M5Cardputer.Keyboard.isKeyPressed('y') || M5Cardputer.Keyboard.isKeyPressed('Y')) {
                waitRelease(); return true;
            }
            if (M5Cardputer.Keyboard.isKeyPressed('n') || M5Cardputer.Keyboard.isKeyPressed('N')) {
                waitRelease(); return false;
            }

            auto st = M5Cardputer.Keyboard.keysState();
            if (st.enter) { waitRelease(); return sel; }
            if (M5Cardputer.Keyboard.isKeyPressed('`')) { waitRelease(); return false; }
            delay(5);
        }
    }

    static String inputText(LGFX_Sprite& canvas, const char* prompt) {
        waitRelease();
        String text = "";
        uint32_t keyTimer = 0;

        while (true) {
            M5Cardputer.update();

            const int bx = 8, by = 28, bw = SCREEN_W - 16, bh = 64;
            drawPanel(canvas, bx, by, bw, bh, true, COL_BG2);
            canvas.fillRect(bx + 2, by + 2, bw - 4, 11, COL_HEADER);
            canvas.setTextColor(0xFFFF, COL_HEADER);
            canvas.setCursor(bx + 5, by + 4);
            canvas.print(OS_NAME);

            canvas.setTextColor(COL_TEXT, COL_BG2);
            canvas.setTextSize(1);
            canvas.setCursor(bx + 6, by + 16);
            canvas.print(prompt);

            // sunken edit field (white, inset border) - classic Win95 textbox
            drawPanel(canvas, bx + 4, by + 28, bw - 8, 14, false, COL_BG);
            canvas.setCursor(bx + 7, by + 31);
            canvas.setTextColor(COL_TEXT, COL_BG);
            String display = text.length() > 30 ? text.substring(text.length() - 30) : text;
            canvas.print(display + "_");

            canvas.setTextColor(COL_DIM, COL_BG2);
            canvas.setCursor(bx + 6, by + 48);
            canvas.print("ENT=ok  `=cancel");

            { DispLock _lk; canvas.pushSprite(0, 0); }

            auto s = M5Cardputer.Keyboard.keysState();
            if (s.word.size() > 0 && millis() - keyTimer > 150) {
                for (auto c : s.word) text += c;
                keyTimer = millis();
            }
            if (s.del && text.length() > 0 && millis() - keyTimer > 100) {
                text.remove(text.length() - 1);
                keyTimer = millis();
            }
            if (s.enter) { waitRelease(); return text; }
            if (M5Cardputer.Keyboard.isKeyPressed('`')) { waitRelease(); return ""; }
            delay(1);
        }
    }

    static void showToast(LGFX_Sprite& canvas, const char* msg, uint16_t color = 0x0400) {
        int w = strlen(msg) * 6 + 14;
        if (w > SCREEN_W - 20) w = SCREEN_W - 20;
        int x = (SCREEN_W - w) / 2;
        int y = SCREEN_H - FOOTER_H - 20;
        drawPanel(canvas, x, y, w, 18, true, COL_BG2);
        canvas.fillRect(x + 2, y + 2, 6, 14, color);
        canvas.setTextColor(COL_TEXT, COL_BG2);
        canvas.setTextSize(1);
        canvas.setCursor(x + 11, y + 5);
        char buf[32];
        strncpy(buf, msg, 31); buf[31] = '\0';
        canvas.print(buf);
    }

    // `ks.ctrl && M5Cardputer.Keyboard.isKeyPressed('x')` looks right but is
    // unreliable in practice on this hardware/library: isKeyPressed() does a
    // fresh raw-matrix check that can land a tick away from the ctrl flag in
    // the KeysState snapshot you already have, and the Cardputer's own docs
    // note that while Ctrl (or Shift) is held, the character the library
    // resolves into `word` is forced to UPPERCASE (Ctrl+c -> 'C', not 'c').
    // Reading the combo out of the SAME already-captured KeysState avoids
    // both problems. (This matches how other community Cardputer OSes
    // handle it — they don't pair a live .ctrl/.shift check with a second
    // isKeyPressed() call either.)
    static bool modChar(bool modActive, const std::vector<char>& word, char lower) {
        if (!modActive) return false;
        for (char c : word) if (tolower((unsigned char)c) == lower) return true;
        return false;
    }
    static bool ctrlChar(const Keyboard_Class::KeysState& ks, char lower)  { return modChar(ks.ctrl,  ks.word, lower); }
    static bool shiftChar(const Keyboard_Class::KeysState& ks, char lower) { return modChar(ks.shift, ks.word, lower); }

    static void waitRelease() {
        M5Cardputer.update();
        while (M5Cardputer.Keyboard.isPressed()) {
            M5Cardputer.update(); delay(5);
        }
    }

private:
    static uint16_t _lerp565(uint16_t a, uint16_t b, float t) {
        int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
        int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
        int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
        return (r << 11) | (g << 5) | bl;
    }

    static void _focusRect(LGFX_Sprite& canvas, int x, int y, int w, int h) {
        for (int i = x; i < x + w; i += 2) {
            canvas.drawPixel(i, y + 2, COL_TEXT);
            canvas.drawPixel(i, y + h - 3, COL_TEXT);
        }
        for (int i = y; i < y + h; i += 2) {
            canvas.drawPixel(x + 2, i, COL_TEXT);
            canvas.drawPixel(x + w - 3, i, COL_TEXT);
        }
    }

    static void _drawRSSI(LGFX_Sprite& canvas, int x, int y) {
        bool connected = (WiFi.status() == WL_CONNECTED);
        int rssi = connected ? WiFi.RSSI() : -100;
        int bars = !connected ? 0
                 : rssi > -55 ? 4
                 : rssi > -70 ? 3
                 : rssi > -85 ? 2 : 1;

        for (int i = 0; i < 4; i++) {
            int bh = 2 + i * 2;
            int bx = x + i * 5;
            int by = y + (8 - bh);
            uint16_t col = (i < bars) ? (uint16_t)0xFFFF : (uint16_t)0x39C7;
            canvas.fillRect(bx, by, 3, bh, col);
        }
    }
};
