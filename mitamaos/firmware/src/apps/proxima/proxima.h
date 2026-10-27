#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include "../../os/app.h"
#include "../compendium/compendium_app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/disp_lock.h"
#include "../../config.h"
#include "../../ui/mitama_ui.h"
#include "../../detect/detection.h"
#include "../../mitama/brain.h"

// Proxima - the Mitama's home screen (outline §2 "Mitama / Proxima layer",
// §15). Where MitamaOS boots, where Esc from the app grid goes, and where
// the G0 button brings you from (most) apps. The full Mitama sits on the
// left; the right side summarises the local network, the detection
// service's threat / watchlist counts and the latest sightings. Its mood
// line rides the status bar. Enter opens the app grid.
//
// Not a launcher tile (naming rule): the launcher hides it from the grid.
class ProximaApp : public App {
public:
    ProximaApp(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Proxima"; }
    const char* getIcon() const override { return "@"; }
    const char* getDesc() const override { return "The Mitama's home"; }

    void onStart() override {
        OsUI::waitRelease();
        _tick = 0;
        _needRedraw = true;
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();
        auto st = M5Cardputer.Keyboard.keysState();

        // S cycles the sass dial (0 Mute Muse .. 3 Insufferable).
        if (M5Cardputer.Keyboard.isKeyPressed('s')) {
            OsUI::waitRelease();
            MitamaBrain::get().cycleSass();
            snprintf(_toast, sizeof(_toast), "Sass %u: %s", MitamaBrain::get().sass(),
                     MitamaBrain::sassName(MitamaBrain::get().sass()));
            _sassToastTicks = 30;
            _needRedraw = true;
        }
        // F changes the Mitama's form (the unlocked ones; the rest are in the Compendium).
        if (M5Cardputer.Keyboard.isKeyPressed('f')) {
            OsUI::waitRelease();
            auto& fm = Forms::get();
            if (fm.unlockedCount() > 1) {
                MUI::Form f = fm.next();
                snprintf(_toast, sizeof(_toast), "Form: %s", Forms::name(f));
            } else {
                snprintf(_toast, sizeof(_toast), "No forms yet. The Compendium lists them.");
            }
            _sassToastTicks = 30;
            _needRedraw = true;
        }

        if (st.enter) {
            OsUI::waitRelease();
            AppManager::get().goHome();   // the app grid
            return;
        }

        // 1-3 open that recent sighting's Compendium card.
        for (char c : st.word) {
            if (c < '1' || c > '3') continue;
            int i = c - '1';          // the row as drawn, not a fresher list
            if (i < _shownN && Compendium::listed(_shown[i])) {
                OsUI::waitRelease();
                CompendiumApp::openAt(_shown[i]);
                AppManager::get().requestLaunch("Compendium");
                return;
            }
        }

        // The bones ring turns slowly (a full turn every ~12s): redraw at
        // ~10fps for it, which also keeps the clock / Wi-Fi fresh and
        // animates the Mitama's line.
        ++_tick;
        if (_tick % 5 == 0) {
            _spin = fmodf(_spin + 3.0f, 360.0f);
            _needRedraw = true;
        }
        if (_needRedraw) { _needRedraw = false; _draw(); }
    }

private:
    LGFX_Sprite& _canvas;
    bool     _needRedraw = true;
    uint32_t _tick = 0;
    uint32_t _lineShownMs = 0;     // when the current line started showing
    uint32_t _lastSerial = 0xFFFFFFFF;
    int      _sassToastTicks = 0;
    char     _toast[48] = "";
    float    _spin = 0;
    uint8_t  _shown[3] = {};       // sighting types on screen, for keys 1-3
    int      _shownN = 0;

    static uint16_t _raceColor(uint8_t type) { return MUI::raceColor(Sig::raceOf(type)); }

    static void _age(char* out, size_t n, uint32_t ms) {
        uint32_t s = ms / 1000;
        if (s < 60)        snprintf(out, n, "%us", (unsigned)s);
        else if (s < 3600) snprintf(out, n, "%um", (unsigned)(s / 60));
        else               snprintf(out, n, "%uh", (unsigned)(s / 3600));
    }

    void _draw() {
        _canvas.fillSprite(MUI::BG);
        bool online = WiFi.status() == WL_CONNECTED;
        DetectionEngine::Stats st = DetectionEngine::get().stats();
        // Mood: red = a threat nearby; cyan = something on the watchlist;
        // green = all clear while on a network; calm yellow while offline.
        auto& brain = MitamaBrain::get();
        uint16_t aura = st.threats ? MUI::RED
                      : brain.mood() != MitamaBrain::M_CALM ? brain.moodColor()
                      : st.watch ? MUI::CYAN : online ? MUI::GREEN : MUI::YELLOW;

        MUI::header(_canvas, "PROXIMA", aura);
        // forms may reach past the soul: keep them off the header and the text column
        auto& ki = Ki::get();
        const int lvY = SCREEN_H - FOOTER_H - 12;   // level and ki bar, under the Mitama
        _canvas.setClipRect(0, HEADER_H + 1, 82, lvY - HEADER_H - 1);
        MUI::soulForm(_canvas, 40, 66, 92, MUI::YELLOW, aura, _spin, Forms::get().current(), ki.rank());
        _canvas.clearClipRect();
        {
            char lv[8]; snprintf(lv, sizeof(lv), "Lv%d", ki.level());
            int lx = MUI::text(_canvas, MUI::small(), lv, 3, lvY, MUI::CYAN) + 4;
            const int bx1 = 78, by = lvY + 5;
            _canvas.drawRect(lx, by, bx1 - lx, 4, MUI::BONES_DIM);
            int fill = (int)((bx1 - lx - 2) * ki.progress());
            if (fill > 0) _canvas.fillRect(lx + 1, by + 1, fill, 2, MUI::CYAN);
        }

        const int x = 84;
        int y = HEADER_H + 3;
        char buf[48];
        if (online) {
            int e = MUI::text(_canvas, MUI::small(), "NET ", x, y, MUI::WHITE);
            _canvas.setClipRect(e, y, SCREEN_W - 3 - e, 14);
            MUI::text(_canvas, MUI::small(), WiFi.SSID().c_str(), e, y, MUI::CYAN);
            _canvas.clearClipRect();
            y += 13;
            snprintf(buf, sizeof(buf), "%d dBm  /  %s", (int)WiFi.RSSI(), WiFi.localIP().toString().c_str());
            MUI::text(_canvas, MUI::small(), buf, x, y, MUI::DIM);
        } else {
            int e = MUI::text(_canvas, MUI::small(), "NET ", x, y, MUI::WHITE);
            MUI::text(_canvas, MUI::small(), "offline", e, y, MUI::DIM);
            y += 13;
            MUI::text(_canvas, MUI::small(), "Join one in Settings", x, y, MUI::DIM);
        }
        y += 14;

        snprintf(buf, sizeof(buf), "%d", st.threats);
        int e = MUI::text(_canvas, MUI::small(), "Threats ", x, y, MUI::WHITE);
        e = MUI::text(_canvas, MUI::big(), buf, e, y - 2, st.threats ? MUI::RED : MUI::GREEN);
        e = MUI::text(_canvas, MUI::small(), "    Watchlist ", e, y, MUI::WHITE);
        snprintf(buf, sizeof(buf), "%d", st.watch);
        MUI::text(_canvas, MUI::small(), buf, e, y, MUI::YELLOW);
        y += 16;

        _canvas.drawFastHLine(x, y, SCREEN_W - x - 4, MUI::BONES_DIM);
        y += 3;
        e = MUI::text(_canvas, MUI::small(), "RECENT", x, y, MUI::DIM, 1);
        // Which radios are listening, on the right of the RECENT label.
        char ears[24];
        if (st.ble == DetectionEngine::BLE_LOW_MEM)   // free RAM in KB, to see how short it is
            snprintf(ears, sizeof(ears), "%sBLE:%uK", st.wifiOn ? "WIFI  " : "", (unsigned)(st.freeHeap / 1024));
        else
            snprintf(ears, sizeof(ears), "%s", st.wifiOn && st.ble == DetectionEngine::BLE_ON ? "WIFI+BLE"
                                             : st.wifiOn ? "WIFI"
                                             : st.ble == DetectionEngine::BLE_ON ? "BLE" : "paused");
        if (ScryLogger::get().recording()) strncat(ears, "  REC", sizeof(ears) - strlen(ears) - 1);
        int ew = MUI::width(_canvas, MUI::small(), ears);
        MUI::text(_canvas, MUI::small(), ears, SCREEN_W - 4 - ew, y, MUI::DIM);
        y += 13;
        if (st.recentN == 0) {
            MUI::text(_canvas, MUI::small(), "No sightings yet.", x, y, MUI::LABEL);
            y += 12;
            MUI::text(_canvas, MUI::small(), "Listening...", x, y, MUI::DIM);
        }
        _shownN = 0;
        for (int i = 0; i < st.recentN; i++) {
            const auto& r = st.recent[i];
            if (_shownN < 3) _shown[_shownN++] = r.type;
            char age[8];
            _age(age, sizeof(age), st.nowMs - r.lastMs);
            int aw = MUI::width(_canvas, MUI::small(), age);
            int right = SCREEN_W - 4 - aw;
            uint16_t col = _raceColor(r.type);
            char num[3] = { (char)('1' + i), ' ', 0 };
            int nx = MUI::text(_canvas, MUI::small(), num, x, y, MUI::DIM);
            _canvas.setClipRect(nx, y, right - 4 - nx, 13);
            int ex = MUI::text(_canvas, MUI::small(), Sig::raceName(Sig::raceOf(r.type)), nx, y, col);
            ex = MUI::text(_canvas, MUI::small(), " ", ex, y, col);
            ex = MUI::text(_canvas, MUI::small(), Sig::typeName(r.type), ex, y, MUI::WHITE);
            if (r.detail) {
                ex = MUI::text(_canvas, MUI::small(), " ", ex, y, MUI::DIM);
                MUI::text(_canvas, MUI::small(), r.detail, ex, y, MUI::DIM);
            }
            _canvas.clearClipRect();
            MUI::text(_canvas, MUI::small(), age, right, y, MUI::DIM);
            y += 12;
        }

        // The Mitama's line: typed out, then scrolled if it's longer than
        // the status bar.
        char say[110];
        if (_sassToastTicks > 0) {
            _sassToastTicks--;
            snprintf(say, sizeof(say), "%s", _toast);
            MUI::footer(_canvas, say, MUI::YELLOW, "ENT apps", aura);
        } else {
            const char* ln = brain.line();
            if (brain.lineSerial() != _lastSerial) { _lastSerial = brain.lineSerial(); _lineShownMs = millis(); }
            uint32_t age = millis() - _lineShownMs;
            snprintf(say, sizeof(say), "\"%s\"", ln);
            size_t len = strlen(say);
            size_t shown = min(len, (size_t)(age / 35));      // ~28 chars/s typing
            char typed[110];
            memcpy(typed, say, shown); typed[shown] = 0;
            int avail = SCREEN_W - 3 - MUI::width(_canvas, MUI::small(), "ENT apps") - 6 - 17;
            int w = MUI::width(_canvas, MUI::small(), say);
            int scroll = 0;
            if (shown == len && w > avail) {
                // pause 2s, scroll at ~30px/s, pause 2s at the end, repeat
                uint32_t t = age - len * 35;
                uint32_t run = (uint32_t)(w - avail) * 33;
                uint32_t cyc = t % (2000 + run + 2000);
                scroll = cyc < 2000 ? 0 : cyc < 2000 + run ? (int)((cyc - 2000) / 33) : w - avail;
            }
            MUI::footer(_canvas, typed, MUI::CREAM, "ENT apps", aura, scroll);
        }

        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }
};
