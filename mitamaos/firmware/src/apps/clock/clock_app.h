#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <time.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../config.h"

//  ClockApp - clock / timer / stopwatch
//  Modes: CLOCK (NTP time or uptime), TIMER (countdown),
//         STOPWATCH
//  Keys: TAB=switch mode, ENT=start/stop, R=reset,
//        in TIMER: +/- change minutes, `=home
class ClockApp : public App {
public:
    ClockApp(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Clock"; }
    const char* getIcon() const override { return "T"; }
    const char* getDesc() const override { return "Clock, Timer, Stopwatch"; }

    void onStart() override {
        _mode      = MODE_CLOCK;
        _swRunning = false;
        _swElapsed = 0;
        _swStart   = 0;
        _tmRunning = false;
        _tmSeconds = 5 * 60;
        _tmRemain  = _tmSeconds;
        _tmStart   = 0;
        _tmDone    = false;
        _keyTimer  = 0;
        _lastTick  = millis();

        // Try NTP sync if WiFi is available
        if (WiFi.status() == WL_CONNECTED && !_ntpDone) {
            configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            _ntpDone = true;
        }

        _draw();
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();

        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            _draw();
            _canvas.pushSprite(0, 0);
            if (OsUI::confirmExit(_canvas)) {
                AppManager::get().goHome();
            } else {
                _draw();
            }
            return;
        }

        auto st = M5Cardputer.Keyboard.keysState();
        bool redraw = false;

        // TAB - switch mode
        if (st.tab) {
            _mode = (Mode)((_mode + 1) % 3);
            OsUI::waitRelease();
            redraw = true;
        }

        // ENT - start/stop
        if (st.enter) {
            OsUI::waitRelease();
            if (_mode == MODE_STOPWATCH) {
                if (_swRunning) {
                    _swElapsed += millis() - _swStart;
                    _swRunning = false;
                } else {
                    _swStart = millis();
                    _swRunning = true;
                }
                redraw = true;
            } else if (_mode == MODE_TIMER) {
                if (_tmDone) {
                    _tmRemain  = _tmSeconds;
                    _tmRunning = false;
                    _tmDone    = false;
                } else if (_tmRunning) {
                    _tmRunning = false;
                } else {
                    _tmRemain  = _tmSeconds;
                    _tmStart   = millis();
                    _tmRunning = true;
                    _tmDone    = false;
                }
                redraw = true;
            }
        }

        // R - reset
        if (M5Cardputer.Keyboard.isKeyPressed('r') && millis() - _keyTimer > 200) {
            _keyTimer = millis();
            if (_mode == MODE_STOPWATCH) {
                _swRunning = false; _swElapsed = 0; redraw = true;
            } else if (_mode == MODE_TIMER) {
                _tmRunning = false; _tmDone = false;
                _tmRemain  = _tmSeconds; redraw = true;
            }
        }

        // +/- in the timer - change preset (only while stopped)
        if (_mode == MODE_TIMER && !_tmRunning) {
            if ((M5Cardputer.Keyboard.isKeyPressed('+') ||
                 M5Cardputer.Keyboard.isKeyPressed('=')) &&
                millis() - _keyTimer > 150) {
                if (_tmSeconds < 99 * 60) _tmSeconds += 60;
                _tmRemain = _tmSeconds;
                _keyTimer = millis(); redraw = true;
            }
            if (M5Cardputer.Keyboard.isKeyPressed('-') &&
                millis() - _keyTimer > 150) {
                if (_tmSeconds > 60) _tmSeconds -= 60;
                _tmRemain = _tmSeconds;
                _keyTimer = millis(); redraw = true;
            }
        }

        // Tick every 500 ms
        if (millis() - _lastTick >= 500) {
            _lastTick = millis();
            redraw = true;

            // Update the timer
            if (_tmRunning) {
                uint32_t elapsed = (millis() - _tmStart) / 1000;
                if (elapsed >= (uint32_t)_tmSeconds) {
                    _tmRemain  = 0;
                    _tmRunning = false;
                    _tmDone    = true;
                } else {
                    _tmRemain = _tmSeconds - (int)elapsed;
                }
            }
        }

        if (redraw) _draw();
    }

private:
    enum Mode { MODE_CLOCK = 0, MODE_STOPWATCH, MODE_TIMER };

    LGFX_Sprite& _canvas;
    Mode     _mode      = MODE_CLOCK;
    uint32_t _lastTick  = 0;
    uint32_t _keyTimer  = 0;
    bool     _ntpDone   = false;

    // Stopwatch
    bool     _swRunning = false;
    uint32_t _swElapsed = 0;
    uint32_t _swStart   = 0;

    // Timer
    bool     _tmRunning = false;
    bool     _tmDone    = false;
    int      _tmSeconds = 300;
    int      _tmRemain  = 300;
    uint32_t _tmStart   = 0;

    void _draw() {
        _canvas.startWrite();
        _canvas.fillSprite(COL_BG);

        const char* modeNames[] = { "Clock", "Stopwatch", "Timer" };
        OsUI::drawHeader(_canvas, modeNames[_mode], COL_HEADER);

    // Tabs
        for (int i = 0; i < 3; i++) {
            int tx = 4 + i * 78;
            bool active = (i == (int)_mode);
            _canvas.fillRect(tx, CONTENT_Y + 2, 74, 14,
                active ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG2);
            _canvas.drawRect(tx, CONTENT_Y + 2, 74, 14,
                active ? (uint16_t)COL_ACCENT : (uint16_t)COL_DIM);
            _canvas.setTextSize(1);
            _canvas.setTextColor(
                active ? (uint16_t)WHITE : (uint16_t)COL_DIM,
                active ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG2);
            int tlen = strlen(modeNames[i]) * 6;
            _canvas.setCursor(tx + (74 - tlen) / 2, CONTENT_Y + 5);
            _canvas.print(modeNames[i]);
        }

        int cy = CONTENT_Y + 24;

        if      (_mode == MODE_CLOCK)     _drawClock(cy);
        else if (_mode == MODE_STOPWATCH) _drawStopwatch(cy);
        else                              _drawTimer(cy);

        _canvas.pushSprite(0, 0);
        _canvas.endWrite();
    }

    void _drawClock(int cy) {
        struct tm ti;
        bool hasTime = getLocalTime(&ti);

        if (hasTime) {
            char timebuf[10];
            snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d",
                     ti.tm_hour, ti.tm_min, ti.tm_sec);
            _canvas.setTextSize(3);
            _canvas.setTextColor(COL_TEXT, COL_BG);
            int tw = strlen(timebuf) * 18;
            _canvas.setCursor((SCREEN_W - tw) / 2, cy + 4);
            _canvas.print(timebuf);

            char datebuf[14];
            snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d",
                     ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
            _canvas.setTextSize(1);
            _canvas.setTextColor(COL_DIM, COL_BG);
            int dw = strlen(datebuf) * 6;
            _canvas.setCursor((SCREEN_W - dw) / 2, cy + 42);
            _canvas.print(datebuf);

            const char* wdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
            _canvas.setTextColor(COL_ACCENT, COL_BG);
            _canvas.setCursor((SCREEN_W - 18) / 2, cy + 56);
            _canvas.print(wdays[ti.tm_wday]);
        } else {
            // NTP not yet synced - show uptime
            uint32_t sec = millis() / 1000;
            char buf[12];
            snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                     (unsigned)(sec/3600), (unsigned)((sec%3600)/60), (unsigned)(sec%60));
            _canvas.setTextSize(3);
            _canvas.setTextColor(COL_WARN, COL_BG);
            int tw = strlen(buf) * 18;
            _canvas.setCursor((SCREEN_W - tw) / 2, cy + 10);
            _canvas.print(buf);

            _canvas.setTextSize(1);
            _canvas.setTextColor(COL_DIM, COL_BG);
            const char* msg = WiFi.status() == WL_CONNECTED
                ? "Syncing NTP..." : "No WiFi - uptime";
            _canvas.setCursor((SCREEN_W - strlen(msg)*6) / 2, cy + 46);
            _canvas.print(msg);
        }

        OsUI::drawFooter(_canvas, "TAB=mode  `=home");
    }

    void _drawStopwatch(int cy) {
        uint32_t total = _swElapsed;
        if (_swRunning) total += millis() - _swStart;

        uint32_t ms   = total % 1000;
        uint32_t secs = (total / 1000) % 60;
        uint32_t mins = (total / 60000) % 60;
        uint32_t hrs  = total / 3600000;

        char buf[16];
        if (hrs > 0)
            snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                     (unsigned)hrs, (unsigned)mins, (unsigned)secs);
        else
            snprintf(buf, sizeof(buf), "%02u:%02u.%02u",
                     (unsigned)mins, (unsigned)secs, (unsigned)(ms/10));

        _canvas.setTextSize(3);
        _canvas.setTextColor(_swRunning ? (uint16_t)COL_OK : (uint16_t)COL_TEXT, COL_BG);
        int tw = strlen(buf) * 18;
        _canvas.setCursor((SCREEN_W - tw) / 2, cy + 10);
        _canvas.print(buf);

        _canvas.setTextSize(1);
        _canvas.setTextColor(COL_DIM, COL_BG);
        const char* state = _swRunning ? "RUNNING" : "STOPPED";
        _canvas.setCursor((SCREEN_W - (int)strlen(state)*6) / 2, cy + 48);
        _canvas.print(state);

        OsUI::drawFooter(_canvas, "ENT=start/stop  R=reset  TAB=mode");
    }

    void _drawTimer(int cy) {
        int mins = _tmRemain / 60;
        int secs = _tmRemain % 60;

        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);

        uint16_t col = _tmDone    ? (uint16_t)COL_ERR :
                       _tmRunning ? (uint16_t)COL_OK  : (uint16_t)COL_TEXT;
        _canvas.setTextSize(3);
        _canvas.setTextColor(col, COL_BG);
        int tw = strlen(buf) * 18;
        _canvas.setCursor((SCREEN_W - tw) / 2, cy + 6);
        _canvas.print(buf);

        _canvas.setTextSize(1);
        if (_tmDone) {
            if ((millis() / 500) % 2) {
                _canvas.setTextColor(COL_ERR, COL_BG);
                _canvas.setCursor((SCREEN_W - 30) / 2, cy + 48);
                _canvas.print("DONE!");
            }
            OsUI::drawFooter(_canvas, "ENT=reset  TAB=mode  `=home");
        } else if (_tmRunning) {
            _canvas.setTextColor(COL_OK, COL_BG);
            _canvas.setCursor((SCREEN_W - 42) / 2, cy + 48);
            _canvas.print("RUNNING");
            OsUI::drawFooter(_canvas, "ENT=stop  R=reset  TAB=mode");
        } else {
            char pre[18];
            snprintf(pre, sizeof(pre), "Set: %02d:%02d",
                     _tmSeconds/60, _tmSeconds%60);
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.setCursor((SCREEN_W - (int)strlen(pre)*6) / 2, cy + 48);
            _canvas.print(pre);
            OsUI::drawFooter(_canvas, "+/-=min  ENT=start  TAB=mode");
        }
    }
};