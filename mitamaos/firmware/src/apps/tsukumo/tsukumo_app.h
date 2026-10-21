#pragma once
// Tsukumo: lists the M5Stack Units plugged into the Grove port or a Cap,
// and shows live readings for the ones MitamaOS has a driver for.
// (Tsukumogami: tools that gain a spirit after long use.) Opened from
// Settings; off the grid.
#include <M5Cardputer.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/disp_lock.h"
#include "../../config.h"
#include "../../ui/mitama_ui.h"
#include "../../system/tsukumo.h"

class TsukumoApp : public App {
public:
    TsukumoApp(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Tsukumo"; }
    const char* getIcon() const override { return "T"; }
    const char* getDesc() const override { return "Attached modules"; }

    void onStart() override {
        OsUI::waitRelease();
        _sel = _top = 0; _open = -1; _note = "";
        _scanNow();
    }
    void onStop() override { _stopDriver(); }

    void update() override {
        M5Cardputer.update();
        auto& kb = M5Cardputer.Keyboard;
        if (kb.isChange() && kb.isPressed()) {
            auto st = kb.keysState();
            if (!_keys(st)) return;
            _draw();
        }
        if (_open >= 0 && millis() - _readMs > 1000) { _readMs = millis(); _read(); _draw(); }
    }

private:
    LGFX_Sprite& _canvas;
    int      _sel = 0, _top = 0, _open = -1;
    String   _note;
    uint32_t _readMs = 0;
    char     _live[4][36] = {};
    // driver state
    uint8_t  _cal[24] = {};
    uint8_t  _lastKey = 0;
    char     _uid[24] = {};
    uint16_t _co2 = 0; float _co2T = 0, _co2H = 0;

    using F = Tsukumo::Found;
    const Tsukumo::Result& _res() const { return Tsukumo::Scanner::get().result(); }

    void _scanNow() {
        _stopDriver(); _open = -1;
        _drawBusy("Looking, gently...");
        Tsukumo::Scanner::get().scan();
        _sel = _top = 0;
        _draw();
    }

    bool _keys(const Keyboard_Class::KeysState& st) {
        auto& sc = Tsukumo::Scanner::get();
        const auto& r = _res();
        for (char c : st.word) {
            if (c == '`') {
                if (_open >= 0) { _stopDriver(); _open = -1; return true; }
                OsUI::waitRelease(); AppManager::get().goHome(); return false;
            }
            if (_open >= 0) continue;
            if (c == ';' && _sel > 0) _sel--;
            if (c == '.' && _sel < r.n - 1) _sel++;
            if (c == 'r' || c == 'R') { _note = ""; _scanNow(); }
            if (c == 'u' || c == 'U') {
                if (r.n || r.gpsGrove) { _note = "Something answered on I2C, so it isn't serial."; continue; }
                _drawBusy("Listening on the Grove pins...");
                sc.listenUart();
                _note = r.uartPin < 0 ? "Nothing talked. Some serial Units only answer." : "";
            }
            if (c == 'g' || c == 'G') {
                if (r.gpsGrove) { sc.setGpsOnGrove(false, -1); _note = "GPS back on the cap. Restart to apply."; }
                else if (r.uartNmea) { sc.setGpsOnGrove(true, r.uartPin); _note = "Grove GPS saved. Restart to use it."; }
            }
        }
        if (st.enter && _open < 0 && r.n) { _open = _sel; _startDriver(); _readMs = 0; }
        return true;
    }

    // ── drivers ────────────────────────────────────────────────────────
    // Each reading runs on the Unit's own bus; behind a PaHub the channel
    // is opened for the read and closed after.
    template <typename Fn> bool _on(const F& f, Fn fn) {
        bool grove = f.bus != Tsukumo::B_CAP;
        if (grove && !m5::Ex_I2C.begin()) return false;
        Tsukumo::Io io{ grove ? m5::Ex_I2C : m5::In_I2C };
        uint8_t m = 1 << f.hubCh, z = 0;
        if (f.bus == Tsukumo::B_HUB) io.cmd(f.hub, &m, 1);
        bool ok = fn(io, f.addr);
        if (f.bus == Tsukumo::B_HUB) io.cmd(f.hub, &z, 1);
        if (grove) { m5::Ex_I2C.release(); pinMode(Tsukumo::G_SDA, INPUT); pinMode(Tsukumo::G_SCL, INPUT); }
        return ok;
    }

    void _startDriver() {
        memset(_live, 0, sizeof(_live));
        _uid[0] = 0; _lastKey = 0; _co2 = 0;
        const F& f = _res().dev[_open];
        switch (f.drv) {
        case Tsukumo::D_BMP280:
            _on(f, [&](const Tsukumo::Io& io, uint8_t a) {
                if (!io.reg(a, 0x88, _cal, 24)) return false;
                io.i2c.writeRegister8(a, 0xF5, 0xA0, Tsukumo::HZ);   // 1 s standby
                return io.i2c.writeRegister8(a, 0xF4, 0x27, Tsukumo::HZ);   // normal mode, x1/x1
            });
            break;
        case Tsukumo::D_SCD4X: _on(f, [](const Tsukumo::Io& io, uint8_t a) { return io.cmd16(a, 0x21B1); }); break;
        case Tsukumo::D_BH1750: _on(f, [](const Tsukumo::Io& io, uint8_t a) { uint8_t c = 0x01; io.cmd(a, &c, 1); c = 0x10; return io.cmd(a, &c, 1); }); break;
        case Tsukumo::D_RFID: _on(f, [](const Tsukumo::Io& io, uint8_t a) { return _rfidInit(io, a); }); break;
        default: break;
        }
    }

    void _stopDriver() {
        if (_open < 0 || _open >= _res().n) return;
        const F& f = _res().dev[_open];
        switch (f.drv) {
        case Tsukumo::D_BMP280: _on(f, [](const Tsukumo::Io& io, uint8_t a) { return io.i2c.writeRegister8(a, 0xF4, 0x24, Tsukumo::HZ); }); break;   // sleep
        case Tsukumo::D_SCD4X: _on(f, [](const Tsukumo::Io& io, uint8_t a) { return io.cmd16(a, 0x3F86); }); break;
        case Tsukumo::D_BH1750: _on(f, [](const Tsukumo::Io& io, uint8_t a) { uint8_t c = 0x00; return io.cmd(a, &c, 1); }); break;
        case Tsukumo::D_RFID: _on(f, [](const Tsukumo::Io& io, uint8_t a) {   // field off
            int v = io.reg8(a, 0x14); return v >= 0 && io.i2c.writeRegister8(a, 0x14, v & ~0x03, Tsukumo::HZ); }); break;
        default: break;
        }
    }

    void _read() {
        const F& f = _res().dev[_open];
        auto L = [&](int i, const char* fmt, auto... v) { snprintf(_live[i], sizeof(_live[i]), fmt, v...); };
        bool ok = true;
        switch (f.drv) {
        case Tsukumo::D_SHT3X: case Tsukumo::D_SHT4X: {
            bool four = f.drv == Tsukumo::D_SHT4X;
            uint8_t b[6];
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) {
                uint8_t c = 0xFD;
                if (!(four ? io.cmd(a, &c, 1) : io.cmd16(a, 0x2400))) return false;
                delay(four ? 10 : 16);
                return io.raw(a, b, 6) && Tsukumo::sensirion(b, 2);
            });
            if (ok) {
                float t = -45 + 175.0f * (b[0] << 8 | b[1]) / 65535;
                float h = four ? -6 + 125.0f * (b[3] << 8 | b[4]) / 65535 : 100.0f * (b[3] << 8 | b[4]) / 65535;
                L(0, "Temperature  %.1f C", t); L(1, "Humidity     %.0f %%", constrain(h, 0.0f, 100.0f));
            }
            break;
        }
        case Tsukumo::D_BMP280: {
            uint8_t b[6];
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) { return io.reg(a, 0xF7, b, 6); });
            if (ok) { float t, p; _bmp(b, t, p); L(0, "Pressure     %.1f hPa", p); L(1, "Temperature  %.1f C", t); }
            break;
        }
        case Tsukumo::D_SCD4X: {
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) {
                uint8_t b[9];
                if (!io.cmd16(a, 0xE4B8)) return false;
                delay(2);
                if (!io.raw(a, b, 3) || !Tsukumo::sensirion(b, 1)) return false;
                if (((b[0] << 8 | b[1]) & 0x07FF) == 0) return true;   // no new sample yet
                if (!io.cmd16(a, 0xEC05)) return false;
                delay(2);
                if (!io.raw(a, b, 9) || !Tsukumo::sensirion(b, 3)) return false;
                _co2 = b[0] << 8 | b[1];
                _co2T = -45 + 175.0f * (b[3] << 8 | b[4]) / 65535;
                _co2H = 100.0f * (b[6] << 8 | b[7]) / 65535;
                return true;
            });
            if (!_co2) L(0, "Warming up (first reading ~5 s)");
            else { L(0, "CO2          %u ppm", _co2); L(1, "Temperature  %.1f C", _co2T); L(2, "Humidity     %.0f %%", _co2H); }
            break;
        }
        case Tsukumo::D_BH1750: {
            uint8_t b[2];
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) { return io.raw(a, b, 2); });
            if (ok) L(0, "Light        %.0f lux", (b[0] << 8 | b[1]) / 1.2f);
            break;
        }
        case Tsukumo::D_MLX90614: {
            uint8_t o[3], am[3];
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) { return io.reg(a, 0x07, o, 3) && io.reg(a, 0x06, am, 3); });
            if (ok) { L(0, "Object       %.1f C", (o[0] | o[1] << 8) * 0.02f - 273.15f); L(1, "Unit itself  %.1f C", (am[0] | am[1] << 8) * 0.02f - 273.15f); }
            break;
        }
        case Tsukumo::D_RTC: {
            uint8_t b[7];
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) { return io.reg(a, 0x02, b, 7); });
            auto bcd = [](uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); };
            if (ok) {
                L(0, "20%02d-%02d-%02d  %02d:%02d:%02d", bcd(b[6]), bcd(b[5] & 0x1F), bcd(b[3] & 0x3F), bcd(b[2] & 0x3F), bcd(b[1] & 0x7F), bcd(b[0] & 0x7F));
                L(1, b[0] & 0x80 ? "Its battery ran out at some point." : "Clock kept time.");
            }
            break;
        }
        case Tsukumo::D_RFID: {
            char uid[24] = {};
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) { _rfidUid(io, a, uid); return true; });
            if (uid[0]) strncpy(_uid, uid, sizeof(_uid) - 1);
            L(0, uid[0] ? "Card here" : "Hold a card to the reader");
            if (_uid[0]) L(1, "Last ID  %s", _uid);
            break;
        }
        case Tsukumo::D_TOF: {
            uint8_t b[2] = {};
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) {
                io.i2c.writeRegister8(a, 0x00, 0x01, Tsukumo::HZ);    // one ranging
                for (int i = 0; i < 20; i++) { delay(5); if (io.reg8(a, 0x13) & 0x07) break; }
                bool r = io.reg(a, 0x1E, b, 2);
                io.i2c.writeRegister8(a, 0x0B, 0x01, Tsukumo::HZ);    // clear
                return r;
            });
            int mm = b[0] << 8 | b[1];
            if (ok) L(0, mm >= 8190 || mm < 20 ? "Distance     out of range" : "Distance     %d mm (rough)", mm);
            break;
        }
        case Tsukumo::D_JOY: {
            uint8_t b[3];
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) { return io.raw(a, b, 3); });
            if (ok) { L(0, "X %3u   Y %3u", b[0], b[1]); L(1, b[2] ? "Button pressed" : "Button up"); }
            break;
        }
        case Tsukumo::D_CARDKB: {
            uint8_t k = 0;
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) { return io.raw(a, &k, 1); });
            if (k) _lastKey = k;
            L(0, "Type on it to test.");
            if (_lastKey >= 32 && _lastKey < 127) L(1, "Last key  '%c'", _lastKey);
            else if (_lastKey) L(1, "Last key  0x%02X", _lastKey);
            break;
        }
        case Tsukumo::D_ENCODER: {
            uint8_t b[2] = {}; int btn = -1;
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) { btn = io.reg8(a, 0x20); return io.reg(a, 0x10, b, 2); });
            if (ok) { L(0, "Count        %d", (int16_t)(b[0] | b[1] << 8)); L(1, btn == 0 ? "Button pressed" : "Button up"); }
            break;
        }
        case Tsukumo::D_M5FW: {
            int v = -1;
            ok = _on(f, [&](const Tsukumo::Io& io, uint8_t a) { v = io.reg8(a, 0xFE); return v >= 0; });
            if (ok) L(0, "Firmware     v%d", v);
            L(1, "No readings in MitamaOS yet.");
            break;
        }
        default:
            L(0, "No driver yet. It's safe to leave");
            L(1, "plugged in; MitamaOS won't touch it.");
            break;
        }
        if (!ok) L(3, "Didn't answer. Unplugged?");
    }

    // BMP280 compensation (datasheet 4.2.3, float version).
    void _bmp(const uint8_t* b, float& t, float& p) {
        auto u16 = [&](int i) { return (uint16_t)(_cal[i] | _cal[i + 1] << 8); };
        auto s16 = [&](int i) { return (int16_t)u16(i); };
        int32_t adcP = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4);
        int32_t adcT = (b[3] << 12) | (b[4] << 4) | (b[5] >> 4);
        double v1 = (adcT / 16384.0 - u16(0) / 1024.0) * s16(2);
        double v2 = (adcT / 131072.0 - u16(0) / 8192.0); v2 = v2 * v2 * s16(4);
        double tf = v1 + v2;
        t = tf / 5120.0;
        v1 = tf / 2.0 - 64000.0;
        v2 = v1 * v1 * s16(16) / 32768.0 + v1 * s16(14) * 2.0;
        v2 = v2 / 4.0 + s16(12) * 65536.0;
        v1 = (s16(10) * v1 * v1 / 524288.0 + s16(8) * v1) / 524288.0;
        v1 = (1.0 + v1 / 32768.0) * u16(6);
        if (v1 == 0) { p = 0; return; }
        double pr = 1048576.0 - adcP;
        pr = (pr - v2 / 4096.0) * 6250.0 / v1;
        v1 = s16(22) * pr * pr / 2147483648.0;
        v2 = pr * s16(20) / 32768.0;
        p = (pr + (v1 + v2 + s16(18)) / 16.0) / 100.0;
    }

    // MFRC522 / WS1850S: just enough to read a card's ID (REQA, then
    // anticollision for the first cascade level).
    static bool _rfidInit(const Tsukumo::Io& io, uint8_t a) {
        auto w = [&](uint8_t r, uint8_t v) { return io.i2c.writeRegister8(a, r, v, Tsukumo::HZ); };
        w(0x01, 0x0F); delay(50);                    // soft reset
        w(0x2A, 0x80); w(0x2B, 0xA9); w(0x2C, 0x03); w(0x2D, 0xE8);   // ~25 ms timeout
        w(0x15, 0x40); w(0x11, 0x3D);
        int v = io.reg8(a, 0x14);
        return v >= 0 && w(0x14, v | 0x03);          // field on
    }
    static int _rfidXfer(const Tsukumo::Io& io, uint8_t a, const uint8_t* tx, int n, uint8_t bits, uint8_t* rx, int max) {
        auto w = [&](uint8_t r, uint8_t v) { io.i2c.writeRegister8(a, r, v, Tsukumo::HZ); };
        w(0x01, 0x00); w(0x04, 0x7F); w(0x0A, 0x80);  // idle, clear irqs, flush FIFO
        io.i2c.writeRegister(a, 0x09, tx, n, Tsukumo::HZ);
        w(0x0D, bits);
        w(0x01, 0x0C);                                 // transceive
        w(0x0D, bits | 0x80);                          // start send
        uint32_t t = millis(); int irq = 0;
        while (millis() - t < 40) { irq = io.reg8(a, 0x04); if (irq < 0 || irq & 0x31) break; delay(1); }
        w(0x0D, 0x00);
        if (irq < 0 || !(irq & 0x30) || (io.reg8(a, 0x06) & 0x13)) return 0;   // timeout or error
        int got = min(io.reg8(a, 0x0A), max);
        if (got > 0) io.reg(a, 0x09, rx, got);
        return got;
    }
    static void _rfidUid(const Tsukumo::Io& io, uint8_t a, char* out) {
        uint8_t req = 0x26, atqa[2], ac[2] = { 0x93, 0x20 }, u[5];
        io.i2c.bitOff(a, 0x0E, 0x80, Tsukumo::HZ);     // collisions keep bits
        if (_rfidXfer(io, a, &req, 1, 0x07, atqa, 2) != 2) return;
        if (_rfidXfer(io, a, ac, 2, 0x00, u, 5) != 5) return;
        if ((u[0] ^ u[1] ^ u[2] ^ u[3]) != u[4]) return;
        if (u[0] == 0x88) snprintf(out, 24, "%02X%02X%02X...", u[1], u[2], u[3]);   // 7-byte ID, first part
        else snprintf(out, 24, "%02X%02X%02X%02X", u[0], u[1], u[2], u[3]);
    }

    // ── drawing ────────────────────────────────────────────────────────
    void _drawBusy(const char* msg) {
        _canvas.fillSprite(MUI::BG);
        MUI::header(_canvas, "TSUKUMO", MUI::YELLOW);
        MUI::text(_canvas, MUI::small(), msg, 6, HEADER_H + 30, MUI::CYAN);
        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }

    static const char* _where(const F& f, char* buf, size_t n) {
        if (f.bus == Tsukumo::B_CAP) snprintf(buf, n, "Cap port  0x%02X", f.addr);
        else if (f.bus == Tsukumo::B_HUB) snprintf(buf, n, "Hub ch %u  0x%02X", f.hubCh, f.addr);
        else snprintf(buf, n, "Grove  0x%02X", f.addr);
        return buf;
    }

    void _draw() {
        _canvas.fillSprite(MUI::BG);
        MUI::header(_canvas, "TSUKUMO", MUI::YELLOW);
        if (_open >= 0 && _open < _res().n) _drawDetail(); else _drawList();
        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }

    void _drawList() {
        const auto& r = _res();
        int y = HEADER_H + 2;
        char head[48];
        snprintf(head, sizeof(head), "%s", r.capLora ? "LoRa cap fitted" : "No LoRa cap");
        MUI::text(_canvas, MUI::small(), "MODULES", 4, y, MUI::YELLOW, 1);
        MUI::text(_canvas, MUI::small(), head, 62, y, MUI::DIM);
        const int top = HEADER_H + 16, lh = 12;
        const int vis = (SCREEN_H - FOOTER_H - top) / lh;
        if (!r.n) {
            const char* a; const char* b;
            if (r.gpsGrove) { a = "Grove port: GPS Unit (in use)."; b = "G gives the GPS back to the cap."; }
            else if (r.uartPin >= 0 && r.uartNmea) { a = "A GPS Unit is talking on Grove."; b = "G to use it as the GPS."; }
            else if (r.uartPin >= 0) { a = "A serial Unit is talking on Grove."; b = "MitamaOS listens only. Left alone."; }
            else if (r.lineLow) { a = "A Grove line is held low."; b = "Likely a button or sensor. Left alone."; }
            else if (r.linesHigh) { a = "Something powered, but quiet."; b = "U listens for a serial Unit."; }
            else { a = "Nothing found."; b = "Buttons, LEDs and relays can't be seen."; }
            MUI::text(_canvas, MUI::small(), a, 4, top + 4, MUI::WHITE);
            MUI::text(_canvas, MUI::small(), b, 4, top + 17, MUI::DIM);
            MUI::text(_canvas, MUI::small(), "MitamaOS never drives the Grove pins.", 4, top + 34, MUI::DIM);
        }
        if (_sel < _top) _top = _sel;
        if (_sel >= _top + vis) _top = _sel - vis + 1;
        for (int i = 0; i < vis && _top + i < r.n; i++) {
            const F& f = r.dev[_top + i];
            int ry = top + i * lh;
            bool sel = _top + i == _sel;
            if (sel) {
                _canvas.fillRect(1, ry - 1, SCREEN_W - 2, lh, MUI::PANEL);
                _canvas.drawRect(1, ry - 1, SCREEN_W - 2, lh, MUI::MAGENTA);
            }
            bool known = strcmp(f.name, "Unknown device") != 0;
            if (f.drv && f.drv != Tsukumo::D_M5FW) MUI::text(_canvas, MUI::small(), "*", 5, ry, MUI::GREEN);
            char w[24];
            _where(f, w, sizeof(w));
            int rw = MUI::width(_canvas, MUI::small(), w);
            _canvas.setClipRect(14, ry, SCREEN_W - 22 - rw, lh);
            MUI::text(_canvas, MUI::small(), f.name, 14, ry, known ? (sel ? MUI::WHITE : MUI::LABEL) : MUI::DIM);
            _canvas.clearClipRect();
            MUI::text(_canvas, MUI::small(), w, SCREEN_W - 5 - rw, ry, f.bus == Tsukumo::B_CAP ? MUI::MAGENTA : MUI::CYAN);
        }
        const char* foot = _note.length() ? _note.c_str() : r.n ? "ENT look  R rescan  ` back" : "R rescan  U listen  ` back";
        MUI::footer(_canvas, foot, _note.length() ? MUI::YELLOW : MUI::DIM);
    }

    // Word-wraps one note; returns the y below it.
    int _wrap(const char* s, int x, int y, int w, uint16_t col) {
        char line[64]; int n = 0, lastSp = -1;
        for (const char* p = s; ; p++) {
            bool end = !*p;
            if (!end) { line[n] = *p; if (*p == ' ') lastSp = n; n++; line[n] = 0; }
            if (end || n >= (int)sizeof(line) - 1 || MUI::width(_canvas, MUI::small(), line) > w) {
                int cut = end ? n : (lastSp > 0 ? lastSp : n - 1);
                char keep = line[cut]; line[cut] = 0;
                MUI::text(_canvas, MUI::small(), line, x, y, col);
                y += 11;
                if (end) break;
                line[cut] = keep;
                int from = lastSp > 0 ? cut + 1 : cut;
                memmove(line, line + from, n - from + 1);
                n -= from; lastSp = -1;
                for (int i = 0; i < n; i++) if (line[i] == ' ') lastSp = i;
            }
        }
        return y;
    }

    void _drawDetail() {
        const F& f = _res().dev[_open];
        int y = HEADER_H + 3;
        MUI::text(_canvas, MUI::title(), f.name, 4, y, MUI::YELLOW, 1);
        y += 16;
        char w[24];
        MUI::text(_canvas, MUI::small(), _where(f, w, sizeof(w)), 4, y, MUI::CYAN);
        y += 12;
        y = _wrap(f.note, 4, y, SCREEN_W - 8, MUI::DIM) + 4;
        for (int i = 0; i < 4; i++) if (_live[i][0]) {
            MUI::text(_canvas, MUI::small(), _live[i], 8, y, i == 3 ? MUI::YELLOW : MUI::WHITE);
            y += 12;
        }
        MUI::footer(_canvas, "` back to the list", MUI::DIM);
    }
};
