#pragma once
// The M5MonsterC5 (an ESP32-C5 board on the Grove port, running C5Lab's
// JanOS firmware) as a second pair of ears: it hears 5 GHz Wi-Fi, which
// the Cardputer's own radio can't.
//
// JanOS takes text commands over a serial line (its TX on G1, RX on G2,
// 115200 8N1). Most of its commands are attacks. MitamaOS only ever sends
// the few in ALLOWED below, all of which listen:
//   - "deauth_detector": watches every 2.4 and 5 GHz channel for deauth
//     frames (receive-only, after one ordinary network scan at start).
//     Prints "[DEAUTH] CH: <ch> | AP: <name> (<bssid>) | RSSI: <rssi>".
//   - "wifi_analyzer scan --band 5 --profile passive": a passive 5 GHz
//     scan (listens on each channel, sends no probes). Prints one
//     "[WFA1] {...}" JSON line per access point, then a "type":"end" line.
//   - "stop" ends whichever is running; "ping" gets "pong".
// While Scry records, it runs back-to-back 5 GHz scans for the log;
// otherwise it runs the deauth detector. The Cardputer still watches
// 2.4 GHz itself either way.
//
// Polled from the main loop. Starts only if Tsukumo found the board at boot.
#include <Arduino.h>
#include "../system/tsukumo.h"
#include "../detect/detection.h"
#include "../detect/scry.h"

class Monster {
public:
    static Monster& get() { static Monster m; return m; }

    enum State : uint8_t { OFF, STOPPING, DEAUTH, SCAN5 };

    void begin() {
        if (!Tsukumo::Scanner::get().result().monster) return;
        Serial2.setRxBufferSize(4096);
        Serial2.begin(115200, SERIAL_8N1, Tsukumo::G_SCL, Tsukumo::G_SDA);
        Tsukumo::monsterLinked = true;   // Tsukumo leaves the serial line to us now
        _heardMs = millis();
        _send(C_STOP);              // in case it kept running from before a reboot
        _state = STOPPING; _stateMs = millis();
    }

    void tick() {
        if (_state == OFF) return;
        _readLines();
        uint32_t now = millis();
        bool scry = ScryLogger::get().recording();
        switch (_state) {
        case STOPPING:
            if (now - _stateMs < 2000) break;   // let its radio settle
            if (scry) { _send(C_SCAN5); _state = SCAN5; }
            else { _send(C_DEAUTH); _state = DEAUTH; }
            _stateMs = now;
            break;
        case DEAUTH:
            if (scry) _stop();
            break;
        case SCAN5:
            // Normally ends itself (~15 s); move on if the end line got lost.
            if (_scanDone || now - _stateMs > 45000) { _scanDone = false; _stop(); }
            break;
        default: break;
        }
        _checkFlood(now);
        // A quiet board is normal (nothing to report); check it's still there.
        if (now - _heardMs > 60000 && now - _pingMs > 60000) { _pingMs = now; _send(C_PING); _pings++; }
        if (_pings >= 3) {                  // unplugged
            _state = OFF; Serial2.end(); Tsukumo::monsterLinked = false;
            pinMode(Tsukumo::G_SDA, INPUT); pinMode(Tsukumo::G_SCL, INPUT);
        }
    }

    bool     active() const { return _state != OFF; }
    State    state() const { return _state; }
    uint32_t seen5() const { return _seen5; }        // 5 GHz access points heard this session
    uint32_t deauthFrames() const { return _deauthTotal; }

private:
    enum Cmd : uint8_t { C_PING, C_STOP, C_DEAUTH, C_SCAN5 };
    // The only strings MitamaOS sends the board.
    static const char* _allowed(Cmd c) {
        switch (c) {
        case C_PING:   return "ping";
        case C_STOP:   return "stop";
        case C_DEAUTH: return "deauth_detector";
        case C_SCAN5:  return "wifi_analyzer scan --band 5 --profile passive --limit 64";
        }
        return "ping";
    }

    State    _state = OFF;
    uint32_t _stateMs = 0, _heardMs = 0, _pingMs = 0;
    uint8_t  _pings = 0;
    bool     _scanDone = false;
    char     _line[512]; int _n = 0;
    uint32_t _seen5 = 0, _deauthTotal = 0;
    uint32_t _seenHash[64] = {};     // small "already counted" set for seen5
    // deauth flood window
    uint32_t _winMs = 0; uint16_t _winN = 0;
    uint8_t  _floodAp[6] = {}; int8_t _floodRssi = 0;

    void _send(Cmd c) { Serial2.print(_allowed(c)); Serial2.print("\r\n"); }
    void _stop() { _send(C_STOP); _state = STOPPING; _stateMs = millis(); }

    void _readLines() {
        int budget = 2048;
        while (Serial2.available() && budget--) {
            char c = Serial2.read();
            if (c == '\n' || c == '\r') {
                if (_n) { _line[_n] = 0; _onLine(_line); _n = 0; }
            } else if (_n < (int)sizeof(_line) - 1) _line[_n++] = c;
        }
    }

    void _onLine(const char* l) {
        _heardMs = millis(); _pings = 0;
        if (const char* d = strstr(l, "[DEAUTH]")) _onDeauth(d);
        else if (const char* w = strstr(l, "[WFA1]")) _onWfa(w);
    }

    static bool _mac(const char* s, uint8_t out[6]) {
        unsigned b[6];
        if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
        for (int i = 0; i < 6; i++) out[i] = b[i];
        return true;
    }

    // "[DEAUTH] CH: 36 | AP: Name (AA:BB:CC:DD:EE:FF) | RSSI: -60"
    void _onDeauth(const char* l) {
        const char* r = strstr(l, "| RSSI: ");
        const char* p = r ? r : l + strlen(l);
        while (p > l && *p != '(') p--;            // the last "(" before RSSI: the BSSID
        uint8_t mac[6] = {};
        if (*p == '(') _mac(p + 1, mac);
        _deauthTotal++;
        _winN++;
        memcpy(_floodAp, mac, 6);
        _floodRssi = r ? (int8_t)atoi(r + 8) : 0;
    }

    // A real flood shows up as a burst; one deauth now and then is just a
    // phone leaving its network. The board hops channels, so it only sees
    // part of any burst.
    void _checkFlood(uint32_t now) {
        if (now - _winMs < 10000) return;
        if (_winN >= 8) {
            Sig::Hit h; h.type = Sig::T_DEAUTH; h.grade = Sig::G_HIGH; h.detail = "deauth flood (M5Monster)";
            DetectionEngine::get().postExternal(h, _floodAp, _floodRssi);
        }
        _winMs = now; _winN = 0;
    }

    static const char* _str(const char* l, const char* key, char* out, size_t n) {
        out[0] = 0;
        const char* p = strstr(l, key);
        if (!p) return nullptr;
        p += strlen(key);
        size_t i = 0;
        while (*p && *p != '"' && i < n - 1) out[i++] = *p++;
        out[i] = 0;
        return out;
    }
    static long _num(const char* l, const char* key, long def) {
        const char* p = strstr(l, key);
        return p ? atol(p + strlen(key)) : def;
    }

    static const char* _wigleAuth(const char* a) {
        if (!strcmp(a, "OPEN")) return "[ESS]";
        if (!strcmp(a, "WEP")) return "[WEP][ESS]";
        if (!strcmp(a, "WPA_PSK")) return "[WPA-PSK-TKIP][ESS]";
        if (!strcmp(a, "WPA2_PSK")) return "[WPA2-PSK-CCMP][ESS]";
        if (!strcmp(a, "WPA_WPA2_PSK")) return "[WPA-PSK-CCMP][WPA2-PSK-CCMP][ESS]";
        if (!strcmp(a, "WPA3_PSK")) return "[WPA2-SAE-CCMP][ESS]";
        if (!strcmp(a, "WPA2_WPA3_PSK")) return "[WPA2-PSK-CCMP][WPA2-SAE-CCMP][ESS]";
        if (strstr(a, "ENTERPRISE") || !strcmp(a, "WPA3_ENT_192")) return "[WPA2-EAP-CCMP][ESS]";
        if (!strcmp(a, "OWE")) return "[WPA2-OWE-CCMP][ESS]";
        return "[ESS]";
    }

    void _onWfa(const char* l) {
        char type[8];
        _str(l, "\"type\":\"", type, sizeof(type));
        if (!strcmp(type, "end")) { _scanDone = true; return; }
        if (strcmp(type, "ap")) return;
        char band[4], bssid[20], hex[70], auth[24];
        _str(l, "\"band\":\"", band, sizeof(band));
        if (strcmp(band, "5")) return;
        Scry::Ap ap = {};
        if (!_mac(_str(l, "\"bssid\":\"", bssid, sizeof(bssid)) ? bssid : "", ap.bssid)) return;
        _str(l, "\"ssid_hex\":\"", hex, sizeof(hex));
        size_t k = 0;
        for (size_t i = 0; hex[i] && hex[i + 1] && k < sizeof(ap.ssid) - 1; i += 2) {
            unsigned v; char two[3] = { hex[i], hex[i + 1], 0 };
            if (sscanf(two, "%2x", &v) == 1) ap.ssid[k++] = (char)v;
        }
        ap.ssid[k] = 0;
        ap.channel = (uint8_t)_num(l, "\"primary\":", 0);
        _str(l, "\"auth\":\"", auth, sizeof(auth));
        strncpy(ap.auth, _wigleAuth(auth), sizeof(ap.auth) - 1);
        int8_t rssi = (int8_t)_num(l, "\"rssi\":", -100);
        ScryLogger::get().onAp(ap, rssi);
        _count(ap.bssid);
    }

    void _count(const uint8_t m[6]) {
        uint32_t h = 2166136261u;
        for (int i = 0; i < 6; i++) h = (h ^ m[i]) * 16777619u;
        h |= 1;
        int slot = h % 64;
        for (int i = 0; i < 64; i++) {
            int s = (slot + i) % 64;
            if (_seenHash[s] == h) return;
            if (!_seenHash[s]) { _seenHash[s] = h; _seen5++; return; }
        }
        _seen5++;    // set full: just count
    }
};
