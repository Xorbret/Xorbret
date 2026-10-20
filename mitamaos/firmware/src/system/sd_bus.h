#pragma once
// SD card bus speed. The OS runs the card at 20 MHz; MiTalk, which reads
// the whole 4.7 MB model for every character, asks for 40 MHz while it's
// open and checks the card still reads correctly, else drops back.
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

namespace SdBus {

inline SPIClass* spi = nullptr;   // set in setup()
inline uint8_t   cs = 0;
inline uint32_t  hz = 20000000;
static constexpr uint32_t NORMAL = 20000000;

// Remounts the card at `want` Hz. Callers must have no files open.
inline bool remount(uint32_t want) {
    if (!spi || hz == want) return hz == want;
    SD.end();
    if (SD.begin(cs, *spi, want)) { hz = want; return true; }
    SD.begin(cs, *spi, NORMAL);
    hz = NORMAL;
    return false;
}

// Tries a faster clock; keeps it only if `path` still reads back the
// same first bytes as at the normal speed.
inline bool tryFast(const char* path, uint32_t want = 40000000) {
    uint8_t a[64] = {}, b[64] = {};
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    size_t n = f.read(a, sizeof(a)); f.close();
    if (!remount(want)) return false;
    f = SD.open(path, FILE_READ);
    size_t m = f ? f.read(b, sizeof(b)) : 0;
    if (f) f.close();
    if (m == n && memcmp(a, b, n) == 0) return true;
    remount(NORMAL);
    return false;
}

} // namespace SdBus
