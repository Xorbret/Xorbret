#pragma once
// Scry (war driving) helpers: pull access-point details out of a received
// beacon / probe response and format WiGLE CSV rows. Pure C++ (no Arduino)
// so it can be tested on a PC. Receive-only: these frames were broadcast by
// the access points themselves.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace Scry {

struct Ap {
    uint8_t bssid[6];
    char    ssid[33];
    uint8_t channel;
    char    auth[48];      // WiGLE capabilities string, e.g. [WPA2-PSK-CCMP][ESS]
};

// RSN / WPA element body -> "[WPA2-PSK-CCMP]" style tag. Reads the first
// pairwise cipher and the first AKM suite only.
inline void _rsnTag(char* out, size_t n, const char* proto, const uint8_t* d, size_t len, size_t start) {
    const char* akm = "";
    const char* cipher = "";
    size_t i = start + 4;                         // skip group cipher suite
    if (i + 2 <= len) {
        uint16_t pc = d[i] | (d[i + 1] << 8);
        i += 2;
        if (pc >= 1 && i + 4 <= len) {
            uint8_t t = d[i + 3];
            cipher = t == 4 ? "CCMP" : t == 2 ? "TKIP" : t == 8 ? "GCMP" : "";
        }
        i += 4 * pc;
        if (i + 2 <= len) {
            uint16_t ac = d[i] | (d[i + 1] << 8);
            i += 2;
            if (ac >= 1 && i + 4 <= len) {
                uint8_t t = d[i + 3];
                akm = t == 2 ? "PSK" : t == 1 ? "EAP" : t == 8 ? "SAE" : t == 6 ? "PSK-SHA256" : t == 18 ? "OWE" : "";
            }
        }
    }
    // SAE / OWE are WPA3
    if (strcmp(akm, "SAE") == 0 || strcmp(akm, "OWE") == 0) proto = "WPA3";
    snprintf(out, n, "[%s%s%s%s%s]", proto, *akm ? "-" : "", akm, *cipher ? "-" : "", cipher);
}

// Parses a beacon (subtype 8) or probe response (5). Returns false for
// anything else. fallbackChannel is the radio channel it arrived on.
inline bool parseBeacon(const uint8_t* f, size_t n, uint8_t fallbackChannel, Ap& ap) {
    if (n < 36) return false;
    uint8_t type = (f[0] >> 2) & 3, sub = f[0] >> 4;
    if (type != 0 || (sub != 8 && sub != 5)) return false;
    memcpy(ap.bssid, f + 16, 6);
    ap.ssid[0] = 0;
    ap.channel = fallbackChannel;
    bool privacy = f[34] & 0x10;
    char rsn[20] = "", wpa[20] = "";

    size_t i = 36;
    while (i + 2 <= n) {
        uint8_t id = f[i], l = f[i + 1];
        if (i + 2 + l > n) break;
        const uint8_t* d = f + i + 2;
        if (id == 0) {
            size_t k = 0;
            for (size_t j = 0; j < l && k < 32; j++) {
                char c = (char)d[j];
                if ((uint8_t)c < 0x20 || c == 0x7F) continue;   // drop control bytes
                ap.ssid[k++] = c;
            }
            ap.ssid[k] = 0;
        } else if (id == 3 && l >= 1) {
            ap.channel = d[0];
        } else if (id == 48 && l >= 2) {
            _rsnTag(rsn, sizeof(rsn), "WPA2", d, l, 2);
        } else if (id == 221 && l >= 8 && d[0] == 0x00 && d[1] == 0x50 && d[2] == 0xF2 && d[3] == 1) {
            _rsnTag(wpa, sizeof(wpa), "WPA", d, l, 6);
        }
        i += 2 + l;
    }
    if (*rsn || *wpa) snprintf(ap.auth, sizeof(ap.auth), "%s%s[ESS]", wpa, rsn);
    else              snprintf(ap.auth, sizeof(ap.auth), "%s[ESS]", privacy ? "[WEP]" : "");
    return true;
}

inline int channelFreq(uint8_t ch) {
    if (ch == 14) return 2484;
    if (ch >= 1 && ch <= 13) return 2407 + 5 * ch;
    if (ch >= 32) return 5000 + 5 * ch;
    return 0;
}

// WiGLE 1.6 file header (two lines).
inline int header(char* out, size_t n, const char* version) {
    return snprintf(out, n,
        "WigleWifi-1.6,appRelease=%s,model=Cardputer ADV,release=%s,device=MitamaOS,"
        "display=ST7789V2,board=ESP32-S3,brand=M5Stack,star=Sol,body=3,subBody=0\n"
        "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,"
        "AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\n", version, version);
}

// Writes s as one CSV field, quoted when it holds a comma or quote.
inline size_t _field(char* out, size_t n, const char* s) {
    bool q = strpbrk(s, ",\"") != nullptr;
    size_t k = 0;
    if (q && k < n) out[k++] = '"';
    for (; *s && k + 2 < n; s++) {
        if (*s == '"') out[k++] = '"';
        out[k++] = *s;
    }
    if (q && k < n) out[k++] = '"';
    if (k < n) out[k] = 0;
    return k;
}

struct Where { double lat, lon; float altM, accM; const char* utc; };   // utc "YYYY-MM-DD HH:MM:SS"

inline int wifiRow(char* out, size_t n, const Ap& ap, int rssi, const Where& w) {
    char ssid[80];
    _field(ssid, sizeof(ssid), ap.ssid);
    const uint8_t* b = ap.bssid;
    return snprintf(out, n, "%02x:%02x:%02x:%02x:%02x:%02x,%s,%s,%s,%u,%d,%d,%.6f,%.6f,%.1f,%.1f,,,WIFI\n",
                    b[0], b[1], b[2], b[3], b[4], b[5], ssid, ap.auth, w.utc, ap.channel,
                    channelFreq(ap.channel), rssi, w.lat, w.lon, w.altM, w.accM);
}

inline int bleRow(char* out, size_t n, const uint8_t mac[6], const char* name, int rssi, int mfgr, const Where& w) {
    char nm[80];
    _field(nm, sizeof(nm), name ? name : "");
    char mf[12] = "";
    if (mfgr >= 0) snprintf(mf, sizeof(mf), "%d", mfgr);
    return snprintf(out, n, "%02x:%02x:%02x:%02x:%02x:%02x,%s,Misc [LE],%s,0,,%d,%.6f,%.6f,%.1f,%.1f,,%s,BLE\n",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], nm, w.utc, rssi,
                    w.lat, w.lon, w.altM, w.accM, mf);
}

// Name (AD 0x08/0x09) and manufacturer company ID (AD 0xFF) of a BLE advert.
inline void bleInfo(const uint8_t* adv, size_t len, char* name, size_t nn, int& mfgr) {
    name[0] = 0; mfgr = -1;
    size_t i = 0;
    while (i + 1 < len) {
        uint8_t l = adv[i];
        if (l == 0 || i + 1 + l > len) break;
        uint8_t t = adv[i + 1];
        const uint8_t* d = adv + i + 2;
        size_t dn = l - 1;
        if ((t == 0x08 || t == 0x09) && !name[0]) {
            size_t k = 0;
            for (size_t j = 0; j < dn && k + 1 < nn; j++) if (d[j] >= 0x20 && d[j] != 0x7F) name[k++] = (char)d[j];
            name[k] = 0;
        } else if (t == 0xFF && dn >= 2 && mfgr < 0) {
            mfgr = d[0] | (d[1] << 8);
        }
        i += 1 + l;
    }
}

// Seen-filter so each device is logged once per window: a 64K-bit Bloom
// filter (8KB) with three hashes; false-positive rate ~0.2% at 3000 entries.
struct Seen {
    uint8_t* bits = nullptr;     // 8192 bytes when active
    static constexpr uint32_t M = 65536;
    static uint32_t _h(const uint8_t* k, size_t n, uint32_t seed) {
        uint32_t h = 2166136261u ^ seed;
        for (size_t i = 0; i < n; i++) { h ^= k[i]; h *= 16777619u; }
        return h % M;
    }
    // true if (probably) seen; marks it seen otherwise
    bool testSet(const uint8_t* k, size_t n) {
        if (!bits) return false;
        uint32_t a = _h(k, n, 0x9e37), b = _h(k, n, 0x85eb), c = _h(k, n, 0xc2b2);
        bool seen = (bits[a >> 3] >> (a & 7) & 1) && (bits[b >> 3] >> (b & 7) & 1) && (bits[c >> 3] >> (c & 7) & 1);
        bits[a >> 3] |= 1 << (a & 7); bits[b >> 3] |= 1 << (b & 7); bits[c >> 3] |= 1 << (c & 7);
        return seen;
    }
    void clear() { if (bits) memset(bits, 0, M / 8); }
};

} // namespace Scry
