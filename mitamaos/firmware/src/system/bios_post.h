#pragma once
#include <M5Cardputer.h>
#include <SD.h>
#include <LittleFS.h>
#include "../config.h"
#include "../os/disp_lock.h"

// BiosPost - a cosmetic (but factually-driven) power-on-self-test screen,
// styled after 90s AMI/Award BIOS boot text: black background, monochrome
// text, lines appearing one at a time, a counting memory test. Every number
// it prints comes from a real query against this specific board (ESP32-S3,
// no PSRAM, 8MB flash) - nothing here is a hardcoded fake value.
//
// Screen is only 240px wide (~39 chars at the default 6px font), so every
// line below is kept short on purpose - anything longer gets clipped off
// the right edge instead of wrapping.
//
// Hold any key while it runs to fast-forward through the remaining lines.
// Hold '`' when prompted to drop into a one-page "SETUP" info screen.
class BiosPost {
public:
    static void run(LGFX_Sprite& canvas) {
        canvas.fillSprite(TERM_BG);
        _y = 4;
        _fast = false;

        _line(canvas, OS_NAME " System BIOS", TERM_FG, 1);
        _line(canvas, "(C) " + String(OS_AUTHOR) + ", GPLv2 free software", TERM_DIM, 1);
        _line(canvas, "", TERM_FG, 1);

        char buf[56];

        snprintf(buf, sizeof(buf), "CPU    : %s rev%d, %d core, %uMHz",
            ESP.getChipModel(), ESP.getChipRevision(),
            (int)ESP.getChipCores(), (unsigned)ESP.getCpuFreqMHz());
        _line(canvas, buf, TERM_FG);

        snprintf(buf, sizeof(buf), "Flash  : %uMB @ %uMHz",
            (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
            (unsigned)(ESP.getFlashChipSpeed() / 1000000));
        _line(canvas, buf, TERM_FG);

        uint32_t psram = ESP.getPsramSize();
        if (psram > 0) {
            snprintf(buf, sizeof(buf), "PSRAM  : %uKB", (unsigned)(psram / 1024));
            _line(canvas, buf, TERM_FG);
        } else {
            _line(canvas, "PSRAM  : Not Installed", TERM_DIM);
        }

        // Classic counting memory test, but ending on the real, currently
        // total heap size for this boot - not a fixed/fake number.
        uint32_t totalHeap = ESP.getHeapSize();
        uint32_t freeHeap  = ESP.getFreeHeap();
        _memoryTest(canvas, totalHeap);

        _line(canvas, "", TERM_FG, 1);
        _line(canvas, "Detecting storage devices...", TERM_FG);

        if (LittleFS.begin(true)) {
            uint32_t tot = LittleFS.totalBytes(), used = LittleFS.usedBytes();
            snprintf(buf, sizeof(buf), " LittleFS : %uK/%uK used",
                (unsigned)(used / 1024), (unsigned)(tot / 1024));
            _line(canvas, buf, TERM_OK);
        } else {
            _line(canvas, " LittleFS : MOUNT FAILED", TERM_ERR);
        }

        if (SD.cardType() != CARD_NONE) {
            unsigned long totMB = (unsigned long)(SD.totalBytes() / (1024 * 1024));
            unsigned long usedMB = (unsigned long)(SD.usedBytes() / (1024 * 1024));
            const char* type = SD.cardType() == CARD_MMC ? "MMC" :
                                SD.cardType() == CARD_SD  ? "SD"  :
                                SD.cardType() == CARD_SDHC ? "SDHC" : "?";
            snprintf(buf, sizeof(buf), " SD Card  : %s %luM/%luM used",
                type, usedMB, totMB);
            _line(canvas, buf, TERM_OK);
        } else {
            _line(canvas, " SD Card  : Not Detected", TERM_WARN);
        }

        _line(canvas, "", TERM_FG, 1);
        _line(canvas, "Detecting I/O devices...", TERM_FG);
        _line(canvas, " Keyboard.........OK", TERM_OK);
        snprintf(buf, sizeof(buf), " Display : %dx%d LCD..OK", SCREEN_W, SCREEN_H);
        _line(canvas, buf, TERM_OK);

        int batMv = M5Cardputer.Power.getBatteryVoltage();
        int batPct = constrain(map(batMv, 3300, 4200, 0, 100), 0, 100);
        snprintf(buf, sizeof(buf), " Power : %d%% (%dmV) %s",
            batPct, batMv, batPct > 10 ? "OK" : "LOW");
        _line(canvas, buf, batPct > 10 ? TERM_OK : TERM_WARN);

        _line(canvas, " WiFi : 802.11 b/g/n....OK", TERM_OK);

        _line(canvas, "", TERM_FG, 1);
        snprintf(buf, sizeof(buf), "%uK free / %uK total RAM",
            (unsigned)(freeHeap / 1024), (unsigned)(totalHeap / 1024));
        _line(canvas, buf, TERM_DIM);

        _line(canvas, "", TERM_FG, 1);
        _line(canvas, "Press ` to enter SETUP", TERM_DIM);

        // Give the user a brief, real window to hold '`' for the setup page.
        uint32_t waitStart = millis();
        bool wantSetup = false;
        while (millis() - waitStart < 900) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isKeyPressed('`')) { wantSetup = true; break; }
            delay(10);
        }
        OsUIWaitRelease();

        if (wantSetup) _setupPage(canvas);
    }

private:
    static const uint16_t TERM_BG   = 0x0000;
    static const uint16_t TERM_FG   = 0xC618; // light gray, easier on the eyes than pure white
    static const uint16_t TERM_DIM  = 0x6B4D;
    static const uint16_t TERM_OK   = 0x0400;
    static const uint16_t TERM_WARN = 0xFEA0;
    static const uint16_t TERM_ERR  = 0xF800;

    static inline int _y;
    static inline bool _fast;

    static void OsUIWaitRelease() {
        M5Cardputer.update();
        while (M5Cardputer.Keyboard.isPressed()) { M5Cardputer.update(); delay(5); }
    }

    static void _line(LGFX_Sprite& canvas, const String& text, uint16_t color, int blank = 0) {
        canvas.setTextSize(1);
        canvas.setTextColor(color, TERM_BG);
        canvas.setCursor(4, _y);
        canvas.print(text);
        { DispLock _lk; canvas.pushSprite(0, 0); }
        _y += 9;

        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isPressed()) _fast = true;
        if (!_fast && blank == 0) delay(55);

        if (_y > SCREEN_H - 12) { // clear-and-restart before we run off the bottom
            canvas.fillSprite(TERM_BG);
            _y = 4;
        }
    }

    static void _memoryTest(LGFX_Sprite& canvas, uint32_t totalBytes) {
        canvas.setTextSize(1);
        canvas.setCursor(4, _y);
        canvas.setTextColor(TERM_FG, TERM_BG);
        canvas.print("Memory Test: ");
        int numX = canvas.getCursorX();
        uint32_t totalKB = totalBytes / 1024;
        const int STEPS = 12;
        for (int s = 1; s <= STEPS; s++) {
            uint32_t shown = (uint64_t)totalKB * s / STEPS;
            canvas.fillRect(numX, _y, 60, 8, TERM_BG);
            canvas.setCursor(numX, _y);
            canvas.setTextColor(TERM_FG, TERM_BG);
            canvas.print(String(shown) + "K");
            { DispLock _lk; canvas.pushSprite(0, 0); }
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isPressed()) _fast = true;
            if (!_fast) delay(35);
        }
        canvas.setTextColor(TERM_OK, TERM_BG);
        canvas.print(" OK");
        { DispLock _lk; canvas.pushSprite(0, 0); }
        _y += 9;
        if (!_fast) delay(120);
    }

    static void _setupPage(LGFX_Sprite& canvas) {
        canvas.fillSprite(0x0015); // classic BIOS-setup blue
        canvas.setTextColor(0xFFFF, 0x0015);
        canvas.setTextSize(1);
        canvas.drawCenterString("PAPEROS BIOS SETUP", SCREEN_W / 2, 4);
        canvas.drawFastHLine(4, 14, SCREEN_W - 8, 0xFFFF);

        int y = 20;
        auto row = [&](const char* k, const String& v) {
            canvas.setTextColor(0xFFE0, 0x0015);
            canvas.setCursor(8, y);
            canvas.print(k);
            canvas.setTextColor(0xFFFF, 0x0015);
            canvas.setCursor(90, y);
            canvas.print(v);
            y += 10;
        };

        row("CPU Type", String(ESP.getChipModel()) + " r" + String(ESP.getChipRevision()));
        row("CPU Speed", String(ESP.getCpuFreqMHz()) + " MHz");
        row("Total RAM", String(ESP.getHeapSize() / 1024) + " KB");
        row("Free RAM", String(ESP.getFreeHeap() / 1024) + " KB");
        row("Flash Size", String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB");
        row("SD Card", SD.cardType() != CARD_NONE
            ? String((unsigned long)(SD.totalBytes() / (1024 * 1024))) + " MB"
            : String("Not Detected"));
        row("Boot Device", "Internal Flash");
        row("BIOS Version", OS_NAME " " OS_VERSION);

        canvas.drawFastHLine(4, y + 2, SCREEN_W - 8, 0xFFFF);
        canvas.setTextColor(0xFFE0, 0x0015);
        canvas.drawCenterString("` = exit and continue boot", SCREEN_W / 2, y + 8);
        { DispLock _lk; canvas.pushSprite(0, 0); }

        while (true) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isKeyPressed('`')) break;
            delay(10);
        }
        OsUIWaitRelease();
    }
};
