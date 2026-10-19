#pragma once
#include <WiFi.h>
#include <Preferences.h>
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
        WiFi.persistent(true);
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
    }

  // Connect using the first non-empty slot (max timeoutMs, 0 = begin() only)
    void connectFirst(uint32_t timeoutMs = 0) {
        if (WiFi.status() == WL_CONNECTED) return;
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(false);
        bool started = false;
        for (int i = 0; i < MAX_WIFI_SLOTS; i++) {
            if (slots[i].ssid != "") {
                if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == slots[i].ssid) return;
                lastBeginMs = millis();
                WiFi.begin(slots[i].ssid.c_str(), slots[i].pass.c_str());
                started = true;
                break;
            }
        }
        if (!started || timeoutMs == 0) return;
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
            delay(50);
        }
    }

    // Background reconnect (launcher / returning from apps)
    // ESP32 Arduino has no WL_CONNECTING (ESP8266 does); WL_IDLE_STATUS = assoc in progress.
    void ensureConnected() {
        if (WiFi.status() == WL_CONNECTED) return;
        wl_status_t st = WiFi.status();
        if (st == WL_IDLE_STATUS) return;  // already connecting
        if (st == WL_DISCONNECTED || st == WL_CONNECTION_LOST
            || st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
            for (int i = 0; i < MAX_WIFI_SLOTS; i++) {
                if (slots[i].ssid != "") {
                    lastBeginMs = millis();
                WiFi.begin(slots[i].ssid.c_str(), slots[i].pass.c_str());
                    return;
                }
            }
        }
    }

    bool isConnecting() const {
        return WiFi.status() == WL_IDLE_STATUS;
    }

    void connectSlot(int idx) {
        if (idx < 0 || idx >= MAX_WIFI_SLOTS) return;
        WiFi.disconnect(true);
        WiFi.mode(WIFI_STA);
        lastBeginMs = millis();
        WiFi.begin(slots[idx].ssid.c_str(), slots[idx].pass.c_str());
    }

    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }

private:
    WiFiManager() = default;
};
