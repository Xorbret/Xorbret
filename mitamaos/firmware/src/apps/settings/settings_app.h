#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <Preferences.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/wifi_manager.h"
#include "../../config.h"
#include "../../system/paper_clock.h"
#include "../../system/ota_tools.h"

//  Settings - PaperOS v1.1 system settings
//
//  Sections:
//    1. Timezone  - pick UTC offset (-12..+14), saved for NTP
//    2. Display   - brightness (10-100%)
//    3. Network   - WiFi slots (Win95 "Control Panel -> Network" spot)
//    4. Games     - PaperEMU OTA slot
//    5. About     - OS info
//
//  Keys: ;/. navigate, ENT select, ` exit
class SettingsApp : public App {
public:
    SettingsApp(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Settings"; }
    const char* getIcon() const override { return "S"; }
    const char* getDesc() const override { return "System settings"; }

    void onStart() override {
        _section = 0;   // 0=menu, 1=tz, 2=bright, 3=games, 4=about, 5=network
        _menuSel = 0;
        _menuScroll = 0;
        _tzSel   = _findCurrentTZ();
        _tzScroll= 0;
        _bright  = _loadBrightness();
        _netSel  = 0;
        _draw();
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();

        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            if (_section != 0) {
                _section = 0;
                OsUI::waitRelease();
                _draw();
                return;
            }
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

        if (_section == 0) {
            if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                if (_menuSel > 0) {
                    _menuSel--;
                    if (_menuSel < _menuScroll) _menuScroll = _menuSel;
                }
                delay(KEY_REPEAT_MS); _draw();
            }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                if (_menuSel < _menuCount - 1) {
                    _menuSel++;
                    if (_menuSel >= _menuScroll + MENU_VISIBLE) _menuScroll = _menuSel - MENU_VISIBLE + 1;
                }
                delay(KEY_REPEAT_MS); _draw();
            }
            if (st.enter) {
                OsUI::waitRelease();
                if (strcmp(_menuItems[_menuSel], "System") == 0) {   // its own app, off the grid
                    AppManager::get().requestLaunch("SysInfo");
                    return;
                }
                _section = _menuSel + 1;
                _draw();
            }
        }
        else if (_section == 1) {
            if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                if (_tzSel > 0) {
                    _tzSel--;
                    if (_tzSel < _tzScroll) _tzScroll = _tzSel;
                }
                delay(80); _draw();
            }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                if (_tzSel < PaperClock::ZONE_COUNT - 1) {
                    _tzSel++;
                    if (_tzSel >= _tzScroll + TZ_VISIBLE) _tzScroll = _tzSel - TZ_VISIBLE + 1;
                }
                delay(80); _draw();
            }
            if (st.enter) {
                OsUI::waitRelease();
                PaperClock::saveOffset(PaperClock::ZONES[_tzSel].offset);
                OsUI::showToast(_canvas, "Timezone saved!", COL_OK);
                _canvas.pushSprite(0, 0);
                delay(900);
                _draw();
            }
        }
        else if (_section == 2) {
            if (M5Cardputer.Keyboard.isKeyPressed(',') || M5Cardputer.Keyboard.isKeyPressed(';')) {
                _bright = max(10, _bright - 5);
                M5Cardputer.Display.setBrightness(_bright * 255 / 100);
                _saveBrightness(_bright);
                delay(80); _draw();
            }
            if (M5Cardputer.Keyboard.isKeyPressed('/') || M5Cardputer.Keyboard.isKeyPressed('.')) {
                _bright = min(100, _bright + 5);
                M5Cardputer.Display.setBrightness(_bright * 255 / 100);
                _saveBrightness(_bright);
                delay(80); _draw();
            }
        }
        else if (_section == 3) {
            if (st.enter && otaOtherHasDifferentApp()) {
                OsUI::waitRelease();
                _canvas.fillSprite(COL_BG);
                OsUI::drawHeader(_canvas, "Remove emulator?", COL_HEADER_BAD);
                _canvas.setTextColor(COL_TEXT, COL_BG);
                _canvas.drawCenterString("Erase OTA game slot", 120, 44);
                _canvas.setTextColor(COL_DIM, COL_BG);
                _canvas.drawCenterString("[Y] Yes   [N] No", 120, 62);
                _canvas.pushSprite(0, 0);
                while (true) {
                    M5Cardputer.update();
                    if (M5Cardputer.Keyboard.isKeyPressed('y')) {
                        OsUI::waitRelease();
                        bool ok = otaEraseEmulatorSlot();
                        OsUI::showToast(_canvas, ok ? "Emulator removed" : "Erase failed", ok ? COL_OK : COL_ERR);
                        _canvas.pushSprite(0, 0);
                        delay(1200);
                        _draw();
                        break;
                    }
                    if (M5Cardputer.Keyboard.isKeyPressed('n') || M5Cardputer.Keyboard.isKeyPressed('`')) {
                        OsUI::waitRelease();
                        _draw();
                        break;
                    }
                    delay(20);
                }
            }
        }
        else if (_section == 5) {
            auto& wm = WiFiManager::get();
            if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                if (_netSel > 0) _netSel--;
                delay(KEY_REPEAT_MS); _draw();
            }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                if (_netSel < MAX_WIFI_SLOTS - 1) _netSel++;
                delay(KEY_REPEAT_MS); _draw();
            }
            if (st.enter) {
                OsUI::waitRelease();
                String s = _scanAndPickSSID();
                if (s == "") {
                    // scan failed, found nothing, or user cancelled the list -
                    // fall back to the old manual-entry flow so this screen
                    // never becomes a dead end
                    s = OsUI::inputText(_canvas, "SSID:");
                }
                if (s != "") {
                    String p = _lastPickedOpen ? "" : OsUI::inputText(_canvas, "Password:");
                    wm.saveSlot(_netSel, s, p);
                    wm.connectSlot(_netSel);
                    OsUI::showToast(_canvas, "Connecting...", COL_OK);
                    _canvas.pushSprite(0, 0);
                    delay(2000);
                    if (wm.isConnected()) PaperClock::syncTime();
                }
                OsUI::waitRelease();
                _draw();
            }
        }
    }

    // Set by _scanAndPickSSID() when the chosen network is open (no
    // password needed), so the caller can skip the password prompt.
    bool _lastPickedOpen = false;

    // Scans nearby networks and shows a scrollable list with signal bars
    // and security type (like a phone's WiFi picker), ; / . to navigate,
    // Enter to pick, ` to cancel. Returns "" on cancel/no networks/scan
    // failure so the caller can fall back to manual SSID entry.
    String _scanAndPickSSID() {
        _canvas.fillSprite(COL_BG);
        OsUI::drawHeader(_canvas, "Network", COL_HEADER);
        _canvas.setTextColor(COL_DIM, COL_BG);
        _canvas.drawCenterString("Scanning...", SCREEN_W / 2, CONTENT_Y + 30);
        _canvas.pushSprite(0, 0);

        int n = WiFi.scanNetworks();
        if (n <= 0) return "";

        const int ROW_H = 15, VISIBLE = 5;
        const int LIST_Y = CONTENT_Y + 2;
        const int LSTH = VISIBLE * ROW_H;
        int sel = 0, camY = 0;

        while (true) {
            _canvas.fillSprite(COL_BG);
            OsUI::drawHeader(_canvas, "Select Network", COL_HEADER);
            OsUI::drawFooter(_canvas, ";.=nav  ENT=select  `=cancel");

            if (sel * ROW_H < -camY) camY = -sel * ROW_H;
            else if (sel * ROW_H > -camY + (VISIBLE - 1) * ROW_H) camY = -((sel - VISIBLE + 1) * ROW_H);
            camY = constrain(camY, -(max(0, n - VISIBLE) * ROW_H), 0);

            _canvas.setClipRect(0, LIST_Y, SCREEN_W, LSTH);
            for (int i = 0; i < n; i++) {
                int y = LIST_Y + i * ROW_H + camY;
                if (y + ROW_H < LIST_Y || y > LIST_Y + LSTH) continue;
                bool rowSel = (i == sel);
                uint16_t bg = rowSel ? COL_SELECTED : COL_BG;
                uint16_t fg = rowSel ? WHITE : COL_TEXT;
                _canvas.fillRect(0, y, SCREEN_W, ROW_H - 1, bg);

                // signal bars from RSSI, same thresholds as the status bar icon
                int rssi = WiFi.RSSI(i);
                int bars = rssi > -55 ? 4 : rssi > -70 ? 3 : rssi > -85 ? 2 : 1;
                for (int b = 0; b < 4; b++) {
                    int bh = 2 + b * 2;
                    uint16_t bc = (b < bars) ? fg : (uint16_t)COL_BG2;
                    _canvas.fillRect(6 + b * 5, y + (12 - bh), 3, bh, bc);
                }

                _canvas.setTextColor(fg, bg);
                char nbuf[26]; strncpy(nbuf, WiFi.SSID(i).c_str(), 25); nbuf[25] = '\0';
                _canvas.setCursor(30, y + 3);
                _canvas.print(nbuf);

                // security type, right-aligned
                wifi_auth_mode_t enc = WiFi.encryptionType(i);
                const char* sec = enc == WIFI_AUTH_OPEN ? "OPEN"
                                 : enc == WIFI_AUTH_WEP ? "WEP"
                                 : enc == WIFI_AUTH_WPA_PSK ? "WPA"
                                 : enc == WIFI_AUTH_WPA2_PSK ? "WPA2"
                                 : enc == WIFI_AUTH_WPA_WPA2_PSK ? "WPA/2"
                                 : enc == WIFI_AUTH_WPA3_PSK ? "WPA3"
                                 : enc == WIFI_AUTH_WPA2_WPA3_PSK ? "WPA2/3"
                                 : "SEC";
                _canvas.setTextColor(rowSel ? WHITE : COL_DIM, bg);
                _canvas.setTextDatum(top_right);
                _canvas.drawString(sec, SCREEN_W - 4, y + 3);
                _canvas.setTextDatum(top_left);
            }
            _canvas.clearClipRect();

            if (n > VISIBLE) {
                int barH = max(4, VISIBLE * VISIBLE * ROW_H / n);
                int barY = LIST_Y + ((-camY) * (LSTH - barH)) / max(1, (n - VISIBLE) * ROW_H);
                _canvas.fillRect(SCREEN_W - 3, LIST_Y, 3, LSTH, COL_BG2);
                _canvas.fillRect(SCREEN_W - 3, barY, 3, barH, COL_ACCENT);
            }
            _canvas.pushSprite(0, 0);

            while (true) {
                M5Cardputer.update();
                auto st = M5Cardputer.Keyboard.keysState();
                if (M5Cardputer.Keyboard.isKeyPressed(';')) { if (sel > 0) sel--; delay(KEY_REPEAT_MS); break; }
                if (M5Cardputer.Keyboard.isKeyPressed('.')) { if (sel < n - 1) sel++; delay(KEY_REPEAT_MS); break; }
                if (st.enter) {
                    OsUI::waitRelease();
                    _lastPickedOpen = (WiFi.encryptionType(sel) == WIFI_AUTH_OPEN);
                    String picked = WiFi.SSID(sel);
                    WiFi.scanDelete();
                    return picked;
                }
                if (M5Cardputer.Keyboard.isKeyPressed('`')) {
                    OsUI::waitRelease();
                    WiFi.scanDelete();
                    return "";
                }
                delay(10);
            }
        }
    }

private:
    LGFX_Sprite& _canvas;
    int  _section  = 0;
    int  _menuSel  = 0;
    int  _menuScroll = 0;
    int  _tzSel    = 0;
    int  _tzScroll = 0;
    int  _bright   = 80;
    int  _netSel   = 0;

    static const int TZ_VISIBLE   = 6;
    static const int MENU_VISIBLE = 4; // how many main-menu rows fit above the footer

    static const char* _menuItems[];
    static const int   _menuCount;

    int _findCurrentTZ() {
        int off = PaperClock::loadOffset();
        for (int i = 0; i < PaperClock::ZONE_COUNT; i++) {
            if (PaperClock::ZONES[i].offset == off) return i;
        }
        return 12; // UTC+0
    }

    int _loadBrightness() {
        Preferences p;
        p.begin("display", true);
        int b = p.getInt("bright", 80);
        p.end();
        return constrain(b, 10, 100);
    }
    void _saveBrightness(int b) {
        Preferences p;
        p.begin("display", false);
        p.putInt("bright", b);
        p.end();
    }

    void _draw() {
        _canvas.fillSprite(COL_BG);

        if (_section == 0) {
            OsUI::drawHeader(_canvas, "Settings  v" OS_VERSION, COL_HEADER);
            _canvas.setTextSize(1);

            for (int i = 0; i < MENU_VISIBLE; i++) {
                int idx = _menuScroll + i;
                if (idx >= _menuCount) break;
                bool sel = (idx == _menuSel);
                int  ty  = CONTENT_Y + 4 + i * 24;
                uint16_t bg = sel ? COL_SELECTED : COL_BG2;
                _canvas.fillRect(8, ty, SCREEN_W - 16, 20, bg);
                _canvas.drawRect(8, ty, SCREEN_W - 16, 20,
                    sel ? (uint16_t)COL_ACCENT : (uint16_t)0x2945);
                _canvas.setTextColor(sel ? (uint16_t)WHITE : (uint16_t)COL_TEXT, bg);
                _canvas.setCursor(18, ty + 7);
                _canvas.print(_menuItems[idx]);
                if (sel) {
                    _canvas.setTextColor(COL_ACCENT, bg);
                    _canvas.setCursor(SCREEN_W - 22, ty + 7);
                    _canvas.print(">");
                }
            }
            if (_menuCount > MENU_VISIBLE) {
                int listY = CONTENT_Y + 4, listH = MENU_VISIBLE * 24;
                int barH = max(6, listH * MENU_VISIBLE / _menuCount);
                int barY = listY + (_menuScroll * listH) / _menuCount;
                OsUI::drawPanel(_canvas, SCREEN_W - 6, listY, 4, listH, false, COL_BG2);
                _canvas.fillRect(SCREEN_W - 5, barY, 2, barH, COL_BEVEL_LO);
            }
            OsUI::drawFooter(_canvas, ";.=nav  ENT=open  `=home");
        }
        else if (_section == 1) {
            OsUI::drawHeader(_canvas, "Timezone Select", COL_HEADER);
            _canvas.setTextSize(1);

            // Current offset
            char curBuf[20];
            snprintf(curBuf, sizeof(curBuf), "Now: %s",
                PaperClock::ZONES[_tzSel].offset >= 0
                    ? (String("UTC+") + PaperClock::ZONES[_tzSel].offset).c_str()
                    : (String("UTC")  + PaperClock::ZONES[_tzSel].offset).c_str()
            );
            _canvas.setTextColor(COL_ACCENT, COL_BG);
            _canvas.setCursor(6, CONTENT_Y + 2);
            _canvas.print(curBuf);

            const int ROW_H = 14;
            const int LIST_Y = CONTENT_Y + 14;

            for (int i = 0; i < TZ_VISIBLE; i++) {
                int idx = _tzScroll + i;
                if (idx >= PaperClock::ZONE_COUNT) break;
                bool sel = (idx == _tzSel);
                int  ty  = LIST_Y + i * ROW_H;
                uint16_t bg = sel ? COL_SELECTED : COL_BG;
                _canvas.fillRect(0, ty, SCREEN_W, ROW_H, bg);
                _canvas.setTextColor(sel ? (uint16_t)WHITE : (uint16_t)COL_TEXT, bg);
                _canvas.setCursor(4, ty + 2);
                // Truncate to 36 chars
                char nbuf[37];
                strncpy(nbuf, PaperClock::ZONES[idx].name, 36);
                nbuf[36] = '\0';
                _canvas.print(nbuf);
            }

            // Scrollbar
            if (PaperClock::ZONE_COUNT > TZ_VISIBLE) {
                int barH = max(4, TZ_VISIBLE * TZ_VISIBLE * ROW_H / PaperClock::ZONE_COUNT);
                int barY = LIST_Y + (_tzScroll * TZ_VISIBLE * ROW_H) / PaperClock::ZONE_COUNT;
                _canvas.fillRect(SCREEN_W - 3, LIST_Y, 3, TZ_VISIBLE * ROW_H, COL_BG2);
                _canvas.fillRect(SCREEN_W - 3, barY, 3, barH, COL_ACCENT);
            }

            OsUI::drawFooter(_canvas, ";.=scroll  ENT=save  `=back");
        }
        else if (_section == 2) {
            OsUI::drawHeader(_canvas, "Brightness", COL_HEADER);
            _canvas.setTextSize(1);
            _canvas.setTextColor(COL_TEXT, COL_BG);
            _canvas.drawCenterString("Brightness", SCREEN_W / 2, CONTENT_Y + 14);

            char pctBuf[8];
            snprintf(pctBuf, sizeof(pctBuf), "%d%%", _bright);
            _canvas.setTextSize(2);
            _canvas.setTextColor(COL_ACCENT, COL_BG);
            _canvas.drawCenterString(pctBuf, SCREEN_W / 2, CONTENT_Y + 28);
            _canvas.setTextSize(1);

            // Slider handle
            int bw = map(_bright, 10, 100, 0, SCREEN_W - 40);
            _canvas.drawRect(20, CONTENT_Y + 52, SCREEN_W - 40, 12, COL_DIM);
            _canvas.fillRect(21, CONTENT_Y + 53, bw, 10, COL_ACCENT);

            OsUI::drawFooter(_canvas, ",/;=dim  //.=bright  `=back");
        }
        else if (_section == 3) {
            OsUI::drawHeader(_canvas, "Games / Emulator", COL_HEADER);
            _canvas.setTextSize(1);
            int y = CONTENT_Y + 8;
            _canvas.setTextColor(COL_TEXT, COL_BG);
            _canvas.setCursor(8, y);
            _canvas.print("OTA game slot:");
            y += 14;
            String slotApp = otaSlotApp();
            bool has = otaOtherHasDifferentApp();
            _canvas.setTextColor(has ? COL_OK : COL_DIM, COL_BG);
            _canvas.setCursor(8, y);
            if (slotApp == "emu") _canvas.print("PaperEMU installed");
            else if (slotApp == "ard") _canvas.print("Arduboy game installed");
            else if (has) _canvas.print("Installed (unknown)");
            else _canvas.print("Not installed");
            y += 18;
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.setCursor(8, y);
            _canvas.print("Put PaperEMU.extension");
            y += 12;
            _canvas.setCursor(8, y);
            _canvas.print("in /MitamaOS/games/ on SD");
            y += 18;
            if (has) {
                _canvas.setTextColor(COL_WARN, COL_BG);
                _canvas.setCursor(8, y);
                _canvas.print("ENT = erase emulator");
                _canvas.setTextColor(COL_DIM, COL_BG);
                _canvas.setCursor(8, y + 12);
                _canvas.print("(no full reflash needed)");
            }
            OsUI::drawFooter(_canvas, "`=back  ENT=erase");
        }
        else if (_section == 4) {
            OsUI::drawHeader(_canvas, "About " OS_NAME, COL_HEADER);
            _canvas.setTextSize(1);
            int y = CONTENT_Y + 8;
            auto row = [&](const char* label, const char* val) {
                _canvas.setTextColor(COL_DIM, COL_BG);
                _canvas.setCursor(8, y);
                _canvas.print(label);
                _canvas.setTextColor(COL_TEXT, COL_BG);
                _canvas.setCursor(90, y);
                _canvas.print(val);
                y += 14;
            };
            row("OS:",      OS_NAME " v" OS_VERSION);
            row("Author:",  OS_AUTHOR);
            row("Board:",   "M5Cardputer");
            row("CPU:",     "ESP32-S3");
            row("Flash:",   "8MB QIO");
            row("Display:", "240x135 TFT");
            row("Emu:",     "PaperEMU NES+GB");
            OsUI::drawFooter(_canvas, "`=back");
        }
        else if (_section == 5) {
            OsUI::drawHeader(_canvas, "Network", COL_HEADER);
            _canvas.setTextSize(1);
            auto& wm = WiFiManager::get();

            for (int i = 0; i < MAX_WIFI_SLOTS; i++) {
                int ry = CONTENT_Y + 2 + i * 22;
                bool sel = (_netSel == i);
                OsUI::drawPanel(_canvas, 4, ry, SCREEN_W - 8, 18, !sel,
                    sel ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG);
                _canvas.setTextColor(sel ? (uint16_t)0xFFFF : (uint16_t)COL_TEXT,
                    sel ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG);
                _canvas.setCursor(10, ry + 5);
                String ssid = wm.slots[i].ssid;
                bool active = wm.isConnected() && (WiFi.SSID() == ssid) && ssid != "";
                _canvas.printf("Slot%d: %s%s", i + 1,
                    ssid == "" ? "(empty)" : ssid.c_str(), active ? " *" : "");
            }

            int sy = CONTENT_Y + 2 + MAX_WIFI_SLOTS * 22 + 2;
            if (wm.isConnected()) {
                _canvas.setTextColor(COL_OK, COL_BG);
                _canvas.setCursor(8, sy);
                _canvas.print("Connected: " + WiFi.localIP().toString());
            } else {
                _canvas.setTextColor(COL_ERR, COL_BG);
                _canvas.setCursor(8, sy);
                _canvas.print("Not connected");
            }
            OsUI::drawFooter(_canvas, ";.=nav  ENT=edit  `=back");
        }

        _canvas.pushSprite(0, 0);
    }
};

const char* SettingsApp::_menuItems[] = {
    "Timezone",
    "Brightness",
    "Games / Emulator",
    "About",
    "Network",
    "System",
};
const int SettingsApp::_menuCount = 6;
