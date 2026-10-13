#pragma once
#include <M5Cardputer.h>
#include "app.h"
#include "app_manager.h"
#include "os_ui.h"
#include "wifi_manager.h"
#include "../config.h"
#include "disp_lock.h"
#include "../system/paper_clock.h"

// App launcher, restyled as a Windows-95-style desktop: teal background,
// a "My Computer" desktop icon followed by every registered app as its own
// icon, and a bottom taskbar with a Start button + clock. Selection is shown
// by the highlighted label only (no mouse pointer), moved with the keyboard
// "arrows" (;,./  =  up,left,down,right, matching the rest of the OS's key
// mapping since this hardware has no real arrow keys).

#define GRID_COLS     3
#define TILE_W       78
#define TILE_H       30
#define GRID_TOP     (HEADER_H + 3)
#define GRID_BOTTOM  (SCREEN_H - FOOTER_H - 2)
#define ROWS_VIS     ((GRID_BOTTOM - GRID_TOP) / TILE_H)

// One synthetic desktop icon lives before the real app list.
#define DESK_MY_COMPUTER 0
#define DESK_EXTRA_COUNT 1

class Launcher : public App {
public:
    Launcher(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Launcher"; }
    const char* getIcon() const override { return "#"; }
    const char* getDesc() const override { return "Home"; }

    void onStart() override {
        // IMPORTANT: whatever key brought us here (Enter to submit a Lua
        // command, Enter/'y' on a confirmExit() dialog, etc.) may still be
        // physically held down on the very first tick we run. Block here
        // until every key is physically released before honoring input.
        OsUI::waitRelease();
        _sel        = 0;
        _scroll     = 0;
        _needRedraw = true;
        _wifiRetry  = 0;
        WiFiManager::get().ensureConnected();
        if (WiFiManager::get().isConnected() && !_ntpStarted) {
            PaperClock::syncTime();
            _ntpStarted = true;
        }
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();

        int appItems = _countAppItems();
        int items = appItems + DESK_EXTRA_COUNT;

        bool changed = false;

        auto st = M5Cardputer.Keyboard.keysState();

        // navigation
        if (M5Cardputer.Keyboard.isKeyPressed(',')) { _move(-1); changed=true; delay(KEY_REPEAT_MS); }
        if (M5Cardputer.Keyboard.isKeyPressed('/')) { _move(+1); changed=true; delay(KEY_REPEAT_MS); }
        if (M5Cardputer.Keyboard.isKeyPressed(';')) { _move(-GRID_COLS); changed=true; delay(KEY_REPEAT_MS); }
        if (M5Cardputer.Keyboard.isKeyPressed('.')) { _move(+GRID_COLS); changed=true; delay(KEY_REPEAT_MS); }

        if (st.enter && items > 0) {
            OsUI::waitRelease();
            _open(_sel, appItems);
            return;
        }

        // clock / wifi tick
        _clockTick++;
        if (_clockTick >= 30) {
            _clockTick = 0; changed = true;
            if (!WiFiManager::get().isConnected()) {
                if (++_wifiRetry >= 5) { _wifiRetry=0; WiFiManager::get().ensureConnected(); }
            } else if (!_ntpStarted) {
                PaperClock::syncTime(); _ntpStarted = true;
            }
        }

        if (_startFlash > 0) { _startFlash--; changed = true; }

        if (changed || _needRedraw) { _needRedraw=false; _draw(items, appItems); }
    }

private:
    LGFX_Sprite& _canvas;
    int  _sel        = 0;
    int  _scroll     = 0;   // top visible row index
    bool _needRedraw = true;
    bool _ntpStarted = false;
    int  _clockTick  = 0;
    int  _wifiRetry  = 0;
    int  _startFlash = 0;

    static bool _isHiddenFromGrid(App* a) {
        // SysInfo is reachable via the "My Computer" desktop icon instead,
        // so it's left out of the regular app grid to avoid listing it twice.
        return a && strcmp(a->getName(), "SysInfo") == 0;
    }

    // Number of real (non-launcher, non-hidden) apps shown as grid icons.
    int _countAppItems() {
        int n = 0, count = AppManager::get().getCount();
        for (int idx = 1; idx < count; idx++) {
            if (!_isHiddenFromGrid(AppManager::get().getApp(idx))) n++;
        }
        return n;
    }

    // Maps a 0-based grid slot (after the synthetic desktop icons) to the
    // AppManager index of the slot-th visible app.
    int _realAppIndex(int slot) {
        int seen = 0, count = AppManager::get().getCount();
        for (int idx = 1; idx < count; idx++) {
            App* a = AppManager::get().getApp(idx);
            if (_isHiddenFromGrid(a)) continue;
            if (seen == slot) return idx;
            seen++;
        }
        return -1;
    }

    void _move(int delta) {
        int appItems = _countAppItems();
        int items = appItems + DESK_EXTRA_COUNT;
        if (items < 1) return;
        _sel = constrain(_sel+delta, 0, items-1);
        int row = _sel / GRID_COLS;
        if (row < _scroll) _scroll = row;
        if (row >= _scroll + ROWS_VIS) _scroll = row - ROWS_VIS + 1;
    }

    void _tilePos(int i, int& tx, int& ty) {
        int row = i / GRID_COLS;
        int col = i % GRID_COLS;
        tx = col*TILE_W + 1;
        ty = GRID_TOP + (row-_scroll)*TILE_H + 1;
    }

    // label/icon-glyph/color for desktop item i (0/1 = synthetic, rest = apps)
    void _itemInfo(int i, int appItems, String& label, String& glyph, uint16_t& color) {
        if (i == DESK_MY_COMPUTER) {
            label = "My Comp."; glyph = "C"; color = 0xC618; // gray "beige box" look
        } else {
            App* app = AppManager::get().getApp(_realAppIndex(i - DESK_EXTRA_COUNT));
            label = app ? String(app->getName()) : "?";
            glyph = app ? String(app->getIcon()) : "?";
            color = TILE_COLORS[i % NUM_TILE_COLORS];
        }
        if ((int)label.length() > 8) label = label.substring(0, 8);
    }

    void _open(int sel, int appItems) {
        if (sel == DESK_MY_COMPUTER) {
            AppManager::get().launch("SysInfo");
        } else {
            AppManager::get().launchByIndex(_realAppIndex(sel - DESK_EXTRA_COUNT));
        }
    }

    void _draw(int items, int appItems) {
        _canvas.fillSprite(COL_DESKTOP);

        auto& wm = WiFiManager::get();
        bool connected  = wm.isConnected();
        bool connecting = !connected && wm.isConnecting();
        uint16_t hcol = connected ? COL_HEADER : (connecting ? COL_HEADER_LOAD : COL_HEADER_BAD);
        if (connected && !_ntpStarted) { PaperClock::syncTime(); _ntpStarted=true; }
        String hdr = String(OS_NAME);
        OsUI::drawHeader(_canvas, hdr.c_str(), hcol);

        if (items==0) {
            _canvas.setTextColor(COL_WARN,COL_DESKTOP); _canvas.setTextSize(1);
            _canvas.drawString("No apps registered", 20, 60);
        }

        for (int i=0; i<items; i++) {
            int row = i / GRID_COLS;
            if (row<_scroll || row>=_scroll+ROWS_VIS) continue;

            int tx, ty; _tilePos(i, tx, ty);
            if (ty+TILE_H > GRID_BOTTOM) continue;

            bool sel = (_sel==i);
            String label, glyph; uint16_t icol;
            _itemInfo(i, appItems, label, glyph, icol);

            // icon: flat square, black outline - a Win95 bitmap-icon stand-in
            int isz = 16;
            int ix = tx + (TILE_W-2-isz)/2, iy = ty + 1;
            _canvas.fillRect(ix, iy, isz, isz, icol);
            _canvas.drawRect(ix, iy, isz, isz, COL_TEXT);
            _canvas.drawFastHLine(ix+1, iy+1, isz-2, 0xFFFF); // tiny top-left highlight, like a sprite bevel
            _canvas.setTextColor(0, icol); _canvas.setTextSize(1);
            _canvas.setCursor(ix + isz/2 - 3, iy + isz/2 - 4);
            _canvas.print(glyph);

            // label, white with a 1px black "shadow" so it reads on teal,
            // with a navy selection box behind it when highlighted
            int lw = label.length()*6;
            int lx = tx + (TILE_W-2)/2 - lw/2, ly = iy + isz + 2;
            if (sel) _canvas.fillRect(lx-2, ly-1, lw+4, 9, COL_SELECTED);
            _canvas.setTextColor(0, COL_DESKTOP);
            _canvas.setCursor(lx+1, ly+1); _canvas.print(label);
            _canvas.setTextColor(0xFFFF, sel ? (uint16_t)COL_SELECTED : (uint16_t)COL_DESKTOP);
            _canvas.setCursor(lx, ly); _canvas.print(label);
        }

        // scrollbar
        int totalRows = (items+GRID_COLS-1)/GRID_COLS;
        if (totalRows > ROWS_VIS) {
            int bh = GRID_BOTTOM - GRID_TOP;
            int th = bh * ROWS_VIS / totalRows;
            int ty = GRID_TOP + (_scroll * bh / totalRows);
            OsUI::drawPanel(_canvas, SCREEN_W-8, GRID_TOP, 6, bh, false, COL_BG2);
            _canvas.fillRect(SCREEN_W-7, ty, 4, max(4,th), COL_BEVEL_LO);
        }

        _drawTaskbar();

        { DispLock _lk; _canvas.pushSprite(0,0); }
    }

    void _drawTaskbar() {
        int ty = SCREEN_H - FOOTER_H;
        OsUI::drawPanel(_canvas, 0, ty, SCREEN_W, FOOTER_H, true, COL_BG2);

        // Start button
        int sw = 48, sh = FOOTER_H - 4;
        bool pressed = _startFlash > 0;
        OsUI::drawButton(_canvas, 2, ty+2, sw, sh, "", pressed);
        // Mini notebook glyph (same motif as the boot-screen PaperLogo mark,
        // just small enough for the taskbar) instead of a Win95-style flag.
        int fx = 6 + (pressed?1:0), fy = ty+4+(pressed?1:0);
        _canvas.fillRect(fx,     fy, 8, 8, COL_BG);      // page
        _canvas.drawRect(fx,     fy, 8, 8, COL_TEXT);    // page border
        _canvas.fillRect(fx,     fy, 2, 8, COL_ACCENT);  // spine
        _canvas.drawFastHLine(fx+3, fy+2, 4, COL_DIM);   // ruled lines
        _canvas.drawFastHLine(fx+3, fy+4, 4, COL_DIM);
        _canvas.drawFastHLine(fx+3, fy+6, 3, COL_DIM);
        _canvas.setTextColor(COL_TEXT, COL_BG2);
        _canvas.setCursor(fx+11, fy);
        _canvas.print("Start");

        // clock, sunken panel on the right (Win95 systray look)
        String tstr = PaperClock::getShortTime();
        int cw = 40;
        int cx = SCREEN_W - cw - 2;
        OsUI::drawPanel(_canvas, cx, ty+2, cw, sh, false, COL_BG2);
        _canvas.setTextColor(COL_TEXT, COL_BG2);
        _canvas.setCursor(cx + (cw - (int)tstr.length()*6)/2, ty+4);
        _canvas.print(tstr);

        // hint text in the middle strip - kept short so it never runs into
        // the clock panel (only ~140px / ~23 chars available here)
        _canvas.setTextColor(COL_DIM, COL_BG2);
        _canvas.setCursor(sw+10, ty+4);
        _canvas.print("Move ,/;.  Open ENT");
    }
};
