#pragma once
// Crash recorder. When the OS panics (a crash or a watchdog), a small hook
// saves the cause, the code address and a short backtrace in RTC memory,
// which survives the reboot. At the next boot it's written to
// /MitamaOS/logs/crash.txt and shown on screen once, so a photo is enough
// to find the bug (the addresses decode against that build's firmware.elf).
//
// crumb("...") marks what the OS was doing; the latest one is saved too.
#include <Arduino.h>

namespace CrashLog {

static constexpr int BT_MAX = 10;
struct Record {
    uint32_t magic;
    uint32_t cause;        // Xtensa EXCCAUSE (or 0xFF for abort/watchdog)
    uint32_t pc, excvaddr;
    uint32_t bt[BT_MAX];
    uint8_t  btN, core, reasonKind, pad;
    char     crumb[28];
};
static constexpr uint32_t MAGIC = 0x4D435231;   // "MCR1"

Record& rtc();                       // in crash_log.cpp (RTC_NOINIT)
void crumb(const char* what);        // cheap: copies up to 27 chars
void base(const char* what);         // the resting crumb (the open app); done() returns to it
void done();

// After SD is mounted: saves a pending record to the card. Returns true
// if the last reset was a crash (call show() once the display is up).
bool boot();
// Full-screen report; waits for a key (or 60 s).
void show();

} // namespace CrashLog
