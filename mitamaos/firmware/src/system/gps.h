#pragma once
// GPS service for the Cap LoRa-1262 (ATGM336H GNSS on the EXT header).
// Pins per M5Stack's Cap LoRa-1262 docs for the Cardputer ADV: the GPS's
// TX arrives on G15 and its RX is G13; 115200 8N1 by default. Without the
// cap nothing arrives and the service just reports "no GPS".
//
// Polled from the detection watcher task (core 0). Also sets the system
// clock from GPS time, so the clock works away from Wi-Fi.
#include <Arduino.h>
#include <sys/time.h>
#include "nmea.h"
#include "paper_clock.h"
#include "../ui/mitama_ui.h"
#include "tsukumo.h"

class Gps {
public:
    static Gps& get() { static Gps g; return g; }

    static constexpr int RX_PIN = 15, TX_PIN = 13;

    void begin() {
        Serial1.setRxBufferSize(1024);
        // Listen only. G13 stays untouched until a GPS has actually spoken on
        // G15, so a different cap on the EXT header never sees it driven.
        // A GPS Unit on the Grove port (saved in Tsukumo) replaces the cap's.
        _rx = Tsukumo::Scanner::gpsPin();
        if (_rx < 0) _rx = RX_PIN;
        Serial1.begin(_baud(), SERIAL_8N1, _rx, -1);
        _sinceSwitch = millis();
    }

    // Reads whatever arrived; cheap when nothing is connected.
    void poll() {
        int n = 0;
        while (Serial1.available() && n++ < 512) {
            if (_parser.feed((char)Serial1.read(), _fix)) _lastMs = millis();
        }
        uint32_t now = millis();
        // Once the module talks: ask it to use GPS + BeiDou + GLONASS
        // (CASIC PCAS04; more satellites, faster fix than its default).
        if (_lastMs && !_configured) {
            _configured = true;
            _searchMs = now;
            if (_rx == RX_PIN) {     // the cap's GPS; a Grove GPS is only listened to
                Serial1.setPins(RX_PIN, TX_PIN);
                Serial1.print("$PCAS04,7*1E\r\n");
            }
        }
        // No sentences yet: alternate between 115200 and the module's other
        // common rate (9600) until one of them talks.
        if (_fix.sentences == 0 && now - _sinceSwitch > 4000) {
            _baudIdx ^= 1;
            Serial1.updateBaudRate(_baud());
            _sinceSwitch = now;
        }
        if (_fix.valid && _fix.year >= 2024 && (!_clockSetMs || now - _clockSetMs > 600000UL)) _setClock();
        MUI::gpsState = !present() ? 0 : hasFix() ? 2 : 1;
    }

    // Milliseconds spent searching since the GPS was first heard.
    uint32_t searchingMs() const { return _searchMs ? millis() - _searchMs : 0; }

    bool present() const { return _lastMs && millis() - _lastMs < 5000; }
    bool hasFix()  const { return present() && _fix.valid; }
    const Nmea::Fix& fix() const { return _fix; }

    // Rough horizontal accuracy in metres from HDOP (WiGLE's AccuracyMeters).
    float accuracyM() const { return _fix.hdop * 4.0f; }

private:
    Nmea::Parser _parser;
    Nmea::Fix    _fix;
    uint32_t     _lastMs = 0, _sinceSwitch = 0, _clockSetMs = 0;
    uint8_t      _baudIdx = 0;
    int          _rx = RX_PIN;
    bool         _configured = false;
    uint32_t     _searchMs = 0;

    uint32_t _baud() const { return _baudIdx ? 9600 : 115200; }

    // Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
    static int32_t _days(int y, unsigned m, unsigned d) {
        y -= m <= 2;
        const int32_t era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = (unsigned)(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + (int32_t)doe - 719468;
    }

    void _setClock() {
        _clockSetMs = millis();
        time_t t = (time_t)_days(_fix.year, _fix.mon, _fix.day) * 86400
                 + _fix.hour * 3600 + _fix.min * 60 + _fix.sec;
        timeval tv = { t, 0 };
        settimeofday(&tv, nullptr);
        // Apply the saved timezone (NTP's configTime does this when online).
        char tz[16];
        snprintf(tz, sizeof(tz), "UTC%+d", -PaperClock::loadOffset());   // POSIX sign is inverted
        setenv("TZ", tz, 1);
        tzset();
    }
};
