#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <SD.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/mem_guard.h"
#include "../../config.h"
#include "../../system/paper_clock.h"

class SysInfo : public App {
public:
    SysInfo(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "SysInfo"; }
    const char* getIcon() const override { return "I"; }
    const char* getDesc() const override { return "System info"; }

    void onStart() override { _draw(); _timer = millis(); }
    void onStop()  override {}

    void update() override {
        M5Cardputer.update();

        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            _draw();
            _canvas.pushSprite(0, 0);
            if (OsUI::confirmExit(_canvas)) {
                AppManager::get().goHome();
            } else { _draw(); }
            return;
        }

        if (millis() - _timer > 2000) {
            _timer = millis();
            _draw();
        }
    }

private:
    LGFX_Sprite& _canvas;
    uint32_t _timer = 0;

    void _draw() {
        _canvas.startWrite();
        _canvas.fillSprite(COL_BG);
        OsUI::drawHeader(_canvas, "SysInfo  v" OS_VERSION, COL_HEADER);

        _canvas.setTextSize(1);
        int y = CONTENT_Y + 4;
        const int LH = 13;

        // Uptime
        String timeStr = PaperClock::getTimeString();
        String dateStr = PaperClock::getDateString();
        _drawRow(y, "Time:", timeStr.c_str(), PaperClock::isSynced() ? (uint16_t)COL_OK : (uint16_t)COL_DIM);
        y += LH;
        _drawRow(y, "Date:", dateStr.c_str(), COL_TEXT);
        y += LH;

        // RAM - current free / total, plus the lowest free-heap value MemGuard
        // has ever observed (a system can look fine at rest and still have
        // come close to OOM during a transient spike).
        uint32_t freeHeap  = ESP.getFreeHeap();
        uint32_t totalHeap = ESP.getHeapSize();
        uint32_t minHeap   = MemGuard::minFreeHeap();
        char rambuf[24];
        snprintf(rambuf, sizeof(rambuf), "%uK/%uK min%uK",
            (unsigned)(freeHeap/1024), (unsigned)(totalHeap/1024),
            (unsigned)(minHeap == UINT32_MAX ? freeHeap/1024 : minHeap/1024));
        _drawRow(y, "RAM:", rambuf,
            freeHeap > 100000 ? (uint16_t)COL_OK : (uint16_t)COL_WARN);
        y += LH;

        // CPU
        char cpubuf[24];
        snprintf(cpubuf, sizeof(cpubuf), "%uMHz %s",
            (unsigned)ESP.getCpuFreqMHz(), ESP.getChipModel());
        _drawRow(y, "CPU:", cpubuf, COL_TEXT);
        y += LH;

        // Battery
        int batMv  = M5Cardputer.Power.getBatteryVoltage();
        int batPct = constrain(map(batMv, 3300, 4200, 0, 100), 0, 100);
        char batbuf[16];
        snprintf(batbuf, sizeof(batbuf), "%d%%  %dmV", batPct, batMv);
        _drawRow(y, "Battery:", batbuf,
            batPct > 50 ? (uint16_t)COL_OK : batPct > 20 ? (uint16_t)COL_WARN : (uint16_t)COL_ERR);
        y += LH;

        // WiFi
        if (WiFi.status() == WL_CONNECTED) {
            String ssid = WiFi.SSID();
            if (ssid.length() > 12) ssid = ssid.substring(0, 11) + "~";
            char wibuf[22];
            snprintf(wibuf, sizeof(wibuf), "%s %ddBm", ssid.c_str(), WiFi.RSSI());
            _drawRow(y, "WiFi:", wibuf, COL_OK); y += LH;
            _drawRow(y, "IP:", WiFi.localIP().toString().c_str(), COL_ACCENT);
        } else {
            _drawRow(y, "WiFi:", "Not connected", COL_ERR);
        }
        y += LH;

        // SD
        if (SD.cardType() != CARD_NONE) {
            char sdbuf[24];
            snprintf(sdbuf, sizeof(sdbuf), "%uMB/%uMB",
                (unsigned)((SD.totalBytes()-SD.usedBytes())/(1024*1024)),
                (unsigned)(SD.totalBytes()/(1024*1024)));
            _drawRow(y, "SD free:", sdbuf, COL_OK);
        } else {
            _drawRow(y, "SD:", "Not inserted", COL_DIM);
        }

        // Uptime
        uint32_t sec = millis() / 1000;
        char upbuf[12];
        snprintf(upbuf, sizeof(upbuf), "%02u:%02u:%02u",
            (unsigned)(sec/3600), (unsigned)((sec%3600)/60), (unsigned)(sec%60));
        _canvas.setTextColor(COL_DIM, COL_BG);
        _canvas.setCursor(SCREEN_W - 56, CONTENT_Y + 4);
        _canvas.print(upbuf);

        if (MemGuard::lastWarning()[0] != '\0') {
            char warnbuf[48];
            snprintf(warnbuf, sizeof(warnbuf), "! %s  `=home", MemGuard::lastWarning());
            OsUI::drawFooter(_canvas, warnbuf);
        } else {
            OsUI::drawFooter(_canvas, "Auto-refresh 2s  `=home");
        }
        _canvas.pushSprite(0, 0);
        _canvas.endWrite();
    }

    void _drawRow(int y, const char* label, const char* val, uint16_t valCol) {
        _canvas.setTextColor(COL_DIM, COL_BG);
        _canvas.setCursor(4, y);
        _canvas.print(label);
        _canvas.setTextColor(valCol, COL_BG);
        char buf[22];
        strncpy(buf, val, 21); buf[21] = '\0';
        _canvas.setCursor(72, y);
        _canvas.print(buf);
    }
};
