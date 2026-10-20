#pragma once
// The Compendium's memory (outline §5a, §16): one entry per species, kept
// across reboots in /MitamaOS/state/compendium.dat.
//
// The detection watcher calls note() for every sighting and tick() from
// its loop, which saves to SD (right away for a first meeting, else at
// most every 30 s) when something changed. Saves are atomic: write .tmp, then swap it in, so a
// dead battery mid-write leaves the previous file intact. Without an SD
// card everything still works, it just forgets on reboot.
#include <Arduino.h>
#include <SD.h>
#include <time.h>
#include "signatures.h"

class Compendium {
public:
    static Compendium& get() { static Compendium c; return c; }

    struct Entry {
        uint32_t first = 0;      // unix time of first meeting (0 = clock wasn't set)
        uint32_t last = 0;       // unix time of the latest sighting (0 = unknown)
        uint32_t count = 0;      // separate encounters (a new device, or one back after a while)
        int8_t   best = -127;    // strongest signal, dBm
        uint8_t  met = 0;
        uint8_t  pad[2] = {0, 0};
    };

    // iBeacons are shop/venue beacons: everywhere and harmless, so the
    // watcher ignores them and they have no Compendium page. The species
    // keeps its slot so saved files stay valid.
    static bool listed(uint8_t t) { return t < Sig::T_COUNT && t != Sig::T_IBEACON; }
    static int total() { int n = 0; for (uint8_t t = 0; t < Sig::T_COUNT; t++) n += listed(t); return n; }

    // Event for the Mitama: a species met for the first time, and whether
    // that finished its race. Read once with takeNews().
    struct News { uint8_t type = Sig::T_COUNT; bool raceDone = false; };

    void load() {
        _sd = SD.cardType() != CARD_NONE;
        if (!_sd) return;
        // Power died between "remove old" and "rename new": finish the swap.
        if (!SD.exists(PATH) && SD.exists(TMP)) SD.rename(TMP, PATH);
        File f = SD.open(PATH, FILE_READ);
        if (!f) return;
        Header h;
        // Version 1 with our entry size only: anything else would be read
        // as garbage, so we start a fresh book instead.
        if (f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) && memcmp(h.magic, "MTC1", 4) == 0
            && h.ver == 1 && h.esz == sizeof(Entry)) {
            Entry tmp[Sig::T_COUNT] = {};
            int n = min<int>(h.n, Sig::T_COUNT);      // species are append-only
            size_t want = n * sizeof(Entry);
            if (f.read((uint8_t*)tmp, want) == want) {
                portENTER_CRITICAL(&_mux);
                memcpy(_e, tmp, want);
                portEXIT_CRITICAL(&_mux);
            }
        }
        f.close();
    }

    // A sighting. `fresh` = a device the watcher wasn't already tracking.
    void note(uint8_t t, int8_t rssi, bool fresh) {
        if (!listed(t)) return;
        uint32_t now = _now();
        portENTER_CRITICAL(&_mux);
        Entry& e = _e[t];
        bool first = !e.met;
        if (first) { e.met = 1; e.first = now; }
        if (fresh || first) { e.count++; _dirty = true; }
        if (rssi > e.best) { e.best = rssi; _minor = true; }
        if (now) { e.last = now; _minor = true; }
        if (first && _newsN < NEWS_MAX) {
            // "Race complete" only means something for races of 2 or more.
            uint8_t r = Sig::raceOf(t);
            int size = raceSize(r);
            _newsQ[_newsN++] = { t, size > 1 && _raceMet(r) == size };
            _urgent = true;
        }
        portEXIT_CRITICAL(&_mux);
    }

    bool takeNews(News& out) {
        portENTER_CRITICAL(&_mux);
        bool had = _newsN > 0;
        if (had) { out = _newsQ[0]; memmove(_newsQ, _newsQ + 1, sizeof(News) * --_newsN); }
        portEXIT_CRITICAL(&_mux);
        return had;
    }

    Entry entry(uint8_t t) {
        Entry e;
        if (t >= Sig::T_COUNT) return e;
        portENTER_CRITICAL(&_mux);
        e = _e[t];
        portEXIT_CRITICAL(&_mux);
        return e;
    }

    int metCount() {
        int n = 0;
        portENTER_CRITICAL(&_mux);
        for (uint8_t t = 0; t < Sig::T_COUNT; t++) n += listed(t) && _e[t].met;
        portEXIT_CRITICAL(&_mux);
        return n;
    }
    int raceMet(uint8_t r) {
        portENTER_CRITICAL(&_mux);
        int n = _raceMet(r);
        portEXIT_CRITICAL(&_mux);
        return n;
    }
    static int raceSize(uint8_t r) {
        int n = 0;
        for (uint8_t t = 0; t < Sig::T_COUNT; t++) n += listed(t) && Sig::raceOf(t) == r;
        return n;
    }
    bool persistent() const { return _sd; }

    // Watcher task: save right away for a first meeting, within 30 s for a
    // new encounter, and only every 10 min when just "last seen" / "best
    // signal" moved (a device parked nearby would otherwise rewrite the
    // file forever). `quiet` skips it while an app owns the SD bus.
    void tick(bool quiet) {
        if (!_sd || quiet || !(_dirty || _minor)) return;
        uint32_t ms = millis();
        if (!_urgent && ms - _lastSave < (_dirty ? 30000UL : 600000UL)) return;
        _lastSave = ms;
        _save();
    }

private:
    static constexpr const char* DIR  = "/MitamaOS/state";
    static constexpr const char* PATH = "/MitamaOS/state/compendium.dat";
    static constexpr const char* TMP  = "/MitamaOS/state/compendium.tmp";
    struct Header { char magic[4]; uint8_t ver; uint8_t n; uint8_t esz; uint8_t pad; };

    Entry       _e[Sig::T_COUNT];
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    static constexpr int NEWS_MAX = 6;
    News        _newsQ[NEWS_MAX];
    int         _newsN = 0;
    volatile bool _dirty = false, _minor = false, _urgent = false;
    bool        _sd = false;
    uint32_t    _lastSave = 0;

    Compendium() = default;

    static uint32_t _now() { time_t t = time(nullptr); return t > 1600000000 ? (uint32_t)t : 0; }

    int _raceMet(uint8_t r) const {
        int n = 0;
        for (uint8_t t = 0; t < Sig::T_COUNT; t++) if (listed(t) && Sig::raceOf(t) == r) n += _e[t].met;
        return n;
    }

    void _save() {
        Entry snap[Sig::T_COUNT];
        portENTER_CRITICAL(&_mux);
        memcpy(snap, _e, sizeof(snap));
        _dirty = false; _minor = false; _urgent = false;
        portEXIT_CRITICAL(&_mux);

        if (!SD.exists(DIR)) SD.mkdir(DIR);
        File f = SD.open(TMP, FILE_WRITE);
        if (!f) { _dirty = true; return; }
        Header h{ {'M', 'T', 'C', '1'}, 1, (uint8_t)Sig::T_COUNT, (uint8_t)sizeof(Entry), 0 };
        bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h)
               && f.write((const uint8_t*)snap, sizeof(snap)) == sizeof(snap);
        f.flush();
        f.close();
        if (!ok) { SD.remove(TMP); _dirty = true; return; }
        // FAT can't rename over a file: drop the old one first. load()
        // falls back to the .tmp if power dies in between.
        SD.remove(PATH);
        SD.rename(TMP, PATH);
    }
};
