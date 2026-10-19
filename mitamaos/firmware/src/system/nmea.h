#pragma once
// Minimal NMEA 0183 parser for the GPS on the Cap LoRa-1262 (ATGM336H).
// Pure C++ (no Arduino), so it can be tested on a PC. Reads RMC (time,
// date, position, validity) and GGA (fix quality, satellites, HDOP,
// altitude) from any talker (GP, GN, BD...). Checksums are required.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace Nmea {

struct Fix {
    bool     valid = false;       // RMC status A and GGA quality > 0
    double   lat = 0, lon = 0;    // degrees, south/west negative
    float    altM = 0, hdop = 99;
    uint8_t  sats = 0;            // satellites used in the fix (GGA)
    uint8_t  inView = 0;          // satellites visible, all systems (GSV)
    // UTC from RMC; year 0 until a date has been seen
    uint16_t year = 0; uint8_t mon = 0, day = 0, hour = 0, min = 0, sec = 0;
    uint32_t sentences = 0;       // good sentences seen, any type
};

class Parser {
public:
    // Feed one byte; returns true when a sentence completed and parsed.
    bool feed(char c, Fix& fix) {
        if (c == '$') { _n = 0; _on = true; return false; }
        if (!_on) return false;
        if (c == '\r' || c == '\n') {
            _on = false;
            _buf[_n] = 0;
            return _sentence(fix);
        }
        if (_n < sizeof(_buf) - 1) _buf[_n++] = c; else _on = false;
        return false;
    }

private:
    char   _buf[100];
    size_t _n = 0;
    bool   _on = false;
    bool   _rmcValid = false;
    uint8_t _ggaQual = 0;
    uint8_t _view[6] = {0};       // in-view count per talker: GP GL GA BD/GB GQ other

    static int _talker(const char* f0) {
        if (f0[0] == 'G' && f0[1] == 'P') return 0;
        if (f0[0] == 'G' && f0[1] == 'L') return 1;
        if (f0[0] == 'G' && f0[1] == 'A') return 2;
        if ((f0[0] == 'B' && f0[1] == 'D') || (f0[0] == 'G' && f0[1] == 'B')) return 3;
        if (f0[0] == 'G' && f0[1] == 'Q') return 4;
        return 5;
    }

    static int _hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }

    // ddmm.mmmm + hemisphere -> signed degrees
    static double _coord(const char* v, const char* h) {
        if (!*v) return 0;
        double raw = atof(v);
        int deg = (int)(raw / 100);
        double d = deg + (raw - deg * 100) / 60.0;
        return (*h == 'S' || *h == 'W') ? -d : d;
    }

    bool _sentence(Fix& fix) {
        char* star = strchr(_buf, '*');
        if (!star || star[1] == 0 || star[2] == 0) return false;
        uint8_t sum = 0;
        for (char* p = _buf; p < star; p++) sum ^= (uint8_t)*p;
        int h1 = _hex(star[1]), h2 = _hex(star[2]);
        if (h1 < 0 || h2 < 0 || sum != (uint8_t)(h1 * 16 + h2)) return false;
        *star = 0;

        // split fields in place (empty fields kept)
        const char* f[20]; int nf = 0;
        char* p = _buf;
        f[nf++] = p;
        while (*p && nf < 20) { if (*p == ',') { *p = 0; f[nf++] = p + 1; } p++; }
        if (strlen(f[0]) < 5) return false;
        const char* type = f[0] + 2;   // skip talker

        if (strcmp(type, "RMC") == 0 && nf >= 10) {
            // 1 time hhmmss.ss, 2 status, 3/4 lat, 5/6 lon, 9 date ddmmyy
            if (strlen(f[1]) >= 6) {
                fix.hour = (f[1][0] - '0') * 10 + (f[1][1] - '0');
                fix.min  = (f[1][2] - '0') * 10 + (f[1][3] - '0');
                fix.sec  = (f[1][4] - '0') * 10 + (f[1][5] - '0');
            }
            if (strlen(f[9]) == 6) {
                fix.day  = (f[9][0] - '0') * 10 + (f[9][1] - '0');
                fix.mon  = (f[9][2] - '0') * 10 + (f[9][3] - '0');
                fix.year = 2000 + (f[9][4] - '0') * 10 + (f[9][5] - '0');
            }
            _rmcValid = f[2][0] == 'A';
            if (_rmcValid) { fix.lat = _coord(f[3], f[4]); fix.lon = _coord(f[5], f[6]); }
        } else if (strcmp(type, "GGA") == 0 && nf >= 10) {
            // 6 quality, 7 sats, 8 hdop, 9 altitude
            _ggaQual = (uint8_t)atoi(f[6]);
            fix.sats = (uint8_t)atoi(f[7]);
            fix.hdop = *f[8] ? (float)atof(f[8]) : 99;
            fix.altM = (float)atof(f[9]);
            if (_ggaQual > 0 && *f[2]) { fix.lat = _coord(f[2], f[3]); fix.lon = _coord(f[4], f[5]); }
        } else if (strcmp(type, "GSV") == 0 && nf >= 4) {
            // 3 = satellites in view for this system
            _view[_talker(f[0])] = (uint8_t)atoi(f[3]);
            int sum = 0;
            for (uint8_t v : _view) sum += v;
            fix.inView = sum > 255 ? 255 : (uint8_t)sum;
            fix.sentences++;
            return true;
        } else {
            fix.sentences++;
            return true;
        }
        fix.valid = _rmcValid && _ggaQual > 0;
        fix.sentences++;
        return true;
    }
};

} // namespace Nmea
