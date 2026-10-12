#pragma once
#include <M5Cardputer.h>
#include <SD.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/wifi_manager.h"
#include "../../config.h"

// Store - native app-store for /sd/lua scripts, fetched over HTTPS from a
// GitHub repo (raw.githubusercontent.com).
//
// WHY NATIVE C++ INSTEAD OF A LUA SCRIPT:
// The Lua REPL environment itself (luaL_openlibs + ~150 registered C
// functions + the luaL_dostring shell-helper block) already burns roughly
// half of this board's tiny internal SRAM before a single user script runs
// (this board is an ESP32-S3FN8 - NO PSRAM, see platformio.ini). A TLS
// handshake (mbedTLS, used by WiFiClientSecure for https://) needs ~40-50KB
// free in one contiguous-ish chunk. Doing HTTPS from *inside* a running Lua
// app routinely fails ("connection refused") simply because there isn't
// enough headroom left after the Lua environment's own footprint. Running
// as a plain native App (like Browser/Games) avoids that overhead entirely
// and has the same TLS budget the Browser app already uses successfully.
//
// index.json format (flat array, ASCII-only - no escaped quotes handled,
// keep names/descriptions simple):
//   [{"name":"snake","version":"1.0","desc":"Classic snake","file":"snake.lua"}, ...]
//
// Keys: ;/. = navigate   ENT = install   D = remove   R = refresh index   ` = home

#define STORE_BASE_URL   "https://raw.githubusercontent.com/Artem76228/PaperOS-Apps/main"
#define STORE_INDEX_URL  STORE_BASE_URL "/index.json"
#define STORE_APPS_URL   STORE_BASE_URL "/apps/"
#define STORE_INSTALL_DIR "/lua"          // relative to SD root -> /sd/lua on the card
#define STORE_CACHE_PATH  "/store_index.json"  // cached on LittleFS, survives no-wifi boots
#define STORE_MAX_APPS    24

class Store : public App {
public:
    Store(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Store"; }
    const char* getIcon() const override { return "S"; }
    const char* getDesc() const override { return "Install Lua apps"; }

    void onStart() override {
        _sel = 0; _camY = 0; _count = 0; _status = "";
        _loadIndex(false);
        _draw();
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();
        WiFiManager::get().ensureConnected();

        bool changed = false;
        if (M5Cardputer.Keyboard.isKeyPressed(';')) {
            if (_sel > 0) { _sel--; changed = true; } delay(KEY_REPEAT_MS);
        }
        if (M5Cardputer.Keyboard.isKeyPressed('.')) {
            if (_sel < _count - 1) { _sel++; changed = true; } delay(KEY_REPEAT_MS);
        }
        auto ks = M5Cardputer.Keyboard.keysState();
        if (ks.enter && _count > 0) {
            OsUI::waitRelease();
            _installSelected();
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('d')) {
            if (_count > 0 && _isInstalled(_apps[_sel])) { OsUI::waitRelease(); _removeSelected(); }
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('r')) {
            OsUI::waitRelease();
            _loadIndex(true);
            _draw();
            return;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            OsUI::waitRelease();
            AppManager::get().launchByIndex(0);
            return;
        }
        if (changed) _draw();
    }

private:
    LGFX_Sprite& _canvas;

    struct AppEntry {
        String name, version, desc, file;
    };
    AppEntry _apps[STORE_MAX_APPS];
    int _count = 0, _sel = 0, _camY = 0;
    String _status;

    // ---------- tiny hand-rolled JSON reader (flat array of flat objects,
    // ASCII string values only - matches exactly what store.lua / this repo
    // convention produces; not a general-purpose JSON parser) ----------
    static String _field(const String& obj, const char* key) {
        String pat = String("\"") + key + "\"";
        int k = obj.indexOf(pat);
        if (k < 0) return "";
        int colon = obj.indexOf(':', k + pat.length());
        if (colon < 0) return "";
        int q1 = obj.indexOf('"', colon + 1);
        if (q1 < 0) return "";
        int q2 = obj.indexOf('"', q1 + 1);
        if (q2 < 0) return "";
        return obj.substring(q1 + 1, q2);
    }

    int _parseIndex(const String& json) {
        int n = 0, i = 0;
        while (i < (int)json.length() && n < STORE_MAX_APPS) {
            int objStart = json.indexOf('{', i);
            if (objStart < 0) break;
            int objEnd = json.indexOf('}', objStart);
            if (objEnd < 0) break;
            String obj = json.substring(objStart, objEnd + 1);
            AppEntry e;
            e.name    = _field(obj, "name");
            e.version = _field(obj, "version");
            e.desc    = _field(obj, "desc");
            e.file    = _field(obj, "file");
            if (e.name.length() && e.file.length()) _apps[n++] = e;
            i = objEnd + 1;
        }
        return n;
    }

    // ---------- HTTPS helpers (same http-vs-https-by-scheme pattern the
    // Browser app uses, see paper_engine.h) ----------
    static bool _begin(HTTPClient& http, WiFiClientSecure& sec, WiFiClient& plain, const String& url) {
        bool isHttps = url.startsWith("https://");
        if (isHttps) {
            // Same ~45KB mbedTLS handshake floor as Lua's net.get -- Store
            // usually has more headroom since the Lua VM isn't loaded, but
            // it's not guaranteed, so fail clean instead of a bare timeout.
            if (ESP.getFreeHeap() < 45000) return false;
            sec.setInsecure();
        }
        WiFiClient& c = isHttps ? static_cast<WiFiClient&>(sec) : plain;
        c.setTimeout(8000); // ms -- was "8" (8ms), killed https TLS handshake every time
        return http.begin(c, url);
    }

    bool _fetchIndexBody(String& outBody) {
        WiFiClientSecure sec; WiFiClient plain;
        HTTPClient http; http.setTimeout(8000);
        if (!_begin(http, sec, plain, STORE_INDEX_URL)) return false;
        int code = http.GET();
        if (code != 200) { http.end(); return false; }
        outBody = http.getString();
        http.end();
        return true;
    }

    // Streams the file straight to SD in 512B chunks - no full-body String,
    // matters a lot on a board with no PSRAM and ~30KB free heap.
    bool _downloadToSD(const String& url, const String& destPath, size_t& outBytes, const String& label) {
        WiFiClientSecure sec; WiFiClient plain;
        HTTPClient http; http.setTimeout(8000);
        if (!_begin(http, sec, plain, url)) return false;
        int code = http.GET();
        if (code != 200) { http.end(); return false; }

        File f = SD.open(destPath.c_str(), FILE_WRITE);
        if (!f) { http.end(); return false; }

        WiFiClient* stream = http.getStreamPtr();
        int total = http.getSize();
        size_t written = 0;
        uint8_t buf[512];
        uint32_t lastData = millis();
        uint32_t lastDraw = 0;
        _showProgress(label, 0, total);

        while (http.connected() && (total < 0 || (int)written < total)) {
            size_t avail = stream->available();
            if (avail) {
                size_t n = stream->readBytes(buf, min(avail, sizeof(buf)));
                f.write(buf, n);
                written += n;
                lastData = millis();
                uint32_t now = millis();
                if (now - lastDraw > 120) { // throttle redraws, don't slow the transfer down
                    _showProgress(label, written, total);
                    lastDraw = now;
                }
            } else {
                if (millis() - lastData > 8000) break;  // stalled
                delay(2);
            }
        }
        f.close();
        http.end();
        outBytes = written;
        if (written > 0) _showProgress(label, written, total > 0 ? total : (int)written);
        return written > 0;
    }

    // Draws a proper fill-bar with a percentage (or a raw byte count when
    // the server didn't send a Content-Length, e.g. chunked responses).
    void _showProgress(const String& label, size_t got, int total) {
        const int bx = 14, by = 58, bw = SCREEN_W - 28, bh = 14;
        _canvas.fillRect(6, 44, SCREEN_W - 12, 40, COL_BG);
        _canvas.setTextColor(COL_ACCENT, COL_BG);
        _canvas.drawCenterString(label.c_str(), SCREEN_W / 2, 44);
        OsUI::drawPanel(_canvas, bx, by, bw, bh, false, COL_BG2);
        char pct[16];
        if (total > 0) {
            int fillW = (int)((long)(bw - 2) * (long)got / total);
            fillW = constrain(fillW, 0, bw - 2);
            _canvas.fillRect(bx + 1, by + 1, fillW, bh - 2, COL_ACCENT);
            snprintf(pct, sizeof(pct), "%d%%", (int)((got * 100) / total));
        } else {
            snprintf(pct, sizeof(pct), "%u B", (unsigned)got);
        }
        _canvas.setTextColor(COL_TEXT, COL_BG);
        _canvas.drawCenterString(pct, SCREEN_W / 2, by + bh + 4);
        _canvas.pushSprite(0, 0);
    }

    void _loadIndex(bool forceNetwork) {
        String body;
        bool gotFromNet = false;

        if (forceNetwork || WiFi.status() == WL_CONNECTED) {
            WiFiManager::get().connectFirst(4000);
            if (WiFi.status() == WL_CONNECTED) {
                _status = "Fetching index...";
                _draw();
                if (_fetchIndexBody(body)) {
                    gotFromNet = true;
                    File c = LittleFS.open(STORE_CACHE_PATH, "w");
                    if (c) { c.print(body); c.close(); }
                }
            }
        }
        if (!gotFromNet) {
            File c = LittleFS.open(STORE_CACHE_PATH, "r");
            if (c) { body = c.readString(); c.close(); }
        }

        _count = body.length() ? _parseIndex(body) : 0;
        if (_sel >= _count) _sel = max(0, _count - 1);
        _status = gotFromNet ? "" : (body.length() ? "offline (cached)" : "no index - press R");
    }

    bool _isInstalled(const AppEntry& a) {
        return SD.exists((String(STORE_INSTALL_DIR) + "/" + a.file).c_str());
    }

    void _installSelected() {
        AppEntry& a = _apps[_sel];
        if (!SD.exists(STORE_INSTALL_DIR)) SD.mkdir(STORE_INSTALL_DIR);
        WiFiManager::get().connectFirst(4000);
        if (WiFi.status() != WL_CONNECTED) { _showMsg("No WiFi", COL_ERR, 1200); _draw(); return; }

        _showMsg(("Installing " + a.name + "...").c_str(), COL_ACCENT, 0);
        String url = String(STORE_APPS_URL) + a.file;
        String dest = String(STORE_INSTALL_DIR) + "/" + a.file;
        size_t bytes = 0;
        if (_downloadToSD(url, dest, bytes, a.name)) {
            char buf[32]; snprintf(buf, sizeof(buf), "OK  %u B", (unsigned)bytes);
            _showMsg(buf, COL_OK, 900);
        } else {
            _showMsg("Download failed", COL_ERR, 1200);
        }
        _draw();
    }

    void _removeSelected() {
        AppEntry& a = _apps[_sel];
        String path = String(STORE_INSTALL_DIR) + "/" + a.file;
        if (SD.remove(path.c_str())) _showMsg("Removed", COL_OK, 600);
        else _showMsg("Remove failed", COL_ERR, 900);
        _draw();
    }

    void _showMsg(const char* msg, uint16_t col, int ms) {
        _canvas.fillRect(10, 52, SCREEN_W - 20, 20, COL_BG2);
        _canvas.drawRect(10, 52, SCREEN_W - 20, 20, col);
        _canvas.setTextColor(col, COL_BG2);
        _canvas.drawCenterString(msg, 120, 57);
        _canvas.pushSprite(0, 0);
        if (ms > 0) delay(ms);
    }

    void _draw() {
        _canvas.fillSprite(COL_BG);
        OsUI::drawHeader(_canvas, "Store");
        OsUI::drawFooter(_canvas, ";/.=nav ENT=install D=del R=sync `=home");

        if (_count == 0) {
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.drawCenterString(_status.length() ? _status.c_str() : "No packages", 120, 60);
            _canvas.pushSprite(0, 0);
            return;
        }

        const int ROW_H = 15, VISIBLE = 5;
        const int LIST_Y = CONTENT_Y + 2;
        const int LSTH = VISIBLE * ROW_H;

        if (_sel * ROW_H < -_camY) _camY = -_sel * ROW_H;
        else if (_sel * ROW_H > -_camY + (VISIBLE - 1) * ROW_H) _camY = -((_sel - VISIBLE + 1) * ROW_H);
        _camY = constrain(_camY, -(max(0, _count - VISIBLE) * ROW_H), 0);

        // Hard-clip to the list area so smooth per-pixel scrolling (_camY)
        // can never bleed a partially-scrolled row up into the header or
        // down into the footer -- the old bounds check let a whole extra
        // row draw one ROW_H above LIST_Y, which visibly overlapped the
        // header bar mid-scroll.
        _canvas.setClipRect(0, LIST_Y, SCREEN_W, LSTH);
        for (int i = 0; i < _count; i++) {
            int y = LIST_Y + i * ROW_H + _camY;
            if (y + ROW_H < LIST_Y || y > LIST_Y + LSTH) continue;

            bool sel = (i == _sel);
            uint16_t bg = sel ? COL_SELECTED : COL_BG;
            uint16_t fg = sel ? WHITE : COL_TEXT;
            _canvas.fillRect(4, y, SCREEN_W - 8, ROW_H - 1, bg);

            bool inst = _isInstalled(_apps[i]);
            _canvas.setTextColor(inst ? (sel ? WHITE : (uint16_t)COL_OK) : (sel ? WHITE : COL_DIM), bg);
            _canvas.setCursor(8, y + 3);
            _canvas.print(inst ? "[i]" : "[ ]");

            _canvas.setTextColor(fg, bg);
            char buf[26]; snprintf(buf, sizeof(buf), "%s", _apps[i].name.c_str());
            _canvas.setCursor(32, y + 3);
            _canvas.print(buf);
        }
        _canvas.clearClipRect();

        // Scrollbar, same visual pattern as the Settings timezone list
        if (_count > VISIBLE) {
            int barH = max(4, VISIBLE * VISIBLE * ROW_H / _count);
            int barY = LIST_Y + ((-_camY) * (LSTH - barH)) / max(1, (_count - VISIBLE) * ROW_H);
            _canvas.fillRect(SCREEN_W - 3, LIST_Y, 3, LSTH, COL_BG2);
            _canvas.fillRect(SCREEN_W - 3, barY, 3, barH, COL_ACCENT);
        }

        char cnt[16]; snprintf(cnt, sizeof(cnt), "%d/%d", _sel + 1, _count);
        _canvas.setTextColor(COL_DIM, COL_BG);
        _canvas.setTextDatum(top_right);
        _canvas.drawString(cnt, SCREEN_W - 4, CONTENT_Y + 2);
        _canvas.setTextDatum(top_left);

        // Description of the selected package, one line under the list
        _canvas.setTextColor(COL_DIM, COL_BG);
        char db[34]; snprintf(db, sizeof(db), "%s", _apps[_sel].desc.c_str());
        _canvas.setCursor(6, SCREEN_H - FOOTER_H - 10);
        _canvas.print(db);

        _canvas.pushSprite(0, 0);
    }
};
