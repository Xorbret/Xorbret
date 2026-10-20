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
#include "../../net/update.h"
#include "../../system/crash_log.h"

// Molt - updates MitamaOS. Checks the MitamaOS site for a newer build and
// downloads it to the SD card (/MitamaOS/update/), then restarts so it can
// be installed from Launcher > SD. Nothing on the computer needed.
//
// Keys: Enter check / download, R restart, ` back.
class MoltApp : public App {
public:
    MoltApp(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Molt"; }
    const char* getIcon() const override { return "U"; }
    const char* getDesc() const override { return "Update MitamaOS"; }

    void onStart() override {
        OsUI::waitRelease();
        _state = S_IDLE; _latest = ""; _path = ""; _pct = -1;
        _say("Press Enter to look for an update.", MUI::DIM);
        _draw();
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();
        auto& kb = M5Cardputer.Keyboard;
        if (kb.isKeyPressed('`')) { OsUI::waitRelease(); AppManager::get().goHome(); return; }
        if (kb.isKeyPressed('r') && _state == S_READY) { OsUI::waitRelease(); ESP.restart(); }
        if (kb.keysState().enter) {
            OsUI::waitRelease();
            if (_state == S_IDLE || _state == S_CURRENT) _check();
            else if (_state == S_NEWER) _download();
            _draw();
        }
    }

private:
    enum State : uint8_t { S_IDLE, S_CURRENT, S_NEWER, S_READY };
    LGFX_Sprite& _canvas;
    State    _state = S_IDLE;
    String   _latest, _path, _msg;
    uint16_t _msgCol = MUI::DIM;
    int      _pct = -1;
    static inline MoltApp* _self = nullptr;

    void _say(const String& m, uint16_t col) { _msg = m; _msgCol = col; }

    bool _online() {
        if (WiFi.status() == WL_CONNECTED) return true;
        _say("Join Wi-Fi in Settings first.", MUI::YELLOW);
        return false;
    }

    // TLS needs ~40 KB: pause the detection radios while talking to the site.
    struct Hold {
        Hold()  { auto& d = DetectionEngine::get(); d.hold(true); for (int i = 0; i < 40 && !d.paused(); i++) delay(50); }
        ~Hold() { DetectionEngine::get().hold(false); }
    };

    void _check() {
        if (!_online()) return;
        _say("Looking...", MUI::CYAN); _draw();
        String msg;
        CrashLog::crumb("molt check");
        { Hold h; _latest = OsUpdate::latest(msg); }
        CrashLog::done();
        if (!_latest.length()) { _say(msg, MUI::BONES); return; }
        if (_latest == OsUpdate::current()) {
            _state = S_CURRENT;
            _say("You have the newest build.", MUI::GREEN);
        } else {
            _state = S_NEWER;
            _say("New build found. Enter downloads it.", MUI::YELLOW);
        }
    }

    static void _progress(size_t done, size_t total) {
        int pct = (int)(done * 100ULL / total);
        if (!_self || pct == _self->_pct) return;
        _self->_pct = pct;
        _self->_draw();
    }

    void _download() {
        if (!_online()) return;
        _self = this; _pct = 0;
        _say("Downloading...", MUI::CYAN); _draw();
        String msg;
        CrashLog::crumb("molt download");
        { Hold h; _path = OsUpdate::download(_latest, &MoltApp::_progress, msg); }
        CrashLog::done();
        _pct = -1;
        if (!_path.length()) { _say(msg, MUI::BONES); return; }
        _state = S_READY;
        _say("Saved. R restarts; install it from Launcher > SD.", MUI::GREEN);
    }

    void _draw() {
        _canvas.fillSprite(MUI::BG);
        MUI::header(_canvas, "MOLT", MUI::GREEN);
        MUI::sigil(_canvas, MUI::SIG_MOLT, 30, 52, 40, MUI::GREEN);
        const int x = 64;
        int y = HEADER_H + 3;
        char buf[64];
        int e = MUI::text(_canvas, MUI::small(), "THIS ", x, y, MUI::WHITE);
        MUI::text(_canvas, MUI::small(), OsUpdate::current(), e, y, MUI::LABEL);
        y += 13;
        e = MUI::text(_canvas, MUI::small(), "NEWEST ", x, y, MUI::WHITE);
        MUI::text(_canvas, MUI::small(), _latest.length() ? _latest.c_str() : "?", e, y,
                  _state == S_NEWER || _state == S_READY ? MUI::YELLOW : MUI::LABEL);
        y += 16;
        if (_pct >= 0) {
            snprintf(buf, sizeof(buf), "%d%%", _pct);
            int w = SCREEN_W - 3 - x;
            _canvas.drawRect(x, y, w, 9, MUI::DIM);
            _canvas.fillRect(x + 1, y + 1, (w - 2) * _pct / 100, 7, MUI::CYAN);
            y += 13;
        }
        if (_state == S_READY) {
            String f = _path.substring(_path.lastIndexOf('/') + 1);
            MUI::text(_canvas, MUI::small(), ("update/" + f).c_str(), x, y, MUI::CYAN);
            y += 13;
        }
        _canvas.setClipRect(x, y, SCREEN_W - 3 - x, 26);
        MUI::text(_canvas, MUI::small(), _msg.c_str(), x, y, _msgCol);
        _canvas.clearClipRect();

        const char* foot = _state == S_NEWER ? "ENT download" : _state == S_READY ? "R restart" : "ENT check";
        MUI::footer(_canvas, foot, MUI::DIM, nullptr, MUI::GREEN);
        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }
};
