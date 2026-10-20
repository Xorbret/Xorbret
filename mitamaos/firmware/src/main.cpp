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
#include "apps/proxima/proxima.h"
#include "detect/detection.h"
#include "mitama/brain.h"
#include "apps/scry/scry_app.h"
#include "apps/compendium/compendium_app.h"
#include "apps/molt/molt_app.h"
#include "system/sd_bus.h"
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
#if PAPEROS_ENABLE_PIANO
#include "apps/piano/piano.h"
#endif
#if PAPEROS_ENABLE_PAINT
#include "apps/paint/paint.h"
#endif
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
#include "system/mitama_logo.h"
#include "ui/mitama_ui.h"

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
ProximaApp    proximaApp(canvas);
ScryApp       scryApp(canvas);
CompendiumApp compendiumApp(canvas);
MoltApp       moltApp(canvas);
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
#if PAPEROS_ENABLE_PIANO
Piano         pianoApp(canvas);
#endif
#if PAPEROS_ENABLE_PAINT
Paint         paintApp(canvas);
#endif
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

// Boot splash: the locked stacked lockup (sealed magatama crest over the
// MitamaOS wordmark) on near-black violet, then a segmented loading bar.
// The bar is a fixed animation, not a real progress readout - there's
// nothing meaningful to measure between "apps are registered" and "jump to
// the launcher".
static void showBootScreen() {
    canvas.fillSprite(COL_BG); // near-black violet
    int cx = SCREEN_W / 2;

    // Stacked lockup: sealed magatama crest over the MitamaOS wordmark.
    MitamaLogo::drawCrest(canvas, cx, 35, 68);
    {
        int w = MUI::width(canvas, MUI::display(), "MitamaOS", 1);
        int x = MUI::text(canvas, MUI::display(), "Mitama", cx - w / 2, 66, MUI::WHITE, 1);
        MUI::text(canvas, MUI::display(), "OS", x + 1, 66, MUI::YELLOW, 1);
    }
    MUI::textCentered(canvas, MUI::small(), ("v" + String(OS_VERSION) + "  //  COMP ONLINE").c_str(),
                      cx, 88, MUI::DIM, 1);

    const char* msgs[] = {
        "Starting up...",
        "Mounting storage...",
        "Connecting to network...",
        "Loading applications..."
    };
    for (int i = 0; i < 4; i++) {
        canvas.fillRect(0, 101, SCREEN_W, 14, COL_BG);
        MUI::textCentered(canvas, MUI::small(), msgs[i], cx, 101, MUI::DIM);

        // segmented bar, sunken groove
        int bx = 20, by = 116, bw = SCREEN_W - 40, bh = 8;
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
    SdBus::spi = sd_spi; SdBus::cs = SD_CS;
    // The OS's own folders, so they're there to drop files into (e.g.
    // /MitamaOS/wigle.txt) before any app has written to the card.
    ScryLogger::ensureDirs();
    Compendium::get().load();   // before the watcher starts noting sightings

    // Brightness
    {
        Preferences p; p.begin("display", true);
        int b = p.getInt("bright", 80); p.end();
        M5Cardputer.Display.setBrightness(constrain(b, 10, 100) * 255 / 100);
    }

    // WiFi
    WiFiManager::get().loadFromPrefs();

    // Register apps. The launcher MUST be index 0 (every app's "home"
    // goes there); Proxima is hidden from the grid. The rest register in
    // the order their tiles appear.
    AppManager::get().registerApp(&launcherApp);
    AppManager::get().registerApp(&proximaApp);
    AppManager::get().registerApp(&compendiumApp);
    AppManager::get().registerApp(&scryApp);
    AppManager::get().registerApp(&filesApp);
    AppManager::get().registerApp(&notesApp);
#if PAPEROS_ENABLE_PAINT
    AppManager::get().registerApp(&paintApp);
#endif
#if PAPEROS_ENABLE_MUSIC
    AppManager::get().registerApp(&musicApp);
#endif
    AppManager::get().registerApp(&gamesApp);
    AppManager::get().registerApp(&luaApp);
#if PAPEROS_ENABLE_BROWSER
    AppManager::get().registerApp(&browserApp);
#endif
    AppManager::get().registerApp(&storeApp);
#if PAPEROS_ENABLE_PIANO
    AppManager::get().registerApp(&pianoApp);
#endif
    AppManager::get().registerApp(&calcApp);
    AppManager::get().registerApp(&clockApp);
#if PAPEROS_ENABLE_EDITOR3D
    AppManager::get().registerApp(&editor3dApp);
#endif
#if PAPEROS_ENABLE_MYAI
    AppManager::get().registerApp(&myaiApp);
#endif
    AppManager::get().registerApp(&sysinfoApp);
    AppManager::get().registerApp(&moltApp);
    AppManager::get().registerApp(&settingsApp);

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

    DetectionEngine::get().begin();

    if (bootTo == BOOT_GAMES) {
        AppManager::get().launch("Games");
    } else {
        BiosPost::run(canvas);
        showBootScreen();
        AppManager::get().launch("Proxima");
    }
}

// G0 (the side button) jumps to Proxima from anywhere, and back to the app
// grid from Proxima. Skipped in apps where leaving abruptly would lose work
// or interrupt something running (unsaved drawings/notes, Lua scripts,
// emulator games, downloads, LLM generation) - those exit the normal way.
static void _checkGoButton() {
    static bool prev = false;
    bool down = M5Cardputer.BtnA.isPressed();
    bool edge = down && !prev;
    prev = down;
    if (!edge) return;
    App* a = AppManager::get().getActive();
    const char* n = a ? a->getName() : "";
    static const char* KEEP[] = { "Paint", "Notes", "3D Editor", "Lua", "Games", "Store", "MiTalk" };
    for (auto k : KEEP) if (strcmp(n, k) == 0) return;
    if (strcmp(n, "Proxima") == 0) AppManager::get().requestHome();
    else AppManager::get().requestLaunch("Proxima");
}

// Tell the detection watcher when the foreground app changes, so it can
// step aside for the apps that need the RAM or the radio.
static void _watchActiveApp() {
    static App* last = nullptr;
    App* a = AppManager::get().getActive();
    if (a == last) return;
    last = a;
    DetectionEngine::get().appChanged(a ? a->getName() : "");
}

void loop() {
    _checkGoButton();
    _watchActiveApp();
    // The Mitama keeps thinking whichever app is open, so its reactions
    // are current when you come back to Proxima.
    static uint32_t lastThink = 0;
    if (millis() - lastThink >= 1000) {
        lastThink = millis();
        MitamaBrain::get().update(DetectionEngine::get().stats());
    }
    AppManager::get().tick();
    delay(TICK_MS);
}
