#pragma once
// Detection signatures (outline §5, §19). Pure C++ with no Arduino or
// ESP-IDF types, so the matchers can be compiled and tested on a PC.
//
// Every rule is a public fact: IEEE MAC block owners (oui_table.h),
// Bluetooth SIG company IDs and 16-bit service UUIDs, Apple's published
// Find My advertisement layout, the ASTM F3411 Remote ID beacon format,
// and names/SSIDs the devices themselves broadcast. Matching only reads
// frames other devices already sent; nothing here transmits.
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace Sig {

// Species (outline §19). Order is stored in sightings; append only.
enum Type : uint8_t {
    T_FLOCK, T_AXON, T_ALPR,                          // Vile
    T_CAMERA, T_RING, T_META,                         // Night
    T_AIRTAG, T_TILE, T_SAMSUNG_TAG, T_GOOGLE_TAG,    // Fairy
    T_DRONE,                                          // Avian
    T_IBEACON,                                        // Herald
    T_SKIMMER,                                        // Foul
    T_RAVEN,                                          // Jaki
    T_DEAUTH, T_EVILTWIN, T_HACKER,                   // Fiend
    T_COUNT
};

enum Grade : uint8_t { G_LOW, G_MED, G_HIGH };

enum Race : uint8_t { R_VILE, R_NIGHT, R_FAIRY, R_AVIAN, R_HERALD, R_FOUL, R_JAKI, R_FIEND, R_COUNT };

inline const char* typeName(uint8_t t) {
    static const char* const N[T_COUNT] = {
        "FLOCK", "AXON", "ALPR", "CAMERA", "RING", "META",
        "AIRTAG", "TILE", "SMARTTAG", "GOOGLE TAG", "DRONE", "IBEACON",
        "SKIMMER", "RAVEN", "DEAUTH", "EVIL TWIN", "HACKER",
    };
    return t < T_COUNT ? N[t] : "?";
}

inline Race raceOf(uint8_t t) {
    switch (t) {
        case T_FLOCK: case T_AXON: case T_ALPR:            return R_VILE;
        case T_CAMERA: case T_RING: case T_META:           return R_NIGHT;
        case T_AIRTAG: case T_TILE: case T_SAMSUNG_TAG:
        case T_GOOGLE_TAG:                                 return R_FAIRY;
        case T_DRONE:                                      return R_AVIAN;
        case T_IBEACON:                                    return R_HERALD;
        case T_SKIMMER:                                    return R_FOUL;
        case T_RAVEN:                                      return R_JAKI;
        default:                                           return R_FIEND;
    }
}

inline const char* raceName(uint8_t r) {
    static const char* const N[R_COUNT] = { "Vile", "Night", "Fairy", "Avian", "Herald", "Foul", "Jaki", "Fiend" };
    return r < R_COUNT ? N[r] : "?";
}

// Threats are things acting against you right now (attack tools, skimmers);
// everything else is surveillance or trackers worth knowing about.
inline bool isThreat(uint8_t t) { Race r = raceOf(t); return r == R_FIEND || r == R_FOUL; }

struct Hit {
    uint8_t     type  = T_COUNT;   // T_COUNT = no match
    uint8_t     grade = G_LOW;
    const char* detail = nullptr;  // vendor or device name, static string
    bool ok() const { return type < T_COUNT; }
};

} // namespace Sig

#include "oui_table.h"

namespace Sig {

// ── helpers ──────────────────────────────────────────────────────────────

inline bool startsWith(const char* s, size_t n, const char* p) {
    size_t k = strlen(p);
    return n >= k && memcmp(s, p, k) == 0;
}
inline bool equals(const char* s, size_t n, const char* p) {
    return n == strlen(p) && memcmp(s, p, n) == 0;
}
inline bool contains(const uint8_t* d, size_t n, const char* p) {
    size_t k = strlen(p);
    if (k == 0 || n < k) return false;
    for (size_t i = 0; i + k <= n; i++) if (memcmp(d + i, p, k) == 0) return true;
    return false;
}
inline bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Locally administered (randomised) MACs carry no vendor block.
inline bool isRandomMac(const uint8_t mac[6]) { return mac[0] & 0x02; }

// OUI lookup on a MAC written most-significant byte first.
inline Hit matchOui(const uint8_t mac[6]) {
    Hit h;
    if (isRandomMac(mac) || (mac[0] & 0x01)) return h;   // random or multicast
    uint32_t key = ((uint32_t)mac[0] << 16) | ((uint32_t)mac[1] << 8) | mac[2];
    int lo = 0, hi = (int)OUI_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t v = OUI_TABLE[mid].oui;
        if (v == key) {
            h.type = OUI_TABLE[mid].type;
            h.grade = OUI_TABLE[mid].grade;
            h.detail = OUI_LABELS[OUI_TABLE[mid].label];
            return h;
        }
        if (v < key) lo = mid + 1; else hi = mid - 1;
    }
    return h;
}

inline Hit hit(uint8_t t, uint8_t g, const char* d) { Hit h; h.type = t; h.grade = g; h.detail = d; return h; }

// ── Bluetooth LE ─────────────────────────────────────────────────────────

inline Hit _bleUuid16(uint16_t u, const uint8_t* sd, size_t sdn) {
    switch (u) {
        case 0xFD5F:                         return hit(T_META, G_HIGH, "Meta glasses");
        case 0xFEB7: case 0xFEB8: case 0xFE45: return hit(T_META, G_MED, "Meta");
        case 0xFEED: case 0xFEEC: case 0xFD84: return hit(T_TILE, G_HIGH, "Tile");
        case 0xFD5A:                         return hit(T_SAMSUNG_TAG, G_HIGH, "Samsung SmartTag");
        case 0xFCB2: case 0xFE33: case 0xFE65: return hit(T_GOOGLE_TAG, G_MED, "Find My Device");
        case 0xFEAA:   // Eddystone: only the Find My Device network frames
            if (sd && sdn >= 1 && (sd[0] == 0x40 || sd[0] == 0x41)) return hit(T_GOOGLE_TAG, G_HIGH, "Find My Device");
            break;
        case 0x3081: case 0x3082: case 0x3083: return hit(T_HACKER, G_HIGH, "Flipper Zero");
        case 0x3100: case 0x3200: case 0x3300: case 0x3400: case 0x3500:
                                             return hit(T_RAVEN, G_MED, "Raven sensor");
        case 0xFC81: case 0xFE6B: case 0xFE6C: return hit(T_AXON, G_HIGH, "Axon");
        case 0xFFFA:   // ASTM F3411 Remote ID, carried as service data
            if (sd && sdn >= 1 && sd[0] == 0x0D) return hit(T_DRONE, G_HIGH, "Remote ID");
            break;
    }
    return Hit();
}

inline Hit _bleName(const char* s, size_t n) {
    if (startsWith(s, n, "Flipper "))       return hit(T_HACKER, G_MED, "Flipper Zero");
    if (equals(s, n, "HC-05") || equals(s, n, "HC-06") || equals(s, n, "HC-03")
        || equals(s, n, "BT04-A") || startsWith(s, n, "RNBT"))
                                            return hit(T_SKIMMER, G_MED, "serial BT module");
    if (equals(s, n, "Flock") || equals(s, n, "Penguin") || equals(s, n, "Pigvision")
        || equals(s, n, "Flock_Setup") || equals(s, n, "FS Ext Battery"))
                                            return hit(T_FLOCK, G_MED, "Flock Safety");
    if (n >= 7 && n <= 15 && startsWith(s, n, "FS-")) {
        bool hex = true;
        for (size_t i = 3; i < n; i++) if (!isHex(s[i])) { hex = false; break; }
        if (hex) return hit(T_FLOCK, G_HIGH, "Flock Safety");
    }
    if (contains((const uint8_t*)s, n, "BodyWorn")) return hit(T_AXON, G_MED, "body camera");
    return Hit();
}

inline Hit _bleManufacturer(const uint8_t* d, size_t n) {
    if (n < 2) return Hit();
    uint16_t cid = d[0] | (d[1] << 8);
    const uint8_t* p = d + 2; size_t pn = n - 2;
    switch (cid) {
        case 0x004C:   // Apple
            // Find My "separated" broadcast: type 0x12, length 0x19. Owners'
            // own nearby iPhones use a short form; only full-length counts.
            if (pn >= 2 && p[0] == 0x12 && p[1] >= 0x19) return hit(T_AIRTAG, G_MED, "Find My tag");
            if (pn >= 2 && p[0] == 0x02 && p[1] == 0x15) return hit(T_IBEACON, G_LOW, "iBeacon");
            break;
        case 0x01AB: return hit(T_META, G_MED, "Meta");
        case 0x060C: return hit(T_META, G_MED, "Vuzix glasses");
        case 0x034D: return hit(T_AXON, G_HIGH, "Axon");
        case 0x09C8: return hit(T_FLOCK, G_LOW, "Flock Safety");
        case 0x08C3: return hit(T_GOOGLE_TAG, G_MED, "Chipolo");
    }
    return Hit();
}

// Matches one advertisement (raw AD structures, as received). mac is MSB
// first; publicAddr says whether it's a vendor-assigned address.
inline Hit matchBle(const uint8_t* adv, size_t len, const uint8_t mac[6], bool publicAddr) {
    Hit best;
    auto take = [&](const Hit& h) { if (h.ok() && (!best.ok() || h.grade > best.grade)) best = h; };

    size_t i = 0;
    while (i + 1 < len) {
        uint8_t l = adv[i];
        if (l == 0 || i + 1 + l > len) break;
        uint8_t t = adv[i + 1];
        const uint8_t* d = adv + i + 2;
        size_t n = l - 1;
        switch (t) {
            case 0x02: case 0x03:   // 16-bit service UUID lists
                for (size_t k = 0; k + 1 < n; k += 2) take(_bleUuid16(d[k] | (d[k + 1] << 8), nullptr, 0));
                break;
            case 0x16:              // 16-bit service data
                if (n >= 2) take(_bleUuid16(d[0] | (d[1] << 8), d + 2, n - 2));
                break;
            case 0x08: case 0x09:   // name
                take(_bleName((const char*)d, n));
                break;
            case 0xFF:
                take(_bleManufacturer(d, n));
                break;
        }
        i += 1 + l;
    }
    // Axon devices tag their adverts with this string (seen both ways round).
    if (!best.ok() && (contains(adv, len, "BWCDEVICE") || contains(adv, len, "ECIVEDCWB")))
        take(hit(T_AXON, G_HIGH, "Axon body camera"));
    if (publicAddr) { Hit o = matchOui(mac); if (!best.ok()) take(o); }
    return best;
}

// ── Wi-Fi ────────────────────────────────────────────────────────────────

inline Hit _wifiSsid(const char* s, size_t n) {
    if (startsWith(s, n, "Flock Camera"))   return hit(T_FLOCK, G_HIGH, "Flock Safety");
    if (equals(s, n, "Flock") || startsWith(s, n, "Flock-") || startsWith(s, n, "flock-")
        || startsWith(s, n, "FLOCK-"))      return hit(T_FLOCK, G_MED, "Flock Safety");
    if (startsWith(s, n, "AB2-") || startsWith(s, n, "AB3-") || startsWith(s, n, "AB4-"))
                                            return hit(T_AXON, G_MED, "Axon body camera");
    if (startsWith(s, n, "Pineapple_"))     return hit(T_HACKER, G_MED, "WiFi Pineapple");
    if (startsWith(s, n, "pwned"))          return hit(T_HACKER, G_MED, "attack tool");
    return Hit();
}

// Information elements of a beacon / probe response body (after the fixed
// 12-byte timestamp/interval/capabilities part).
inline Hit _wifiIes(const uint8_t* ie, size_t n) {
    Hit best;
    auto take = [&](const Hit& h) { if (h.ok() && (!best.ok() || h.grade > best.grade)) best = h; };
    size_t i = 0;
    while (i + 2 <= n) {
        uint8_t id = ie[i], l = ie[i + 1];
        if (i + 2 + l > n) break;
        const uint8_t* d = ie + i + 2;
        if (id == 0 && l > 0) take(_wifiSsid((const char*)d, l));
        if (id == 221 && l >= 4) {
            if (d[0] == 0xFA && d[1] == 0x0B && d[2] == 0xBC && d[3] == 0x0D)
                take(hit(T_DRONE, G_HIGH, "Remote ID"));
            else if (contains(d, l, "pwnd_tot"))
                take(hit(T_HACKER, G_HIGH, "Pwnagotchi"));
        }
        i += 2 + l;
    }
    return best;
}

enum WifiKind : uint8_t { W_OTHER, W_DEAUTH };

struct WifiResult {
    Hit     hit;
    uint8_t kind = W_OTHER;
    uint8_t src[6] = {0};   // the transmitting device the hit is about
    bool    hasSrc = false;
};

// Classifies one received 802.11 frame (no FCS needed).
inline WifiResult matchWifi(const uint8_t* f, size_t n) {
    WifiResult r;
    if (n < 24) return r;
    uint8_t fc0 = f[0], fc1 = f[1];
    uint8_t type = (fc0 >> 2) & 3, sub = fc0 >> 4;
    if (type == 0) {                       // management
        memcpy(r.src, f + 10, 6); r.hasSrc = true;
        if (sub == 12 || sub == 10) { r.kind = W_DEAUTH; return r; }
        if ((sub == 8 || sub == 5) && n > 36) r.hit = _wifiIes(f + 36, n - 36);
        if (!r.hit.ok()) r.hit = matchOui(r.src);
    } else if (type == 2) {                // data: the station end of the link
        bool toDs = fc1 & 1, fromDs = fc1 & 2;
        if (toDs && fromDs) return r;      // mesh / WDS, skip
        const uint8_t* sta = fromDs ? f + 4 : f + 10;
        memcpy(r.src, sta, 6); r.hasSrc = true;
        r.hit = matchOui(r.src);
    }
    return r;
}

} // namespace Sig
