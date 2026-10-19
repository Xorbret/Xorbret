#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <SD.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/disp_lock.h"
#include "../../config.h"
#include "../../ui/mitama_ui.h"
#include "../../system/gps.h"
#include "../../detect/scry.h"
#include "../../detect/detection.h"
#include "../../net/wigle.h"

// Scry - war driving. Records every Wi-Fi network and Bluetooth device the
// watcher hears, with its GPS position, to WiGLE CSV logs on the SD card,
// and uploads them to a WiGLE account. Recording keeps going in the
// background after leaving the app; the logs are in /MitamaOS/logs/scry/,
// and uploaded ones move to its uploaded/ folder.
//
// Keys: Enter start/stop, U upload, A WiGLE account, ` back.
class ScryApp : public App {
public:
    ScryApp(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Scry"; }
    const char* getIcon() const override { return "E"; }
    const char* getDesc() const override { return "War driving + WiGLE"; }

    void onStart() override {
        OsUI::waitRelease();
        _msg = ""; _tick = 0;
        _pending = _countPending();
        _draw();
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();
        auto& kb = M5Cardputer.Keyboard;
        auto& log = ScryLogger::get();

        if (kb.isKeyPressed('`')) { OsUI::waitRelease(); AppManager::get().goHome(); return; }

        if (kb.keysState().enter) {
            OsUI::waitRelease();
            if (log.recording()) {
                log.stop();
                _say("Stopped. Log saved.", MUI::GREEN);
                delay(150);
                _pending = _countPending();
            } else {
                const char* why = "";
                if (log.start(&why)) _say("Recording.", MUI::GREEN);
                else                 _say(why, MUI::BONES);
            }
        }
        if (kb.isKeyPressed('u')) { OsUI::waitRelease(); _uploadAll(); }
        if (kb.isKeyPressed('a')) { OsUI::waitRelease(); _account(); }

        if (++_tick % 25 == 0 || _dirty) { _dirty = false; _draw(); }
    }

private:
    LGFX_Sprite& _canvas;
    uint32_t _tick = 0;
    bool     _dirty = true;
    String   _msg;
    uint16_t _msgCol = MUI::DIM;
    int      _pending = 0;

    void _say(const char* m, uint16_t col) { _msg = m; _msgCol = col; _dirty = true; }

    static bool _isLog(const char* n) {
        size_t l = strlen(n);
        return l > 4 && strcmp(n + l - 4, ".csv") == 0;
    }

    int _countPending() {
        File d = SD.open(ScryLogger::DIR);
        if (!d) return 0;
        int n = 0;
        for (File f = d.openNextFile(); f; f = d.openNextFile()) {
            if (!f.isDirectory() && _isLog(f.name())) n++;
            f.close();
        }
        d.close();
        return n;
    }

    // Typed in, or read from /MitamaOS/wigle.txt ("APIname:APItoken") so the
    // long token doesn't have to be typed on the keyboard.
    void _account() {
        Wigle::Account a;
        File f = SD.open("/MitamaOS/wigle.txt", FILE_READ);
        if (f) {
            String s = f.readString();
            f.close();
            s.trim();
            int c = s.indexOf(':');
            if (c > 0) { a.name = s.substring(0, c); a.token = s.substring(c + 1); a.token.trim(); }
        }
        if (!a.ok()) {
            a.name = OsUI::inputText(_canvas, "WiGLE API name:");
            if (a.name.length()) a.token = OsUI::inputText(_canvas, "WiGLE API token:");
        }
        if (a.ok()) { Wigle::save(a); _say("WiGLE account saved.", MUI::GREEN); }
        else          _say("Account unchanged.", MUI::DIM);
        OsUI::waitRelease();
        _dirty = true;
    }

    void _uploadAll() {
        auto acct = Wigle::load();
        if (!acct.ok())               { _say("Add your WiGLE account first (A).", MUI::YELLOW); return; }
        if (WiFi.status() != WL_CONNECTED) { _say("Join Wi-Fi in Settings first.", MUI::YELLOW); return; }
        if (ScryLogger::get().recording()) { _say("Stop recording first.", MUI::YELLOW); return; }

        // TLS needs ~40KB: pause the radios (frees Bluetooth's memory).
        auto& det = DetectionEngine::get();
        det.hold(true);
        for (int i = 0; i < 40 && !det.paused(); i++) delay(50);

        String upDir = String(ScryLogger::DIR) + "/uploaded";
        if (!SD.exists(upDir.c_str())) SD.mkdir(upDir.c_str());

        int ok = 0, fail = 0;
        String lastErr;
        // Collect names first; renaming while iterating a directory is unsafe.
        String names[24]; int n = 0;
        File d = SD.open(ScryLogger::DIR);
        for (File f = d ? d.openNextFile() : File(); f && n < 24; f = d.openNextFile()) {
            if (!f.isDirectory() && _isLog(f.name())) names[n++] = f.name();
            f.close();
        }
        if (d) d.close();

        for (int i = 0; i < n; i++) {
            String nm = names[i];
            int s = nm.lastIndexOf('/'); if (s >= 0) nm = nm.substring(s + 1);
            String path = String(ScryLogger::DIR) + "/" + nm;
            char line[48];
            snprintf(line, sizeof(line), "Uploading %d of %d...", i + 1, n);
            _say(line, MUI::CYAN); _draw();
            String msg;
            if (Wigle::upload(path.c_str(), acct, msg)) {
                SD.rename(path.c_str(), (upDir + "/" + nm).c_str());
                ok++;
            } else {
                fail++; lastErr = msg;
                if (msg.startsWith("WiGLE rejected")) break;   // same for every file
            }
        }
        det.hold(false);
        _pending = _countPending();
        char out[64];
        if (n == 0)          snprintf(out, sizeof(out), "No logs to upload.");
        else if (fail == 0)  snprintf(out, sizeof(out), "Uploaded %d log%s to WiGLE.", ok, ok == 1 ? "" : "s");
        else                 snprintf(out, sizeof(out), "%d up, %d failed: %s", ok, fail, lastErr.c_str());
        _say(out, fail ? MUI::BONES : MUI::GREEN);
    }

    void _draw() {
        auto& g = Gps::get();
        auto& log = ScryLogger::get();
        const auto& fx = g.fix();
        _canvas.fillSprite(MUI::BG);
        uint16_t mood = log.recording() ? MUI::RED : MUI::YELLOW;
        MUI::header(_canvas, "SCRY", mood);
        MUI::sigil(_canvas, MUI::SIG_SCRY, 30, 52, 40, log.recording() ? MUI::RED : MUI::CYAN);

        const int x = 64;
        int y = HEADER_H + 3;
        char buf[64];
        int e = MUI::text(_canvas, MUI::small(), "GPS ", x, y, MUI::WHITE);
        if (!g.present()) {
            MUI::text(_canvas, MUI::small(), "not found - is the cap on?", e, y, MUI::BONES);
        } else if (!g.hasFix()) {
            snprintf(buf, sizeof(buf), "searching (%u sats)", fx.sats);
            MUI::text(_canvas, MUI::small(), buf, e, y, MUI::YELLOW);
        } else {
            snprintf(buf, sizeof(buf), "fix  %u sats  +/-%.0fm", fx.sats, g.accuracyM());
            MUI::text(_canvas, MUI::small(), buf, e, y, MUI::GREEN);
        }
        y += 13;
        if (g.hasFix()) {
            snprintf(buf, sizeof(buf), "%.5f, %.5f", fx.lat, fx.lon);
            MUI::text(_canvas, MUI::small(), buf, x, y, MUI::DIM);
        }
        y += 14;

        e = MUI::text(_canvas, MUI::small(), "LOG ", x, y, MUI::WHITE);
        if (log.recording()) {
            MUI::text(_canvas, MUI::small(), g.hasFix() ? "recording" : "waiting for GPS fix", e, y,
                      g.hasFix() ? MUI::RED : MUI::YELLOW);
            y += 13;
            snprintf(buf, sizeof(buf), "Wi-Fi %lu   BLE %lu", (unsigned long)log.wifiCount(), (unsigned long)log.bleCount());
            MUI::text(_canvas, MUI::big(), buf, x, y - 1, MUI::CYAN);
        } else {
            MUI::text(_canvas, MUI::small(), "stopped", e, y, MUI::DIM);
            y += 13;
            snprintf(buf, sizeof(buf), "%d log%s waiting for WiGLE", _pending, _pending == 1 ? "" : "s");
            MUI::text(_canvas, MUI::small(), buf, x, y, MUI::LABEL);
        }
        y += 16;
        if (WiFi.status() == WL_CONNECTED && log.recording())
            MUI::text(_canvas, MUI::small(), "On Wi-Fi: hearing one channel only", x, y, MUI::DIM);
        else if (_msg.length()) {
            _canvas.setClipRect(x, y, SCREEN_W - 3 - x, 13);
            MUI::text(_canvas, MUI::small(), _msg.c_str(), x, y, _msgCol);
            _canvas.clearClipRect();
        }

        MUI::footer(_canvas, log.recording() ? "ENT stop  U upload  A account" : "ENT record  U upload  A account",
                    MUI::DIM, nullptr, mood);
        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }
};
