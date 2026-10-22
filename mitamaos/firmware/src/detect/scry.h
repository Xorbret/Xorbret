#pragma once
// Scry logger: war driving. While recording, every Wi-Fi access point and
// Bluetooth LE device the detection watcher hears is written once per
// window, with the GPS position, to a WiGLE CSV file on the SD card:
// /MitamaOS/logs/scry/scry-YYYYMMDD-HHMMSS.csv. Only frames the devices
// broadcast themselves are used; nothing is transmitted to gather them.
//
// Threads: onWifi()/onBle() run in the radio tasks and only enqueue;
// drain() runs in the watcher task and owns the file and the seen-filter.
#include <Arduino.h>
#include <SD.h>
#include "scry_parse.h"
#include "../system/gps.h"
#include "../config.h"

class ScryLogger {
public:
    static ScryLogger& get() { static ScryLogger s; return s; }

    static constexpr const char* DIR = "/MitamaOS/logs/scry";

    // SD.mkdir() doesn't create parents, so make each level in turn.
    static void ensureDirs() {
        for (const char* d : { "/MitamaOS", "/MitamaOS/logs", DIR })
            if (!SD.exists(d)) SD.mkdir(d);
    }

    bool recording() const { return _rec; }
    uint32_t wifiCount() const { return _wifiN; }
    uint32_t bleCount() const { return _bleN; }
    const char* fileName() const { return _name; }

    // Starts a new log file. Returns false (with a reason) if it can't.
    bool start(const char** why) {
        if (_rec) return true;
        for (int i = 0; i < 50 && _closeReq; i++) vTaskDelay(pdMS_TO_TICKS(10));
        if (_closeReq) { *why = "Still saving the last log"; return false; }
        if (!_q) _q = xQueueCreate(32, sizeof(Rec));
        if (!_seen.bits) _seen.bits = (uint8_t*)calloc(Scry::Seen::M / 8, 1);
        if (!_q || !_seen.bits) { *why = "Not enough memory"; _release(); return false; }
        if (SD.cardType() == CARD_NONE) { *why = "No SD card found"; _release(); return false; }
        ensureDirs();
        struct tm t;
        time_t now = time(nullptr);
        if (now > 1700000000) {
            gmtime_r(&now, &t);
            snprintf(_name, sizeof(_name), "scry-%04d%02d%02d-%02d%02d%02d.csv",
                     t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
        } else {
            snprintf(_name, sizeof(_name), "scry-%lu.csv", (unsigned long)(esp_random() % 1000000));
        }
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", DIR, _name);
        _f = SD.open(path, FILE_WRITE);
        if (!_f) { *why = "Can't write to the SD card"; _release(); return false; }
        char hdr[400];
        int n = Scry::header(hdr, sizeof(hdr), OS_VERSION);
        _f.write((const uint8_t*)hdr, n);
        _f.flush();
        _wifiN = _bleN = 0;
        _seen.clear();
        _windowMs = _flushMs = millis();
        _rec = true;
        return true;
    }

    // Stops recording; the watcher task writes what's queued and closes
    // the file (it owns the file while recording).
    void stop() {
        if (!_rec) return;
        _rec = false;
        _closeReq = true;
    }

    // ── radio tasks ──────────────────────────────────────────────────────

    void onWifi(const uint8_t* frame, size_t len, uint8_t channel, int8_t rssi) {
        if (!_rec || !_fixOk) return;
        Rec r;
        if (!Scry::parseBeacon(frame, len, channel, r.ap)) return;
        if (_seenProbe(r.ap.bssid, 0)) return;
        r.ble = false; r.rssi = rssi;
        xQueueSend(_q, &r, 0);
    }

    // An access point heard by another radio (the M5MonsterC5's 5 GHz scan).
    void onAp(const Scry::Ap& ap, int8_t rssi) {
        if (!_rec || !_fixOk) return;
        if (_seenProbe(ap.bssid, 0)) return;
        Rec r;
        r.ap = ap; r.ble = false; r.rssi = rssi;
        xQueueSend(_q, &r, 0);
    }

    void onBle(const uint8_t mac[6], const uint8_t* adv, size_t len, int8_t rssi) {
        if (!_rec || !_fixOk) return;
        if (_seenProbe(mac, 1)) return;
        Rec r;
        memcpy(r.ap.bssid, mac, 6);
        int mf;
        Scry::bleInfo(adv, len, r.ap.ssid, sizeof(r.ap.ssid), mf);
        r.mfgr = mf; r.ble = true; r.rssi = rssi;
        xQueueSend(_q, &r, 0);
    }

    // ── watcher task ─────────────────────────────────────────────────────

    void drain() {
        auto& g = Gps::get();
        _fixOk = g.hasFix();
        if (!_q) return;
        Rec r;
        while (xQueueReceive(_q, &r, 0) == pdTRUE) {
            if (!_f || !_fixOk) continue;
            uint8_t key[7]; memcpy(key, r.ap.bssid, 6); key[6] = r.ble;
            if (_seen.testSet(key, 7)) continue;      // already logged this window
            const auto& fx = g.fix();
            char utc[24];
            snprintf(utc, sizeof(utc), "%04u-%02u-%02u %02u:%02u:%02u",
                     fx.year, fx.mon, fx.day, fx.hour, fx.min, fx.sec);
            Scry::Where w{ fx.lat, fx.lon, fx.altM, g.accuracyM(), utc };
            char row[320];   // an SSID of 32 quotes doubles to 66 characters
            int n = r.ble ? Scry::bleRow(row, sizeof(row), r.ap.bssid, r.ap.ssid, r.rssi, r.mfgr, w)
                          : Scry::wifiRow(row, sizeof(row), r.ap, r.rssi, w);
            if (n > 0 && n < (int)sizeof(row)) _f.write((const uint8_t*)row, n);   // a cut-off row would merge with the next
            if (r.ble) _bleN++; else _wifiN++;
        }
        if (_closeReq) {
            vTaskDelay(pdMS_TO_TICKS(60));   // radio callbacks in flight finish
            while (xQueueReceive(_q, &r, 0) == pdTRUE) {}
            _f.close();
            _release();
            _closeReq = false;
            return;
        }
        uint32_t now = millis();
        if (_rec && now - _flushMs > 5000) { _flushMs = now; _f.flush(); }
        // New window: log everything again, so WiGLE gets fresh positions.
        if (_rec && now - _windowMs > WINDOW_MS) { _windowMs = now; _seen.clear(); }
    }

private:
    static constexpr uint32_t WINDOW_MS = 10UL * 60 * 1000;

    struct Rec {
        Scry::Ap ap;        // for BLE: bssid = MAC, ssid = name
        int16_t  mfgr = -1;
        int8_t   rssi = 0;
        bool     ble = false;
    };

    QueueHandle_t  _q = nullptr;
    Scry::Seen     _seen;
    File           _f;
    char           _name[40] = "";
    volatile bool  _rec = false;
    volatile bool  _fixOk = false;
    volatile bool  _closeReq = false;
    uint32_t       _wifiN = 0, _bleN = 0, _windowMs = 0, _flushMs = 0;

    // Read-only peek at the seen-filter from a radio task, so devices
    // already logged this window don't flood the queue. A stale answer
    // only costs a queue slot; drain() makes the real decision.
    bool _seenProbe(const uint8_t mac[6], uint8_t ble) {
        uint8_t* bits = _seen.bits;
        if (!bits) return true;
        uint8_t key[7]; memcpy(key, mac, 6); key[6] = ble;
        uint32_t a = Scry::Seen::_h(key, 7, 0x9e37), b = Scry::Seen::_h(key, 7, 0x85eb), c = Scry::Seen::_h(key, 7, 0xc2b2);
        return (bits[a >> 3] >> (a & 7) & 1) && (bits[b >> 3] >> (b & 7) & 1) && (bits[c >> 3] >> (c & 7) & 1);
    }

    void _release() {
        uint8_t* b = _seen.bits;
        _seen.bits = nullptr;
        free(b);
    }
};
