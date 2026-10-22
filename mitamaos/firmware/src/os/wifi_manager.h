#pragma once
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "../system/sd_settings.h"
#include "../config.h"

//  WiFiManager - stores/loads credentials, connects
//  Singleton: WiFiManager::get()
class WiFiManager {
public:
    static WiFiManager& get() {
        static WiFiManager inst;
        return inst;
    }

    struct Slot { String ssid, pass; };

    // millis() of the last WiFi.begin(); the detection watcher holds its
    // channel hopping off for a while after it so joins aren't disturbed.
    volatile uint32_t lastBeginMs = 0;
    Slot slots[MAX_WIFI_SLOTS];

    void loadFromPrefs() {
        Preferences p;
        p.begin("wifi-config", true);
        for (int i = 0; i < MAX_WIFI_SLOTS; i++) {
            slots[i].ssid = p.getString(("s" + String(i)).c_str(), "");
            slots[i].pass = p.getString(("p" + String(i)).c_str(), "");
        }
        p.end();
        WiFi.mode(WIFI_STA);
        // ESP-IDF's own auto-reconnect retries very aggressively in the
        // background while disconnected (radio churn), which was the most
        // likely cause of the "cursor moves slightly slower with WiFi off"
        // symptom - it steals scheduler time from everything else. We
        // drive our own slower, throttled retry via ensureConnected()
        // instead (see launcher.h), so this stays off.
        WiFi.setAutoReconnect(false);
        // Saved networks live in "wifi-config"; persistent mode would also
        // write each join attempt to flash, every 15 s while out of range.
        WiFi.persistent(false);
        // Remember why the last join failed, for the Settings message.
        static bool hooked = false;
        if (!hooked) {
            hooked = true;
            WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
                WiFiManager::get().lastReason = info.wifi_sta_disconnected.reason;
            }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        }
    }

    volatile uint8_t lastReason = 0;      // wifi_err_reason_t of the last drop (0 = none)

    // True while a join we started may still be running: the detection
    // watcher turns its promiscuous listening off then, as it can upset
    // the handshake (WPA3 especially).
    bool joining() const {
        return lastBeginMs && millis() - lastBeginMs < 15000 && WiFi.status() != WL_CONNECTED;
    }

    void saveSlot(int idx, const String& ssid, const String& pass) {
        if (idx < 0 || idx >= MAX_WIFI_SLOTS) return;
        slots[idx].ssid = ssid;
        slots[idx].pass = pass;
        Preferences p;
        p.begin("wifi-config", false);
        p.putString(("s" + String(idx)).c_str(), ssid);
        p.putString(("p" + String(idx)).c_str(), pass);
        p.end();
        SdSettings::changed();
    }

    // Connect to the slot that worked last (else the first saved one).
    // timeoutMs: how long to wait for it (0 = begin() only). If it isn't
    // around, ensureConnected() tries the other slots in turn.
    void connectFirst(uint32_t timeoutMs = 0) {
        if (WiFi.status() == WL_CONNECTED) return;
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(false);
        _tryIdx = _goodSlot();
        if (!_beginNext()) return;
        if (timeoutMs == 0) return;
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
            delay(50);
        }
        noteResult();
    }

    // Background reconnect (launcher / returning from apps). Each call
    // after a failed attempt moves on to the next saved slot, so a phone
    // hotspot in slot 2 gets its turn when the home network in slot 1
    // isn't in range.
    // ESP32 Arduino has no WL_CONNECTING (ESP8266 does); WL_IDLE_STATUS = assoc in progress.
    void ensureConnected() {
        if (noteResult()) return;
        // ESP32 reports a join in progress as plain "disconnected", so
        // without this a retry would cut off a slow handshake (phone
        // hotspots, WPA3) every time and it would never finish.
        if (joining()) return;
        // (IDLE can also mean "associated, DHCP stalled", so past the join
        // window it's retried like any other failure.)
        wl_status_t st = WiFi.status();
        if (st == WL_IDLE_STATUS || st == WL_DISCONNECTED || st == WL_CONNECTION_LOST
            || st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
            _beginNext();
        }
    }

    // True when connected; remembers which slot did it, so that one is
    // tried first next time.
    bool noteResult() {
        if (WiFi.status() != WL_CONNECTED) return false;
        String ss = WiFi.SSID();
        for (int i = 0; i < MAX_WIFI_SLOTS; i++)
            if (slots[i].ssid.length() && slots[i].ssid == ss && i != _good) {
                _good = i;
                Preferences p; p.begin("wifi-config", false); p.putUChar("good", i); p.end();
            }
        return true;
    }

    bool isConnecting() const {
        return WiFi.status() == WL_IDLE_STATUS;
    }

    void connectSlot(int idx) {
        if (idx < 0 || idx >= MAX_WIFI_SLOTS) return;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_STA);
        lastReason = 0;
        _tryIdx = idx;
        _beginNext();
    }

    // Waits up to timeoutMs for a join started by connectSlot(); returns a
    // short reason if it didn't work (nullptr = connected).
    const char* waitResult(uint32_t timeoutMs, void (*progress)(int secsLeft) = nullptr) {
        uint32_t t0 = millis();
        int shown = -1;
        while (millis() - t0 < timeoutMs) {
            if (WiFi.status() == WL_CONNECTED) { noteResult(); return nullptr; }
            int left = (timeoutMs - (millis() - t0) + 999) / 1000;
            if (progress && left != shown) { shown = left; progress(left); }
            delay(100);
        }
        static char msg[40];
        uint8_t r = lastReason;
        const char* what =
            r == 201 ? "Not found (2.4 GHz only)" :                          // NO_AP_FOUND
            (r == 15 || r == 204 || r == 202 || r == 2) ? "Rejected: password?" :   // handshake / auth
            r == 205 ? "Connection failed" :
            r ? "Failed" : "No answer";
        if (r) snprintf(msg, sizeof(msg), "%s (code %u)", what, r);
        else snprintf(msg, sizeof(msg), "%s", what);
        return msg;
    }

    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }

private:
    WiFiManager() = default;

    int _tryIdx = 0;          // next slot ensureConnected() will try
    int _good = -1;           // slot that connected last (-1 = not loaded)

    int _goodSlot() {
        if (_good < 0) {
            Preferences p; p.begin("wifi-config", true);
            _good = p.getUChar("good", 0); p.end();
            if (_good >= MAX_WIFI_SLOTS) _good = 0;
        }
        return _good;
    }

    // Starts joining the first saved slot from _tryIdx on, then moves
    // _tryIdx past it. False if no slot is saved.
    bool _beginNext() {
        for (int k = 0; k < MAX_WIFI_SLOTS; k++) {
            int i = (_tryIdx + k) % MAX_WIFI_SLOTS;
            if (slots[i].ssid == "") continue;
            esp_wifi_set_promiscuous(false);   // detection listening off for the join (see joining())
            lastBeginMs = millis();
            WiFi.begin(slots[i].ssid.c_str(), slots[i].pass.c_str());
            _tryIdx = (i + 1) % MAX_WIFI_SLOTS;
            return true;
        }
        return false;
    }
};
