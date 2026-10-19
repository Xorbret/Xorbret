#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/disp_lock.h"
#include "../../config.h"
#include "../../ui/mitama_ui.h"
#include "../../detect/detection.h"

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
            int threats = DetectionEngine::get().stats().threats;
            if (online != _wasOnline || (threats > 0) != _wasAlarm || _tick >= 50 * 45) { _tick = 0; _pickLine(); }
        }

        if (_needRedraw) { _needRedraw = false; _draw(); }
    }

private:
    LGFX_Sprite& _canvas;
    bool     _needRedraw = true;
    uint32_t _tick = 0;
    bool     _wasOnline = false;
    bool     _wasAlarm = false;
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
        static const char* ALARM[] = {
            "Something's hostile out there.",
            "Not friendly. Stay sharp.",
            "Hostile signal. I see it.",
        };
        static const char* LATE[] = {
            "It's late. Even demons sleep.",
            "Bed. Now. I'll keep watch.",
        };
        _wasOnline = WiFi.status() == WL_CONNECTED;
        _wasAlarm = DetectionEngine::get().stats().threats > 0;
        struct tm t;
        if (_wasAlarm) {
            _line = ALARM[random(3)];
        } else if (MUI::localNow(t) && (t.tm_hour >= 21 || t.tm_hour < 5)) {
            _line = LATE[random(2)];
        } else if (_wasOnline) {
            _line = ONLINE[random(4)];
        } else {
            _line = OFFLINE[random(3)];
        }
    }

    static uint16_t _raceColor(uint8_t type) {
        switch (Sig::raceOf(type)) {
            case Sig::R_VILE:   return MUI::YELLOW;
            case Sig::R_NIGHT:  return MUI::PURPLE;
            case Sig::R_FAIRY:  return MUI::MAGENTA;
            case Sig::R_AVIAN:
            case Sig::R_HERALD: return MUI::CYAN;
            case Sig::R_FOUL:   return MUI::BONES;
            case Sig::R_JAKI:   return MUI::GREEN;
            default:            return MUI::RED;
        }
    }

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
        uint16_t aura = st.threats ? MUI::RED : st.watch ? MUI::CYAN : online ? MUI::GREEN : MUI::YELLOW;

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
        int ew = MUI::width(_canvas, MUI::small(), ears);
        MUI::text(_canvas, MUI::small(), ears, SCREEN_W - 4 - ew, y, MUI::DIM);
        y += 13;
        if (st.recentN == 0) {
            MUI::text(_canvas, MUI::small(), "No sightings yet.", x, y, MUI::LABEL);
            y += 12;
            MUI::text(_canvas, MUI::small(), "Listening...", x, y, MUI::DIM);
        }
        for (int i = 0; i < st.recentN; i++) {
            const auto& r = st.recent[i];
            char age[8];
            _age(age, sizeof(age), st.nowMs - r.lastMs);
            int aw = MUI::width(_canvas, MUI::small(), age);
            int right = SCREEN_W - 4 - aw;
            uint16_t col = _raceColor(r.type);
            _canvas.setClipRect(x, y, right - 4 - x, 13);
            int ex = MUI::text(_canvas, MUI::small(), Sig::raceName(Sig::raceOf(r.type)), x, y, col);
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

        char say[48];
        snprintf(say, sizeof(say), "\"%s\"", _line);
        MUI::footer(_canvas, say, MUI::CREAM, "ENT apps", aura);

        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }
};
