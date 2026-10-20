#pragma once
// Aether's data: which Wi-Fi networks are beaconing on which channel.
// Fed from the watcher's promiscuous callback (beacons other routers
// already broadcast - nothing is sent), read by the Aether app.
#include <Arduino.h>

class AetherTally {
public:
    static AetherTally& get() { static AetherTally a; return a; }
    static constexpr int CHANNELS = 13;

    struct Channel { uint8_t aps = 0; int8_t best = -127; };

    // A beacon frame (802.11 header + body), heard on `rxCh`.
    void onBeacon(const uint8_t* p, int len, uint8_t rxCh, int8_t rssi) {
        if (len < 36 || p[0] != 0x80) return;
        uint8_t ch = rxCh;
        for (int i = 36; i + 2 <= len; ) {          // DS Parameter Set (tag 3) names the real channel
            uint8_t tag = p[i], l = p[i + 1];
            if (i + 2 + l > len) break;
            if (tag == 3 && l == 1) { ch = p[i + 2]; break; }
            i += 2 + l;
        }
        if (ch < 1 || ch > CHANNELS) return;
        const uint8_t* bssid = p + 16;
        uint32_t now = millis();
        portENTER_CRITICAL_ISR(&_mux);
        int slot = -1, oldest = 0;
        for (int i = 0; i < N; i++) {
            if (_ap[i].ms && memcmp(_ap[i].bssid, bssid, 6) == 0) { slot = i; break; }
            if (_ap[i].ms < _ap[oldest].ms) oldest = i;
        }
        if (slot < 0) { slot = oldest; memcpy(_ap[slot].bssid, bssid, 6); }
        _ap[slot].ch = ch; _ap[slot].rssi = rssi; _ap[slot].ms = now ? now : 1;
        portEXIT_CRITICAL_ISR(&_mux);
    }

    // Networks heard in the last `windowMs`, per channel (index 0 = ch 1).
    int snapshot(Channel out[CHANNELS], uint32_t windowMs = 60000) {
        for (int c = 0; c < CHANNELS; c++) out[c] = Channel();
        int total = 0;
        uint32_t now = millis();
        portENTER_CRITICAL(&_mux);
        for (auto& a : _ap) {
            if (!a.ms || now - a.ms > windowMs) continue;
            Channel& c = out[a.ch - 1];
            if (c.aps < 255) c.aps++;
            if (a.rssi > c.best) c.best = a.rssi;
            total++;
        }
        portEXIT_CRITICAL(&_mux);
        return total;
    }

private:
    static constexpr int N = 64;
    struct Ap { uint8_t bssid[6]; uint8_t ch; int8_t rssi; uint32_t ms; } _ap[N] = {};
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    AetherTally() = default;
};
