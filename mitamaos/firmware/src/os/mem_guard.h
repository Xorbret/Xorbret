#pragma once
#include <Arduino.h>

// MemGuard - tiny always-on heap tracker.
//
// Two jobs:
//  1. Track the lowest free-heap value ever observed, so SysInfo (or Serial)
//     can show "worst case" RAM instead of just "right now" - a system can
//     look healthy at rest and still have come within a few KB of OOM
//     during a transient spike (e.g. WiFi connect, Lua GC, model load).
//  2. Flag apps that leave the heap smaller than it was before they started,
//     which usually means something wasn't freed in onStop(). A single
//     leak is easy to miss by eye; repeated app switching makes it obvious
//     in the Serial log.
namespace MemGuard {

static uint32_t s_minFreeHeap  = UINT32_MAX;
static char     s_lastWarning[64] = "";

inline void poll() {
    uint32_t f = ESP.getFreeHeap();
    if (f < s_minFreeHeap) s_minFreeHeap = f;
}

inline uint32_t minFreeHeap() { return s_minFreeHeap; }
inline const char* lastWarning() { return s_lastWarning; }

// Tolerance for heap accounting noise (allocator bookkeeping, WiFi/BT
// housekeeping that isn't tied to the app that just exited, etc). Anything
// leaking more than this after onStop() is flagged.
#define MEMGUARD_LEAK_TOLERANCE_BYTES 512

// Call right after an app's onStop() has run. heapBeforeStop is the free
// heap sampled right before onStop() was called.
inline void checkAfterStop(const char* appName, uint32_t heapBeforeStop) {
    uint32_t heapAfterStop = ESP.getFreeHeap();
    poll();
    if (heapAfterStop + MEMGUARD_LEAK_TOLERANCE_BYTES < heapBeforeStop) {
        uint32_t lost = heapBeforeStop - heapAfterStop;
        Serial.printf("[MemGuard] possible leak in '%s': heap dropped %u bytes after onStop()\n",
                       appName, (unsigned)lost);
        snprintf(s_lastWarning, sizeof(s_lastWarning), "%s leaked ~%uB", appName, (unsigned)lost);
    }
}

} // namespace MemGuard
