#pragma once
// Detection service (outline §5): a background watcher that listens to Wi-Fi
// and Bluetooth LE traffic around the Cardputer and keeps a short list of
// sightings for Proxima and the Compendium.
//
// Receive-only by construction:
//  - Wi-Fi uses promiscuous receive. The radio's only transmissions are
//    the ordinary client traffic of a network you joined in Settings.
//  - Bluetooth uses a passive NimBLE scan (no scan requests), with the
//    peripheral, broadcaster and central roles compiled out (platformio.ini).
//
// Threads: frames arrive in the Wi-Fi driver task and the NimBLE host task;
// both only classify and post a small event to a queue. The watcher task
// (core 0) owns the radio state: it drains the queue into the sighting
// store, hops channels, restarts the BLE scan and suspends everything while
// a memory-hungry app runs.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
#include "signatures.h"
#include "scry.h"
#include "compendium.h"
#include "aether.h"
#include "../system/gps.h"
#include "../os/wifi_manager.h"

class DetectionEngine {
public:
    static DetectionEngine& get() { static DetectionEngine inst; return inst; }

    struct Sighting {
        uint8_t  type = Sig::T_COUNT, grade = 0, via = 0;   // via: 0 Wi-Fi, 1 BLE
        uint8_t  mac[6] = {0};
        int8_t   rssi = 0;
        uint16_t count = 0;
        uint32_t firstMs = 0, lastMs = 0;
        const char* detail = nullptr;
    };

    enum BleState : uint8_t { BLE_OFF, BLE_ON, BLE_LOW_MEM, BLE_PAUSED };

    struct Stats {
        int      threats = 0, watch = 0;      // active in the last 10 minutes
        int      recentN = 0;
        Sighting recent[3];
        bool     wifiOn = false;
        uint8_t  ble = BLE_OFF;
        uint32_t nowMs = 0;
        uint32_t freeHeap = 0;               // bytes, for the low-memory hint
    };

    void begin() {
        if (_q) return;
        _lock = xSemaphoreCreateMutex();
        _q = xQueueCreate(24, sizeof(Event));
        Gps::get().begin();
        xTaskCreatePinnedToCore(_taskTrampoline, "mitama-watch", 6144, this, 1, nullptr, 0);
    }

    // Called from the main loop with the active app's name. Apps that need
    // every byte of RAM or the radio to themselves pause the watcher.
    void appChanged(const char* name) {
        static const char* HEAVY[] = { "MiTalk", "PaperWeb", "Store", "Games", "Lua" };
        bool heavy = false;
        for (auto h : HEAVY) if (strcmp(name, h) == 0) heavy = true;
        _heavyApp = heavy;
        _wantPause = _heavyApp || _hold;
    }

    // Pauses the radios while something needs the RAM for a moment (e.g.
    // a TLS upload). Wait for paused() before relying on the memory.
    void hold(bool on) { _hold = on; _wantPause = _heavyApp || _hold; }
    bool paused() const { return !_wifiOn && _ble != BLE_ON; }

    Stats stats() {
        Stats s;
        s.nowMs = millis();
        s.wifiOn = _wifiOn;
        s.ble = _ble;
        s.freeHeap = ESP.getFreeHeap();
        if (!_lock) return s;
        xSemaphoreTake(_lock, portMAX_DELAY);
        for (int i = 0; i < MAX_SIGHT; i++) {
            const Sighting& x = _store[i];
            if (x.type >= Sig::T_COUNT || s.nowMs - x.lastMs > ACTIVE_MS) continue;
            if (Sig::isThreat(x.type)) s.threats++;
            else if (Sig::raceOf(x.type) != Sig::R_HERALD) s.watch++;
        }
        // three most recent, newest first
        bool used[MAX_SIGHT] = {};
        for (int k = 0; k < 3; k++) {
            int best = -1;
            for (int i = 0; i < MAX_SIGHT; i++) {
                if (used[i] || _store[i].type >= Sig::T_COUNT) continue;
                if (Sig::raceOf(_store[i].type) == Sig::R_HERALD) continue;
                if (best < 0 || _store[i].lastMs > _store[best].lastMs) best = i;
            }
            if (best < 0) break;
            used[best] = true;
            s.recent[s.recentN++] = _store[best];
        }
        xSemaphoreGive(_lock);
        return s;
    }

    // A sighting from another radio (the M5MonsterC5); `via` 2.
    void postExternal(const Sig::Hit& h, const uint8_t mac[6], int8_t rssi) {
        if (_q && h.ok()) _post(h, 2, mac, rssi);
    }

private:
    static constexpr int      MAX_SIGHT = 48;
    static constexpr uint32_t ACTIVE_MS = 10UL * 60 * 1000;
    static constexpr uint32_t HOP_MS = 250, BLE_RESTART_MS = 20000;
    static constexpr int      DEAUTH_LIMIT = 40;              // frames per 2s window
    // NimBLE's controller + host take roughly 40KB; init aborts (reboots)
    // if an allocation fails, so leave a margin for the rest of the OS.
    static constexpr uint32_t BLE_MIN_HEAP = 64000;

    struct Event {
        uint8_t type, grade, via;
        uint8_t mac[6];
        int8_t  rssi;
        const char* detail;
    };

    // Recently reported sources, per radio, so a chatty device doesn't
    // flood the queue. Only touched from that radio's own task.
    struct Recent {
        static constexpr int N = 16;
        uint8_t mac[N][6] = {}; uint8_t type[N] = {}; uint32_t ms[N] = {}; int next = 0;
        bool seen(const uint8_t m[6], uint8_t t, uint32_t now) {
            for (int i = 0; i < N; i++)
                if (ms[i] && type[i] == t && now - ms[i] < 15000 && memcmp(mac[i], m, 6) == 0) return true;
            memcpy(mac[next], m, 6); type[next] = t; ms[next] = now ? now : 1;
            next = (next + 1) % N;
            return false;
        }
    };

    // Evil twin: one network name beaconed open by one radio and locked by
    // another, within five minutes. Meshes and multi-radio routers use the
    // same lock on every radio, so they don't trip it. Beacons only, and
    // only touched from the Wi-Fi callback.
    struct Twins {
        struct E { uint32_t h = 0; uint8_t open[6] = {}, lock[6] = {}; uint32_t openMs = 0, lockMs = 0; };
        static constexpr int N = 24;
        static constexpr uint32_t WINDOW = 5UL * 60 * 1000;
        E e[N];
        // True when `f` completes a twin; `out` gets the open radio's BSSID.
        bool onBeacon(const uint8_t* f, int n, uint32_t now, uint8_t out[6]) {
            if (n < 36 || ((f[0] >> 2) & 3) != 0 || (f[0] >> 4) != 8) return false;
            bool locked = f[34] & 0x10;               // capability: privacy
            uint32_t h = 2166136261u; bool named = false;
            for (int i = 36; i + 2 <= n;) {
                uint8_t id = f[i], l = f[i + 1];
                if (i + 2 + l > n) break;
                if (id == 0) {
                    if (l > 32) return false;
                    for (int j = 0; j < l; j++) { h = (h ^ f[i + 2 + j]) * 16777619u; named |= f[i + 2 + j] != 0; }
                    break;
                }
                i += 2 + l;
            }
            if (!named) return false;                  // hidden networks
            h |= 1;
            int slot = -1, old = 0;
            for (int i = 0; i < N; i++) {
                if (e[i].h == h) { slot = i; break; }
                uint32_t a = max(e[i].openMs, e[i].lockMs), b = max(e[old].openMs, e[old].lockMs);
                if (!e[i].h || (e[old].h && now - a > now - b)) old = i;
            }
            if (slot < 0) { slot = old; e[slot] = E(); e[slot].h = h; }
            E& x = e[slot];
            if (locked) { memcpy(x.lock, f + 16, 6); x.lockMs = now | 1; }
            else        { memcpy(x.open, f + 16, 6); x.openMs = now | 1; }
            if (!x.openMs || !x.lockMs || now - x.openMs > WINDOW || now - x.lockMs > WINDOW) return false;
            if (memcmp(x.open, x.lock, 6) == 0) return false;
            memcpy(out, x.open, 6);
            return true;
        }
    };
    Twins             _twins;

    QueueHandle_t     _q = nullptr;
    SemaphoreHandle_t _lock = nullptr;
    Sighting          _store[MAX_SIGHT];
    volatile bool     _wantPause = false, _heavyApp = false, _hold = false;
    volatile bool     _wifiOn = false;
    volatile uint8_t  _ble = BLE_OFF;
    Recent            _wifiRecent, _bleRecent;
    uint32_t          _deauthFrames = 0;     // atomic: the Wi-Fi task adds, the watcher takes
    uint8_t           _deauthSrc[6] = {0};
    int8_t            _deauthRssi = 0;

    DetectionEngine() = default;

    void _post(const Sig::Hit& h, uint8_t via, const uint8_t mac[6], int8_t rssi) {
        Event e{ h.type, h.grade, via, {0}, rssi, h.detail };
        memcpy(e.mac, mac, 6);
        xQueueSend(_q, &e, 0);
    }

    // ── Wi-Fi ────────────────────────────────────────────────────────────

    static void _promisc(void* buf, wifi_promiscuous_pkt_type_t type) {
        if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;
        auto& self = get();
        auto* p = (const wifi_promiscuous_pkt_t*)buf;
        int len = p->rx_ctrl.sig_len - 4;          // drop the FCS
        if (len < 24) return;
        if (type == WIFI_PKT_MGMT) {
            ScryLogger::get().onWifi(p->payload, len, p->rx_ctrl.channel, p->rx_ctrl.rssi);
            AetherTally::get().onBeacon(p->payload, len, p->rx_ctrl.channel, p->rx_ctrl.rssi);
            uint8_t twin[6];
            uint32_t now = millis();
            if (self._twins.onBeacon(p->payload, len, now, twin) && !self._wifiRecent.seen(twin, Sig::T_EVILTWIN, now)) {
                Sig::Hit h; h.type = Sig::T_EVILTWIN; h.grade = Sig::G_MED; h.detail = "same name, open and locked";
                self._post(h, 0, twin, p->rx_ctrl.rssi);
            }
        }
        Sig::WifiResult r = Sig::matchWifi(p->payload, len);
        if (r.kind == Sig::W_DEAUTH) {
            __atomic_fetch_add(&self._deauthFrames, 1, __ATOMIC_RELAXED);
            memcpy(self._deauthSrc, r.src, 6);
            self._deauthRssi = p->rx_ctrl.rssi;
            return;
        }
        if (!r.hit.ok() || !r.hasSrc) return;
        if (self._wifiRecent.seen(r.src, r.hit.type, millis())) return;
        self._post(r.hit, 0, r.src, p->rx_ctrl.rssi);
    }

    void _wifiKeepAlive() {
        wifi_mode_t m;
        if (_wantPause || WiFiManager::get().joining()
            || esp_wifi_get_mode(&m) != ESP_OK || m == WIFI_MODE_NULL) {
            if (_wifiOn) { esp_wifi_set_promiscuous(false); _wifiOn = false; }
            return;
        }
        bool en = false;
        esp_wifi_get_promiscuous(&en);
        if (!en) {
            wifi_promiscuous_filter_t f = { WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
            esp_wifi_set_promiscuous_filter(&f);
            esp_wifi_set_promiscuous_rx_cb(&_promisc);
            en = esp_wifi_set_promiscuous(true) == ESP_OK;
        }
        _wifiOn = en;
    }

    // Hop only while idle: never when joined (stay on the network's
    // channel), while a connection attempt may be running, or mid-scan.
    void _hop() {
        static uint8_t ch = 1;
        if (!_wifiOn || WiFi.status() == WL_CONNECTED) return;
        if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
        if (millis() - WiFiManager::get().lastBeginMs < 15000 && WiFiManager::get().lastBeginMs) return;
        ch = ch % 13 + 1;
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    }

    void _checkDeauth() {
        uint32_t n = __atomic_exchange_n(&_deauthFrames, 0, __ATOMIC_RELAXED);
        if (n < (uint32_t)DEAUTH_LIMIT) return;
        Sig::Hit h; h.type = Sig::T_DEAUTH; h.grade = Sig::G_HIGH; h.detail = "deauth flood";
        _post(h, 0, _deauthSrc, _deauthRssi);
    }

    // ── Bluetooth LE ─────────────────────────────────────────────────────

    class BleCb : public NimBLEAdvertisedDeviceCallbacks {
        void onResult(NimBLEAdvertisedDevice* d) override {
            auto& self = DetectionEngine::get();
            const uint8_t* le = d->getAddress().getNative();   // LSB first
            uint8_t mac[6];
            for (int i = 0; i < 6; i++) mac[i] = le[5 - i];
            bool pub = d->getAddressType() == BLE_ADDR_PUBLIC;
            ScryLogger::get().onBle(mac, d->getPayload(), d->getPayloadLength(), (int8_t)d->getRSSI());
            Sig::Hit h = Sig::matchBle(d->getPayload(), d->getPayloadLength(), mac, pub);
            if (!h.ok() || h.type == Sig::T_IBEACON) return;   // Herald: off by default
            if (self._bleRecent.seen(mac, h.type, millis())) return;
            self._post(h, 1, mac, (int8_t)d->getRSSI());
        }
    };

    void _bleStart() {
        if (ESP.getFreeHeap() < BLE_MIN_HEAP) { _ble = BLE_LOW_MEM; return; }
        static BleCb cb;
        NimBLEDevice::init("");
        NimBLEScan* s = NimBLEDevice::getScan();
        s->setAdvertisedDeviceCallbacks(&cb, false);   // controller drops repeats until restart
        s->setActiveScan(false);                       // passive: never sends scan requests
        s->setInterval(100);
        s->setWindow(60);                              // leave airtime for Wi-Fi (shared radio)
        s->setMaxResults(0);                           // callbacks only, store nothing
        s->start(0, nullptr, false);
        _ble = BLE_ON;
    }

    void _bleStop() {
        if (_ble != BLE_ON) return;
        NimBLEDevice::getScan()->stop();
        NimBLEDevice::deinit(true);
        _ble = BLE_PAUSED;
    }

    void _bleRestart() {
        if (_ble != BLE_ON) return;
        NimBLEScan* s = NimBLEDevice::getScan();
        s->stop();
        s->clearResults();
        s->start(0, nullptr, false);
    }

    // ── store ────────────────────────────────────────────────────────────

    void _record(const Event& e) {
        uint32_t now = millis();
        xSemaphoreTake(_lock, portMAX_DELAY);
        int slot = -1, empty = -1, oldest = -1;
        for (int i = 0; i < MAX_SIGHT; i++) {
            const Sighting& x = _store[i];
            if (x.type == e.type && memcmp(x.mac, e.mac, 6) == 0) { slot = i; break; }
            if (x.type >= Sig::T_COUNT) { if (empty < 0) empty = i; }
            else if (oldest < 0 || x.lastMs < _store[oldest].lastMs) oldest = i;
        }
        if (slot < 0) slot = empty >= 0 ? empty : oldest;   // full: replace the stalest
        Sighting& x = _store[slot];
        bool fresh = !(x.type == e.type && memcmp(x.mac, e.mac, 6) == 0);
        // An encounter: a device we weren't tracking, or one back after ten
        // quiet minutes. Those are what the Compendium counts.
        bool encounter = fresh || now - x.lastMs > ACTIVE_MS;
        if (fresh) {
            x = Sighting();
            x.type = e.type; memcpy(x.mac, e.mac, 6); x.firstMs = now;
        }
        if (e.grade >= x.grade || fresh) { x.grade = e.grade; x.detail = e.detail; }
        x.via = e.via;
        x.rssi = e.rssi;
        x.lastMs = now;
        if (x.count < 0xFFFF) x.count++;
        xSemaphoreGive(_lock);
        Compendium::get().note(e.type, e.rssi, encounter);
    }

    // ── watcher task ─────────────────────────────────────────────────────

    static void _taskTrampoline(void* arg) { ((DetectionEngine*)arg)->_run(); }

    void _run() {
        // Let boot finish (Wi-Fi join, NTP) before taking the radio.
        vTaskDelay(pdMS_TO_TICKS(3000));
        uint32_t lastHop = 0, lastKeep = 0, lastDeauth = 0, lastBle = millis();
        for (;;) {
            Event e;
            while (xQueueReceive(_q, &e, pdMS_TO_TICKS(20)) == pdTRUE) _record(e);
            Gps::get().poll();
            ScryLogger::get().drain();
            Compendium::get().tick(_wantPause);   // same task as Scry's writes: one SD writer

            uint32_t now = millis();
            if (now - lastKeep >= 1000) {
                lastKeep = now;
                _wifiKeepAlive();
                // BLE shares the antenna: give a Wi-Fi join all of it.
                if (_wantPause || WiFiManager::get().joining()) _bleStop();
                else if (_ble != BLE_ON && now - lastBle >= 5000) { lastBle = now; _bleStart(); }
            }
            if (now - lastDeauth >= 2000) { lastDeauth = now; _checkDeauth(); }
            if (now - lastHop >= HOP_MS) { lastHop = now; _hop(); }
            if (_ble == BLE_ON && now - lastBle >= BLE_RESTART_MS) { lastBle = now; _bleRestart(); }
        }
    }
};
