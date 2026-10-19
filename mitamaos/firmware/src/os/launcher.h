#pragma once
#include <M5Cardputer.h>
#include "app.h"
#include "app_manager.h"
#include "os_ui.h"
#include "wifi_manager.h"
#include "../config.h"
#include "disp_lock.h"
#include "../system/paper_clock.h"
#include "../ui/mitama_ui.h"

// App launcher (outline §15): a 3-wide grid of tiles, each an esoteric
// sigil in its app's accent colour over a dark violet tile with a dim red
// border. The selected tile turns yellow with a glow and a red corner tick.
// Moved with the keyboard "arrows" (;,./ = up,left,down,right, matching the
// rest of the OS); Enter opens, Esc (`) goes back to Proxima.
//
// AppManager index 0 is this launcher, so every app's "exit to home" lands
// back on the grid. Proxima, the Mitama's home screen, is registered as an
// app but hidden from the grid: it's where you boot, Esc from here, or G0.

#define GRID_COLS   3
#define TILE_W      74
#define TILE_H      49
#define TILE_GAP    4
#define GRID_LEFT   5
#define GRID_TOP    (HEADER_H + 3)
#define ROWS_VIS    2

class Launcher : public App {
public:
    Launcher(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Launcher"; }
    const char* getIcon() const override { return "#"; }
    const char* getDesc() const override { return "Apps"; }

    void onStart() override {
        // IMPORTANT: whatever key brought us here (Enter to submit a Lua
        // command, Enter/'y' on a confirmExit() dialog, etc.) may still be
        // physically held down on the very first tick we run. Block here
        // until every key is physically released before honoring input.
        OsUI::waitRelease();
        _move(0);
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

        int items = _countItems();
        bool changed = false;

        auto st = M5Cardputer.Keyboard.keysState();

        // navigation
        if (M5Cardputer.Keyboard.isKeyPressed(',')) { _move(-1); changed=true; delay(KEY_REPEAT_MS); }
        if (M5Cardputer.Keyboard.isKeyPressed('/')) { _move(+1); changed=true; delay(KEY_REPEAT_MS); }
        if (M5Cardputer.Keyboard.isKeyPressed(';')) { _move(-GRID_COLS); changed=true; delay(KEY_REPEAT_MS); }
        if (M5Cardputer.Keyboard.isKeyPressed('.')) { _move(+GRID_COLS); changed=true; delay(KEY_REPEAT_MS); }

        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            OsUI::waitRelease();
            AppManager::get().launch("Proxima");
            return;
        }

        if (st.enter && items > 0) {
            OsUI::waitRelease();
            AppManager::get().launchByIndex(_realAppIndex(_sel));
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

        if (changed || _needRedraw) { _needRedraw=false; _draw(items); }
    }

private:
    LGFX_Sprite& _canvas;
    int  _sel        = 0;
    int  _scroll     = 0;   // top visible row index
    bool _needRedraw = true;
    bool _ntpStarted = false;
    int  _clockTick  = 0;
    int  _wifiRetry  = 0;

    // Grid presentation for each app: display label (plain names, except
    // Notes -> Grimoire per the naming rule), sigil and accent colour.
    struct Look { const char* name; const char* label; MUI::Sigil sigil; uint16_t color; };
    static const Look* _look(const char* name) {
        static const Look LOOKS[] = {
            { "Scry",      "Scry",     MUI::SIG_SCRY,     MUI::CYAN    },
            { "Files",     "Files",    MUI::SIG_FILES,    MUI::CYAN    },
            { "Notes",     "Grimoire", MUI::SIG_GRIMOIRE, MUI::YELLOW  },
            { "Paint",     "Paint",    MUI::SIG_PAINT,    MUI::MAGENTA },
            { "Music",     "Music",    MUI::SIG_MUSIC,    MUI::PURPLE  },
            { "Games",     "Games",    MUI::SIG_GAMES,    MUI::GREEN   },
            { "Lua",       "Lua",      MUI::SIG_LUA,      MUI::CYAN    },
            { "PaperWeb",  "Browser",  MUI::SIG_BROWSER,  MUI::CYAN    },
            { "Settings",  "Settings", MUI::SIG_SETTINGS, MUI::DIM     },
            { "Store",     "Store",    MUI::SIG_STORE,    MUI::YELLOW  },
            { "Piano",     "Piano",    MUI::SIG_PIANO,    MUI::PURPLE  },
            { "3D Editor", "3D",       MUI::SIG_CUBE,     MUI::CYAN    },
            { "Calc",      "Calc",     MUI::SIG_CALC,     MUI::MAGENTA },
            { "Clock",     "Clock",    MUI::SIG_CLOCK,    MUI::YELLOW  },
            { "myAI",      "myAI",     MUI::SIG_ORACLE,   MUI::GREEN   },
            { "SysInfo",   "System",   MUI::SIG_SYSTEM,   MUI::DIM     },
        };
        for (auto& l : LOOKS) if (strcmp(l.name, name) == 0) return &l;
        return nullptr;
    }

    static bool _isHiddenFromGrid(App* a) {
        return a && strcmp(a->getName(), "Proxima") == 0;
    }

    // Number of apps shown as tiles (everything after the launcher except
    // the hidden ones).
    int _countItems() {
        int n = 0, count = AppManager::get().getCount();
        for (int idx = 1; idx < count; idx++) {
            if (!_isHiddenFromGrid(AppManager::get().getApp(idx))) n++;
        }
        return n;
    }

    // Maps a 0-based grid slot to the AppManager index of that app.
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
        int items = _countItems();
        if (items < 1) { _sel = 0; _scroll = 0; return; }
        _sel = constrain(_sel+delta, 0, items-1);
        int row = _sel / GRID_COLS;
        if (row < _scroll) _scroll = row;
        if (row >= _scroll + ROWS_VIS) _scroll = row - ROWS_VIS + 1;
    }

    void _drawTile(int x, int y, const char* label, MUI::Sigil sig, uint16_t accent, bool sel) {
        _canvas.fillRect(x, y, TILE_W, TILE_H, MUI::PANEL);
        _canvas.drawRect(x, y, TILE_W, TILE_H, sel ? MUI::YELLOW : MUI::BONES_DIM);
        if (sel) {
            _canvas.drawRect(x - 1, y - 1, TILE_W + 2, TILE_H + 2, MUI::YELLOW_DIM);  // glow
            _canvas.fillRect(x - 1, y - 1, 7, 2, MUI::BONES);                          // corner tick
            _canvas.fillRect(x - 1, y - 1, 2, 7, MUI::BONES);
        }
        uint16_t col = sel ? MUI::YELLOW : accent;
        MUI::sigil(_canvas, sig, x + TILE_W / 2, y + 17, 20, col);
        MUI::textCentered(_canvas, MUI::small(), label, x + TILE_W / 2, y + TILE_H - 15,
                          sel ? MUI::YELLOW : MUI::LABEL);
    }

    void _draw(int items) {
        _canvas.fillSprite(MUI::BG);

        auto& wm = WiFiManager::get();
        bool connected  = wm.isConnected();
        bool connecting = !connected && wm.isConnecting();
        uint16_t mood = connected ? MUI::YELLOW : (connecting ? MUI::BONES_DIM : MUI::BONES);
        if (connected && !_ntpStarted) { PaperClock::syncTime(); _ntpStarted=true; }
        MUI::header(_canvas, "APPS", mood);

        if (items == 0) {
            MUI::textCentered(_canvas, MUI::small(), "No apps registered", SCREEN_W / 2, 60, MUI::YELLOW);
        }

        for (int i = 0; i < items; i++) {
            int row = i / GRID_COLS, col = i % GRID_COLS;
            if (row < _scroll || row >= _scroll + ROWS_VIS) continue;
            int x = GRID_LEFT + col * (TILE_W + TILE_GAP);
            int y = GRID_TOP + (row - _scroll) * (TILE_H + TILE_GAP);

            App* app = AppManager::get().getApp(_realAppIndex(i));
            const char* name = app ? app->getName() : "?";
            const Look* l = _look(name);
            _drawTile(x, y, l ? l->label : name, l ? l->sigil : MUI::SIG_UNKNOWN,
                      l ? l->color : MUI::LABEL, _sel == i);
        }

        int totalRows = (items + GRID_COLS - 1) / GRID_COLS;
        char pos[12];
        snprintf(pos, sizeof(pos), "%d/%d", _sel / GRID_COLS + 1, max(1, totalRows));
        MUI::footer(_canvas, pos, MUI::DIM, "ENT open  ESC proxima");

        { DispLock _lk; _canvas.pushSprite(0,0); }
    }
};
