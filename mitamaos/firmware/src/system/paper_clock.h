#pragma once
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include "sd_settings.h"

/*
 PaperClock - NTP sync + timezone selection
 The zone is stored in Preferences "timecfg": "tz_zone" (index into ZONES,
 a POSIX TZ string with its daylight-saving rule) and, for older builds,
 "tz_offset" (whole hours). A device that only has tz_offset is mapped to
 the first zone with that standard offset.
*/

class PaperClock {
public:
    struct TZEntry { const char* posix; int16_t minutes; const char* name; };
    static const TZEntry ZONES[];
    static const int     ZONE_COUNT;

    static int loadZone() {
        Preferences p;
        p.begin("timecfg", true);
        int z = p.getUChar("tz_zone", 255);
        int off = p.getInt("tz_offset", 0);
        p.end();
        if (z < ZONE_COUNT) return z;
        for (int i = 0; i < ZONE_COUNT; i++) if (ZONES[i].minutes == off * 60) return i;
        return _utc();
    }

    static void saveZone(int z) {
        if (z < 0 || z >= ZONE_COUNT) return;
        Preferences p;
        p.begin("timecfg", false);
        p.putUChar("tz_zone", z);
        p.putInt("tz_offset", ZONES[z].minutes / 60);
        p.end();
        SdSettings::changed();
        applyTz();
        syncTime();
    }

    // Sets the local time rules without touching the clock itself.
    static void applyTz() {
        setenv("TZ", ZONES[loadZone()].posix, 1);
        tzset();
    }

    static void syncTime() {
        if (WiFi.status() != WL_CONNECTED) return;
        configTzTime(ZONES[loadZone()].posix, "pool.ntp.org", "time.nist.gov");
    }

    // These never wait: getLocalTime() stalls for its whole timeout when
    // the clock was never set.
    static bool _now(struct tm& t) {
        time_t n = time(nullptr);
        if (n < 1600000000) return false;
        localtime_r(&n, &t);
        return true;
    }

    static String getTimeString() {
        struct tm t;
        if (!_now(t)) return "--:--";
        char buf[12];
        strftime(buf, sizeof(buf), "%H:%M:%S", &t);
        return String(buf);
    }

    static String getShortTime() {
        struct tm t;
        if (!_now(t)) return "--:--";
        char buf[8];
        strftime(buf, sizeof(buf), "%H:%M", &t);
        return String(buf);
    }

    static String getDateString() {
        struct tm t;
        if (!_now(t)) return "--.--.----";
        char buf[16];
        strftime(buf, sizeof(buf), "%d.%m.%Y", &t);
        return String(buf);
    }

    static bool isSynced() {
        struct tm t;
        return _now(t);
    }

private:
    static int _utc() { for (int i = 0; i < ZONE_COUNT; i++) if (!strcmp(ZONES[i].posix, "UTC0")) return i; return 0; }
};

// Where several zones share a standard offset, the first one is what an
// older build's whole-hour setting maps to.
const PaperClock::TZEntry PaperClock::ZONES[] = {
    { "<-12>12",                        -720, "UTC-12  Baker Island" },
    { "SST11",                          -660, "UTC-11  Pago Pago" },
    { "HST10",                          -600, "UTC-10  Hawaii" },
    { "AKST9AKDT,M3.2.0,M11.1.0",       -540, "UTC-9   Alaska (DST)" },
    { "PST8PDT,M3.2.0,M11.1.0",         -480, "UTC-8   Los Angeles (DST)" },
    { "MST7MDT,M3.2.0,M11.1.0",         -420, "UTC-7   Denver (DST)" },
    { "MST7",                           -420, "UTC-7   Phoenix" },
    { "CST6CDT,M3.2.0,M11.1.0",         -360, "UTC-6   Chicago (DST)" },
    { "CST6",                           -360, "UTC-6   Mexico City" },
    { "EST5EDT,M3.2.0,M11.1.0",         -300, "UTC-5   New York (DST)" },
    { "AST4ADT,M3.2.0,M11.1.0",         -240, "UTC-4   Halifax (DST)" },
    { "AST4",                           -240, "UTC-4   Puerto Rico" },
    { "NST3:30NDT,M3.2.0,M11.1.0",      -210, "UTC-3:30 St. John's (DST)" },
    { "<-03>3",                         -180, "UTC-3   Buenos Aires/Sao Paulo" },
    { "<-02>2",                         -120, "UTC-2   South Georgia" },
    { "<-01>1<+00>,M3.5.0/0,M10.5.0/1",  -60, "UTC-1   Azores (DST)" },
    { "GMT0BST,M3.5.0/1,M10.5.0",          0, "UTC+0   London/Dublin (DST)" },
    { "WET0WEST,M3.5.0/1,M10.5.0",         0, "UTC+0   Lisbon (DST)" },
    { "UTC0",                              0, "UTC+0   UTC" },
    { "CET-1CEST,M3.5.0,M10.5.0/3",       60, "UTC+1   Paris/Berlin (DST)" },
    { "WAT-1",                            60, "UTC+1   Lagos" },
    { "EET-2EEST,M3.5.0/3,M10.5.0/4",    120, "UTC+2   Helsinki/Athens (DST)" },
    { "SAST-2",                          120, "UTC+2   Johannesburg" },
    { "<+03>-3",                         180, "UTC+3   Moscow/Istanbul" },
    { "<+0330>-3:30",                    210, "UTC+3:30 Tehran" },
    { "<+04>-4",                         240, "UTC+4   Dubai" },
    { "<+0430>-4:30",                    270, "UTC+4:30 Kabul" },
    { "PKT-5",                           300, "UTC+5   Karachi" },
    { "IST-5:30",                        330, "UTC+5:30 India" },
    { "<+0545>-5:45",                    345, "UTC+5:45 Kathmandu" },
    { "<+06>-6",                         360, "UTC+6   Dhaka" },
    { "<+07>-7",                         420, "UTC+7   Bangkok/Jakarta" },
    { "CST-8",                           480, "UTC+8   Beijing/Singapore" },
    { "JST-9",                           540, "UTC+9   Tokyo/Seoul" },
    { "ACST-9:30",                       570, "UTC+9:30 Darwin" },
    { "ACST-9:30ACDT,M10.1.0,M4.1.0/3",  570, "UTC+9:30 Adelaide (DST)" },
    { "AEST-10AEDT,M10.1.0,M4.1.0/3",    600, "UTC+10  Sydney (DST)" },
    { "AEST-10",                         600, "UTC+10  Brisbane" },
    { "<+11>-11",                        660, "UTC+11  Noumea" },
    { "NZST-12NZDT,M9.5.0,M4.1.0/3",     720, "UTC+12  Auckland (DST)" },
    { "<+12>-12",                        720, "UTC+12  Fiji" },
    { "<+13>-13",                        780, "UTC+13  Tonga" },
    { "<+14>-14",                        840, "UTC+14  Line Islands" },
};
const int PaperClock::ZONE_COUNT = sizeof(PaperClock::ZONES) / sizeof(PaperClock::ZONES[0]);
