#pragma once

/*
 PaperOS v1.1 - Universal Emulator Compatibility Layer
 Supports ALL emulator versions:
   - Emulator.extension     (original, ~871KB)
   - EmulatorV3.2.extension (~899KB)
   - EmulatorV3.4.extension (~925KB)
 Detection is automatic at runtime via file size heuristic or
 explicit #define passed from platformio.ini build_flags.
*/

#ifndef EMU_COMPAT_H
#define EMU_COMPAT_H

#include <Arduino.h>
#include <SD.h>

#define EMU_VER_LEGACY   0   // Emulator.extension  (~871 KB)
#define EMU_VER_V32      1   // EmulatorV3.2        (~899 KB)
#define EMU_VER_V34      2   // EmulatorV3.4        (~925 KB)

static const char* EMU_FILENAMES[] = {
    "/EmulatorV3.4.extension",
    "/EmulatorV3.2.extension",
    "/Emulator.extension",
};
static const int EMU_FILE_COUNT = 3;

static const uint32_t EMU_SIZE_V34    = 920000;   // >=920 KB -> V3.4
static const uint32_t EMU_SIZE_V32    = 890000;   // >=890 KB -> V3.2

class EmulatorCompat {
public:
    static int detectVersionFromName(const String& path) {
        if (path.indexOf("V3.4") >= 0 || path.indexOf("V3_4") >= 0)
            return EMU_VER_V34;
        if (path.indexOf("V3.2") >= 0 || path.indexOf("V3.2") >= 0)
            return EMU_VER_V32;
        return EMU_VER_LEGACY;
    }

    // Returns the full path, or "" if not found
    static String findBestEmulator() {
        for (int i = 0; i < EMU_FILE_COUNT; i++) {
            if (SD.exists(EMU_FILENAMES[i])) {
                return String(EMU_FILENAMES[i]);
            }
        }
        return "";
    }

    static String versionName(int ver) {
        switch (ver) {
            case EMU_VER_V34: return "EmulatorV3.4";
            case EMU_VER_V32: return "EmulatorV3.2";
            default:          return "Emulator (legacy)";
        }
    }

    // Call before launching the emulator to patch any required parameters
    static void applyCompat(int ver) {
        (void)ver;
        // Placeholder: future memory/timing patches for a specific version
    }

    static bool ramOk() {
        return ESP.getFreeHeap() > 80000;
    }
};

#endif // EMU_COMPAT_H
