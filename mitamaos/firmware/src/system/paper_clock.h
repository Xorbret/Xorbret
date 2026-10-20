#pragma once
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include "sd_settings.h"

/*
 PaperClock - NTP sync + timezone selection
 Timezones stored in Preferences namespace "timecfg", key "tz_offset" (int, hours -12..+14)
 Format: UTC offset in hours (integer values only)
*/

class PaperClock {
public:
    static int loadOffset() {
        Preferences p;
        p.begin("timecfg", true);
        int off = p.getInt("tz_offset", 0);
        p.end();
        return off;
    }

    static void saveOffset(int offsetHours) {
        Preferences p;
        p.begin("timecfg", false);
        p.putInt("tz_offset", offsetHours);
        p.end();
        SdSettings::changed();
        // Apply immediately
        configTime(offsetHours * 3600L, 0, "pool.ntp.org", "time.nist.gov");
    }

    static void syncTime() {
        if (WiFi.status() != WL_CONNECTED) return;
        int off = loadOffset();
        configTime((long)off * 3600L, 0, "pool.ntp.org", "time.nist.gov");
    }

    static String getTimeString() {
        struct tm t;
        if (!getLocalTime(&t, 80)) return "--:--";
        char buf[12];
        strftime(buf, sizeof(buf), "%H:%M:%S", &t);
        return String(buf);
    }

    static String getShortTime() {
        struct tm t;
        if (!getLocalTime(&t, 80)) return "--:--";
        char buf[8];
        strftime(buf, sizeof(buf), "%H:%M", &t);
        return String(buf);
    }

    static String getDateString() {
        struct tm t;
        if (!getLocalTime(&t, 80)) return "--.--.----";
        char buf[16];
        strftime(buf, sizeof(buf), "%d.%m.%Y", &t);
        return String(buf);
    }

    static bool isSynced() {
        struct tm t;
        return getLocalTime(&t, 200);
    }

    struct TZEntry { int offset; const char* name; };
    static const TZEntry ZONES[];
    static const int     ZONE_COUNT;
};

const PaperClock::TZEntry PaperClock::ZONES[] = {
    {-12, "UTC-12 (Baker Is.)"},
    {-11, "UTC-11 (Samoa)"},
    {-10, "UTC-10 (Hawaii)"},
    { -9, "UTC-9  (Alaska)"},
    { -8, "UTC-8  (Los Angeles)"},
    { -7, "UTC-7  (Denver)"},
    { -6, "UTC-6  (Chicago)"},
    { -5, "UTC-5  (New York)"},
    { -4, "UTC-4  (Halifax)"},
    { -3, "UTC-3  (Buenos Aires)"},
    { -2, "UTC-2  (Mid-Atlantic)"},
    { -1, "UTC-1  (Azores)"},
    {  0, "UTC+0  (London/Lisbon)"},
    {  1, "UTC+1  (Paris/Berlin)"},
    {  2, "UTC+2  (Cairo/Helsinki)"},
    {  3, "UTC+3  (Moscow/Istanbul)"},
    {  4, "UTC+4  (Dubai)"},
    {  5, "UTC+5  (Karachi)"},
    {  6, "UTC+6  (Dhaka)"},
    {  7, "UTC+7  (Bangkok)"},
    {  8, "UTC+8  (Beijing/HK)"},
    {  9, "UTC+9  (Tokyo/Seoul)"},
    { 10, "UTC+10 (Sydney)"},
    { 11, "UTC+11 (Noumea)"},
    { 12, "UTC+12 (Auckland)"},
    { 13, "UTC+13 (Samoa DST)"},
    { 14, "UTC+14 (Line Islands)"},
};
const int PaperClock::ZONE_COUNT = 27;
