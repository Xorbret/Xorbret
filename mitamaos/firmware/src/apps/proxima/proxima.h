#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/disp_lock.h"
#include "../../config.h"
#include "../../ui/mitama_ui.h"

// Proxima - the Mitama's home screen (outline §2 "Mitama / Proxima layer",
// §15). Where MitamaOS boots, where Esc from the app grid goes, and where
// the G0 button brings you from (most) apps. The full Mitama sits on the
// left; the right side summarises the local network and, once the M1
// detection service exists, threats and recent sightings. Its mood line
// rides the status bar. Enter opens the app grid.
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
        _pickLine();
        _tick = 0;
        _needRedraw = true;
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();
        auto st = M5Cardputer.Keyboard.keysState();

        if (st.enter) {
            OsUI::waitRelease();
            AppManager::get().goHome();   // the app grid
            return;
        }

        // The bones ring turns slowly (a full turn every ~12s): redraw at
        // ~10fps for it, which also keeps the clock / Wi-Fi fresh. Change
        // the Mitama's line every ~45s, or when the network state flips.
        ++_tick;
        if (_tick % 5 == 0) {
            _spin = fmodf(_spin + 3.0f, 360.0f);
            _needRedraw = true;
        }
        if (_tick % 50 == 0) {
            bool online = WiFi.status() == WL_CONNECTED;
            if (online != _wasOnline || _tick >= 50 * 45) { _tick = 0; _pickLine(); }
        }

        if (_needRedraw) { _needRedraw = false; _draw(); }
    }

private:
    LGFX_Sprite& _canvas;
    bool     _needRedraw = true;
    uint32_t _tick = 0;
    bool     _wasOnline = false;
    const char* _line = "";
    float    _spin = 0;

    // Placeholder line-bank until the M2 Mitama brain (event x mood, sass
    // dial) arrives. Kept short enough to fit the status bar.
    void _pickLine() {
        static const char* ONLINE[] = {
            "All quiet. Suspiciously quiet.",
            "Nothing to report. Savor it.",
            "I'm watching. You're welcome.",
            "Your network. My problem.",
        };
        static const char* OFFLINE[] = {
            "No network. I'll just sit here.",
            "Offline. Honestly, peaceful.",
            "Nothing to watch. Rude.",
        };
        static const char* LATE[] = {
            "It's late. Even demons sleep.",
            "Bed. Now. I'll keep watch.",
        };
        _wasOnline = WiFi.status() == WL_CONNECTED;
        struct tm t;
        if (MUI::localNow(t) && (t.tm_hour >= 21 || t.tm_hour < 5)) {
            _line = LATE[random(2)];
        } else if (_wasOnline) {
            _line = ONLINE[random(4)];
        } else {
            _line = OFFLINE[random(3)];
        }
    }

    void _draw() {
        _canvas.fillSprite(MUI::BG);
        bool online = WiFi.status() == WL_CONNECTED;
        // Mood: green aura = all clear while watching a network; calm
        // yellow while offline. (Alarm states come with detection.)
        uint16_t aura = online ? MUI::GREEN : MUI::YELLOW;

        MUI::header(_canvas, "PROXIMA", aura);
        MUI::soul(_canvas, 40, 68, 92, MUI::YELLOW, aura, _spin);

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

        int e = MUI::text(_canvas, MUI::small(), "Threats ", x, y, MUI::WHITE);
        e = MUI::text(_canvas, MUI::big(), "0", e, y - 2, MUI::GREEN);
        e = MUI::text(_canvas, MUI::small(), "    Watchlist ", e, y, MUI::WHITE);
        MUI::text(_canvas, MUI::small(), "0", e, y, MUI::YELLOW);
        y += 16;

        _canvas.drawFastHLine(x, y, SCREEN_W - x - 4, MUI::BONES_DIM);
        y += 3;
        MUI::text(_canvas, MUI::small(), "RECENT", x, y, MUI::DIM, 1);
        y += 13;
        MUI::text(_canvas, MUI::small(), "No sightings yet.", x, y, MUI::LABEL);
        y += 12;
        MUI::text(_canvas, MUI::small(), "The scanner wakes up soon.", x, y, MUI::DIM);

        char say[48];
        snprintf(say, sizeof(say), "\"%s\"", _line);
        MUI::footer(_canvas, say, MUI::CREAM, "ENT apps", aura);

        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }
};
