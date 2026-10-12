// SD.h must be included BEFORE M5Cardputer.h/M5GFX.h: LovyanGFX's internal
// DataWrapperT<> file-reader specialization (used by drawPngFile/drawJpgFile/
// drawQoiFile) only compiles into a real, non-abstract implementation if the
// SD library's header guard is already visible when M5GFX's datawrapper.hpp
// gets parsed. With M5Cardputer.h first (as it was before), the abstract
// stub gets picked no matter what type you pass at the call site - that's
// why the fs::FS/fs::SDFS cast in filemanager.h didn't help on its own.
#include <SD.h>
#include <M5Cardputer.h>
#include <SPI.h>
#include <WiFi.h>
#include <Preferences.h>
#include <LittleFS.h>

#include "config.h"
#include "os/disp_lock.h"
#include "os/app_manager.h"
#include "os/os_ui.h"
#include "os/launcher.h"
#include "os/wifi_manager.h"
#if PAPEROS_ENABLE_BROWSER
#include "apps/browser/browser.h"
#endif
#include "apps/files/filemanager.h"
#include "apps/notes/notes.h"
#include "apps/sysinfo/sysinfo.h"
#include "apps/calc/calc.h"
#include "apps/clock/clock_app.h"
#include "apps/games/games.h"
#include "apps/store/store.h"
#if PAPEROS_ENABLE_MUSIC
#include "apps/music/music.h"
#endif
#include "apps/piano/piano.h"
#include "apps/paint/paint.h"
#include "apps/settings/settings_app.h"
#if PAPEROS_ENABLE_EDITOR3D
#include "apps/editor3d/editor3d.h"
#endif
#if PAPEROS_ENABLE_MYAI
#include "apps/myai/myai_app.h"
#endif
#include "apps/lua/lua_app.h"
#include "system/emu_compat.h"
#include "system/paper_clock.h"
#include "system/bios_post.h"
#include "system/paper_logo.h"

#define SD_CS   12
#define SD_MOSI 14
#define SD_MISO 39
#define SD_SCK  40

// M5Cardputer ADV only: GPIO5 must be driven HIGH before mounting the SD
// card. The ADV adds extra I2C sensors that share/contend GPIO5 with the SD
// SPI chip-select path; left floating, the card fails to mount. Driving it
// high once at boot (before SD.begin) is harmless on the original Cardputer,
// so it is unconditional here.
#define SD_PWR_PIN 5

LGFX_Sprite canvas(&M5Cardputer.Display);

Launcher      launcherApp(canvas);
#if PAPEROS_ENABLE_BROWSER
Browser       browserApp(canvas);
#endif
FileManager   filesApp(canvas);
Notes         notesApp(canvas);
Calc          calcApp(canvas);
ClockApp      clockApp(canvas);
Games         gamesApp(canvas);
Store         storeApp(canvas);
#if PAPEROS_ENABLE_MUSIC
MusicPlayer   musicApp(canvas);
#endif
Piano         pianoApp(canvas);
Paint         paintApp(canvas);
SysInfo       sysinfoApp(canvas);
SettingsApp   settingsApp(canvas);
#if PAPEROS_ENABLE_MYAI
MyAiApp       myaiApp(canvas);
#endif
LuaTermApp    luaApp(canvas);
#if PAPEROS_ENABLE_EDITOR3D
Editor3D      editor3dApp(canvas);
#endif

static int _readBootTo() {
    Preferences p; p.begin("app", true);
    int v = p.getInt("BootTo", 0); p.end(); return v;
}
static void _clearBootTo() {
    Preferences p; p.begin("app", false);
    p.putInt("BootTo", 0); p.end();
}

// Windows-95-style startup banner: solid navy field, a little 4-pane
// "flag" logo standing in for the Windows one, and a chunky segmented
// loading bar (Win95's own startup bar was a fixed animation, not a real
// progress readout - this one is too, since there's nothing meaningful to
// measure between "apps are registered" and "jump to the launcher").
static void showBootScreen() {
    canvas.fillSprite(COL_BG); // near-black violet
    int cx = SCREEN_W / 2;

    // Hand-drawn logo mark, floating directly on the navy background.
    int lw = PaperLogo::lockupW(2);
    int lx = cx - lw / 2;
    int ly = 10;
    PaperLogo::drawLockup(canvas, lx, ly, 2);
    int logoBottom = ly + PaperLogo::markH(2);

    canvas.setTextSize(1);
    canvas.setTextColor(COL_DIM, COL_BG);
    canvas.drawCenterString("v" + String(OS_VERSION) + "  by " + String(OS_AUTHOR), cx, logoBottom + 6);

    const char* msgs[] = {
        "Starting up...",
        "Mounting storage...",
        "Connecting to network...",
        "Loading applications..."
    };
    for (int i = 0; i < 4; i++) {
        canvas.fillRect(0, 96, SCREEN_W, 12, COL_BG);
        canvas.setTextColor(COL_DIM, COL_BG);
        canvas.drawCenterString(msgs[i], cx, 96);

        // segmented bar, sunken groove
        int bx = 20, by = 112, bw = SCREEN_W - 40, bh = 8;
        canvas.fillRect(bx, by, bw, bh, COL_BG);
        OsUI::drawBevel(canvas, bx, by, bw, bh, false);
        int filled = map(i + 1, 0, 4, 0, bw - 2);
        for (int seg = 0; seg < filled; seg += 5) {
            int w = min(4, filled - seg);
            canvas.fillRect(bx + 1 + seg, by + 1, w, bh - 2, COL_WARN); // cyber yellow
        }
        { DispLock _lk; canvas.pushSprite(0, 0); }
        delay(280);
    }
    delay(150);
}

void setup() {
    Serial.begin(115200);
    dispLockInit(); // must exist before any task (e.g. Lua REPL) can touch the display

    M5Cardputer.begin();
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Power.begin();

    canvas.setColorDepth(8);
    canvas.createSprite(SCREEN_W, SCREEN_H);
    // Uses LGFX's default built-in font. Every screen's layout is sized
    // for it; setting a different font here (e.g. a CJK font) without
    // resetting it back would make text overflow icons and boxes
    // throughout the whole OS.

    int bootTo = _readBootTo();
    _clearBootTo();

    // Mount LittleFS BEFORE SD (matches MyAi_Ard exactly)
    LittleFS.begin(true);

    // ADV SD power/CS enable - drive GPIO5 HIGH before touching the SD bus.
    pinMode(SD_PWR_PIN, OUTPUT);
    digitalWrite(SD_PWR_PIN, HIGH);
    delay(10);

    // SD
    SPIClass* sd_spi = new SPIClass(HSPI);
    sd_spi->begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    // 20MHz instead of the library's 4MHz default. Matters more now that
    // myAI re-reads the whole model.bin from here every token (moved off
    // internal flash so ROM loading in Games stopped erasing it - see
    // paperos_8mb.csv). If a card/wiring combo turns out unstable at this
    // speed, drop it back down; 4000000 is always the safe fallback.
    SD.begin(SD_CS, *sd_spi, 20000000);

    // Brightness
    {
        Preferences p; p.begin("display", true);
        int b = p.getInt("bright", 80); p.end();
        M5Cardputer.Display.setBrightness(constrain(b, 10, 100) * 255 / 100);
    }

    // WiFi
    WiFiManager::get().loadFromPrefs();

    // Register apps (launcher is FIRST - index 0)
    AppManager::get().registerApp(&launcherApp);
#if PAPEROS_ENABLE_BROWSER
    AppManager::get().registerApp(&browserApp);
#endif
    AppManager::get().registerApp(&gamesApp);
    AppManager::get().registerApp(&storeApp);
#if PAPEROS_ENABLE_MUSIC
    AppManager::get().registerApp(&musicApp);
#endif
    AppManager::get().registerApp(&pianoApp);
    AppManager::get().registerApp(&paintApp);
#if PAPEROS_ENABLE_EDITOR3D
    AppManager::get().registerApp(&editor3dApp);
#endif
    AppManager::get().registerApp(&filesApp);
    AppManager::get().registerApp(&notesApp);
    AppManager::get().registerApp(&calcApp);
    AppManager::get().registerApp(&clockApp);
    AppManager::get().registerApp(&sysinfoApp);
    AppManager::get().registerApp(&settingsApp);
#if PAPEROS_ENABLE_MYAI
    AppManager::get().registerApp(&myaiApp);
#endif
    AppManager::get().registerApp(&luaApp);

    // WiFi connect + NTP sync used to live only in the "normal boot" branch
    // below, so a stuck BootTo=BOOT_GAMES flag (e.g. after a game/emulator
    // crash before it could clear the flag) silently skipped WiFi and the
    // clock forever, since the launcher - which normally drives background
    // reconnect - never even started. Always attempt both, regardless of
    // where we're booting to.
    WiFiManager::get().connectFirst(2500);
    if (WiFiManager::get().isConnected()) {
        PaperClock::syncTime();
    }

    if (bootTo == BOOT_GAMES) {
        AppManager::get().launch("Games");
    } else {
        BiosPost::run(canvas);
        showBootScreen();
        AppManager::get().launchByIndex(0);
    }
}

void loop() {
    AppManager::get().tick();
    delay(TICK_MS);
}
