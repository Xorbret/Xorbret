#pragma once
#include <M5Cardputer.h>
#include <SD.h>
#include <Update.h>
#include <Preferences.h>
#include "esp_ota_ops.h"
#include "../../system/ota_tools.h"
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../config.h"

//  Games - game launcher with OTA loading
//
//  Supported formats:
//   .ard  - Arduboy games (flashed directly to ota_1)
//   .nes  - NES ROM   |
//   .gb   - GameBoy   |- need the .extension emulator in /MitamaOS/games/ on SD
//   .gbc  - GBC ROM   |
//
//  Emulator lookup order:
//   1. PaperEMU.extension  - our NES+GBC emulator
//   2. EmulatorV3.4.extension - original GameStation
//
//  Flow:
//  1. .ard -> Update.begin(ota_1) -> write binary -> esp_ota_set_boot -> restart
//  2. .nes/.gb/.gbc -> if emulator already in ota_1: just change ROM path -> restart
//                     otherwise: flash the emulator first -> restart
//  3. On PaperOS boot, check Preferences["BootTo"]==BOOT_GAMES -> auto-launch Games
//
//  Before OTA: save "BootTo"=BOOT_GAMES so we return here after the game
//  ROM path: Preferences namespace "app", key "RomToLoad" - read by the emulator
//
//  Keys: ;/. - navigate, ENT - launch, ` - home, D - delete

#define BOOT_GAMES  7   // same value as in AdvanceOS - compatible with the emulator

class Games : public App {
public:
    Games(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Games"; }
    const char* getIcon() const override { return "G"; }
    const char* getDesc() const override { return "ARD / NES / GB / GBC"; }

    void onStart() override {
        _sel   = 0;
        _count = 0;
        _camY  = 0;
        _scanGames();
        _draw();
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();
        auto ks = M5Cardputer.Keyboard.keysState();

        bool changed = false;

        if (M5Cardputer.Keyboard.isKeyPressed(';')) {
            if (_sel > 0) { _sel--; changed = true; } delay(KEY_REPEAT_MS);
        }
        if (M5Cardputer.Keyboard.isKeyPressed('.')) {
            if (_sel < _count - 1) { _sel++; changed = true; } delay(KEY_REPEAT_MS);
        }

        if (ks.enter && _count > 0) {
            OsUI::waitRelease();
            _launchSelected();
            return;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('d')) {
            if (_count > 0) { OsUI::waitRelease(); _deleteSelected(); }
            return;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            OsUI::waitRelease();
            AppManager::get().launchByIndex(0);
            return;
        }

        if (changed) _draw();
    }

private:
    LGFX_Sprite& _canvas;

    struct GameEntry {
        char name[40];   // file name only
        char ext[5];     // "ard" / "nes" / "gb" / "gbc"
    };

    static const int MAX_GAMES = 64;
    GameEntry _games[MAX_GAMES];
    int  _count = 0;
    int  _sel   = 0;
    int  _camY  = 0;

    void _scanGames() {
        _count = 0;
        SD.mkdir("/MitamaOS");
        SD.mkdir("/MitamaOS/games");
        File dir = SD.open("/MitamaOS/games");
        if (!dir || !dir.isDirectory()) return;

        File f = dir.openNextFile();
        while (f && _count < MAX_GAMES) {
            if (!f.isDirectory()) {
                String n = String(f.name());
                int sl = n.lastIndexOf('/');
                String base = (sl >= 0) ? n.substring(sl + 1) : n;
                String low  = base;
                low.toLowerCase();

                const char* ext = nullptr;
                if      (low.endsWith(".ard")) ext = "ard";
                else if (low.endsWith(".nes")) ext = "nes";
                else if (low.endsWith(".gb"))  ext = "gb";
                else if (low.endsWith(".gbc")) ext = "gbc";

                if (ext) {
                    strncpy(_games[_count].name, base.c_str(), 39);
                    _games[_count].name[39] = '\0';
                    strncpy(_games[_count].ext, ext, 4);
                    _games[_count].ext[4] = '\0';
                    _count++;
                }
            }
            f.close();
            f = dir.openNextFile();
        }
        dir.close();
    }

    const char* _typeLabel(int i) {
        const char* e = _games[i].ext;
        if (strcmp(e, "ard") == 0) return "ARD";
        if (strcmp(e, "nes") == 0) return "NES";
        if (strcmp(e, "gb")  == 0) return "GB ";
        if (strcmp(e, "gbc") == 0) return "GBC";
        return "BIN";
    }

    uint16_t _typeColor(int i) {
        const char* e = _games[i].ext;
        if (strcmp(e, "ard") == 0) return 0x07FF;  // cyan - Arduboy
        if (strcmp(e, "nes") == 0) return 0xF800;  // red - NES
        if (strcmp(e, "gb")  == 0) return 0x07E0;  // green - GB
        if (strcmp(e, "gbc") == 0) return 0xFFE0;  // yellow - GBC
        return COL_DIM;
    }

    void _launchSelected() {
        const char* ext = _games[_sel].ext;
        String path = "/MitamaOS/games/" + String(_games[_sel].name);

        if (strcmp(ext, "ard") == 0) {
            _flashArduboy(path);
        } else {
            _flashNES_GB(path);
        }
    }

    void _flashArduboy(const String& path) {
        File bin = SD.open(path.c_str(), FILE_READ);
        if (!bin) { _showMsg("Cannot open file", COL_ERR, 1500); return; }
        size_t sz = bin.size();
        if (sz == 0) { bin.close(); _showMsg("File empty", COL_ERR, 1500); return; }

        _showFlashScreen("Arduboy", _games[_sel].name, sz);

        const esp_partition_t* cur  = esp_ota_get_running_partition();
        const esp_partition_t* ota1 = esp_ota_get_next_update_partition(cur);

        if (!Update.begin(sz, U_FLASH, ota1->address)) {
            bin.close(); _showMsg("Update.begin failed", COL_ERR, 1500); return;
        }

        _streamWithProgress(bin, sz);
        bin.close();

        if (!Update.end(false)) { _showMsg("Flash error", COL_ERR, 1500); return; }

        esp_ota_set_boot_partition(ota1);

        // The OTA slot now holds a game binary, not the emulator - if the
        // emulator was flashed there before, it's gone now.
        Preferences p;
        p.begin("app", false);
        p.putString("OtaApp", "ard");
        p.end();

        // Save: return to Games when PaperOS starts
        _saveBootTo(BOOT_GAMES);

        _showMsg("OK!  Rebooting...", COL_OK, 600);
        ESP.restart();
    }

    void _flashNES_GB(const String& romPath) {
        // Save the ROM path (the emulator reads it on boot)
        _saveRomPath(romPath);

        // Check whether the emulator is already in ota_1
        if (_emulatorAlreadyInstalled()) {
            // Just switch boot to ota_1 with the new ROM
            const esp_partition_t* cur  = esp_ota_get_running_partition();
            const esp_partition_t* ota1 = esp_ota_get_next_update_partition(cur);
            esp_ota_set_boot_partition(ota1);
            _saveBootTo(BOOT_GAMES);
            _showMsg("Loading ROM...", COL_OK, 400);
            ESP.restart();
        } else {
            // Flash the emulator
            // Look for the emulator: PaperEMU -> legacy MiniEMU -> GameStation
            String emulPath = "";
            String emulName = "";
            const char* candidates[] = {
                "/MitamaOS/games/PaperEMU.extension",
                "/MitamaOS/games/MiniEMU.extension",
                "/MitamaOS/games/EmulatorV3.4.extension",
                "/AdvanceOS/EmulatorV3.4.extension"
            };
            const char* names[] = { "PaperEMU", "PaperEMU", "EmulatorV3.4", "EmulatorV3.4" };
            for (int ci = 0; ci < 4; ci++) {
                if (SD.exists(candidates[ci])) {
                    emulPath = candidates[ci];
                    emulName = names[ci];
                    break;
                }
            }
            if (emulPath.isEmpty()) {
                _showMsg("No emulator found!", COL_ERR, 3000);
                _showMsg("Put PaperEMU.extension in /MitamaOS/games/", COL_DIM, 2500);
                return;
            }

            File bin = SD.open(emulPath.c_str(), FILE_READ);
            if (!bin) { _showMsg("Cannot open emulator", COL_ERR, 1500); return; }
            size_t sz = bin.size();

            _showFlashScreen("Emulator", emulName.c_str(), sz);

            const esp_partition_t* ota1 = esp_ota_get_next_update_partition(NULL);
            if (!Update.begin(sz, U_FLASH, ota1->address)) {
                bin.close(); _showMsg("Update.begin failed", COL_ERR, 1500); return;
            }

            _streamWithProgress(bin, sz);
            bin.close();

            if (!Update.end(false)) { _showMsg("Flash error", COL_ERR, 1500); return; }

            esp_ota_set_boot_partition(ota1);

            // Remember the emulator is now installed
            Preferences p;
            p.begin("app", false);
            p.putString("OtaApp", "emu");
            p.end();

            _saveBootTo(BOOT_GAMES);
            _showMsg("OK!  Rebooting...", COL_OK, 600);
            ESP.restart();
        }
    }

    bool _emulatorAlreadyInstalled() {
        return otaSlotHasEmulator();
    }

    void _saveRomPath(const String& path) {
        Preferences p;
        p.begin("app", false);
        p.putString("RomToLoad", path);
        p.end();
    }

    void _saveBootTo(int val) {
        Preferences p;
        p.begin("app", false);
        p.putInt("BootTo", val);
        p.end();
    }

    void _streamWithProgress(File& f, size_t total) {
        uint8_t buf[512];
        size_t written = 0;
        while (f.available()) {
            size_t r = f.read(buf, sizeof(buf));
            Update.write(buf, r);
            written += r;
            int pw = map(written, 0, total, 0, SCREEN_W - 20);
            _canvas.fillRect(10, 88, pw, 8, COL_ACCENT);
            _canvas.drawRect(10, 88, SCREEN_W - 20, 8, COL_DIM);
            char pct[12];
            snprintf(pct, sizeof(pct), "%d%%", (int)(written * 100 / total));
            _canvas.fillRect(100, 100, 40, 10, COL_BG);
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.drawCenterString(pct, 120, 100);
            _canvas.pushSprite(0, 0);
        }
    }

    void _showFlashScreen(const char* type, const char* name, size_t sz) {
        _canvas.fillSprite(COL_BG);
        OsUI::drawHeader(_canvas, "Flashing...", COL_HEADER_LOAD);
        _canvas.setTextColor(COL_ACCENT, COL_BG);
        _canvas.drawCenterString(type, 120, 28);
        _canvas.setTextColor(COL_TEXT, COL_BG);
        // Name without extension, truncated
        char buf[28]; strncpy(buf, name, 27); buf[27] = '\0';
        int l = strlen(buf);
        // strip the extension if present
        for (int i = l - 1; i > 0; i--) { if (buf[i] == '.') { buf[i] = '\0'; break; } }
        _canvas.drawCenterString(buf, 120, 44);
        char szb[20];
        snprintf(szb, sizeof(szb), "%.1f KB", sz / 1024.0f);
        _canvas.setTextColor(COL_DIM, COL_BG);
        _canvas.drawCenterString(szb, 120, 58);
        _canvas.fillRect(10, 88, 0, 8, COL_ACCENT);
        _canvas.drawRect(10, 88, SCREEN_W - 20, 8, COL_DIM);
        _canvas.pushSprite(0, 0);
    }

    void _showMsg(const char* msg, uint16_t col, int ms) {
        _canvas.fillRect(10, 52, SCREEN_W - 20, 20, COL_BG2);
        _canvas.drawRect(10, 52, SCREEN_W - 20, 20, col);
        _canvas.setTextColor(col, COL_BG2);
        _canvas.drawCenterString(msg, 120, 57);
        _canvas.pushSprite(0, 0);
        delay(ms);
    }

    void _deleteSelected() {
        _canvas.fillRect(20, 42, 200, 48, COL_BG2);
        _canvas.drawRect(20, 42, 200, 48, COL_ERR);
        _canvas.setTextColor(COL_ERR, COL_BG2);
        _canvas.drawCenterString("Delete?", 120, 50);
        char buf[28]; strncpy(buf, _games[_sel].name, 27); buf[27] = '\0';
        _canvas.setTextColor(COL_DIM, COL_BG2);
        _canvas.drawCenterString(buf, 120, 62);
        _canvas.setTextColor(COL_TEXT, COL_BG2);
        _canvas.drawCenterString("[Y] Yes   [N] No", 120, 76);
        _canvas.pushSprite(0, 0);

        while (true) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isKeyPressed('y')) {
                OsUI::waitRelease();
                String path = "/MitamaOS/games/" + String(_games[_sel].name);
                SD.remove(path.c_str());
                // If this was the only .nes/.gb/.gbc left, clear the emulator flag
                _scanGames();
                bool hasNES = false;
                for (int i = 0; i < _count; i++) {
                    const char* e = _games[i].ext;
                    if (strcmp(e,"nes")==0||strcmp(e,"gb")==0||strcmp(e,"gbc")==0)
                        hasNES = true;
                }
                if (!hasNES) {
                    Preferences p; p.begin("app",false);
                    p.remove("OtaApp"); p.end();
                }
                if (_sel >= _count) _sel = max(0, _count-1);
                _draw();
                return;
            }
            if (M5Cardputer.Keyboard.isKeyPressed('n') ||
                M5Cardputer.Keyboard.isKeyPressed('`')) {
                OsUI::waitRelease();
                _draw();
                return;
            }
            delay(20);
        }
    }

    void _draw() {
        _canvas.fillSprite(COL_BG);
        OsUI::drawHeader(_canvas, "Games");
        OsUI::drawFooter(_canvas, ";/.=nav ENT=run D=del `=home");

        if (_count == 0) {
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.drawCenterString("No games found", 120, 52);
            _canvas.setTextColor(COL_TEXT, COL_BG);
            _canvas.drawCenterString("Put in /MitamaOS/games/ on SD:", 120, 66);
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.drawCenterString(".ard  .nes  .gb  .gbc", 120, 80);
            _canvas.drawCenterString("PaperEMU: NES GB GBC", 120, 88);
            _canvas.drawCenterString("+ PaperEMU.extension", 120, 100);
            _canvas.pushSprite(0, 0);
            return;
        }

        const int ROW_H   = 15;
        const int VISIBLE = 6;
        const int LIST_Y  = CONTENT_Y + 2;

        // Scroll
        if (_sel * ROW_H < -_camY)
            _camY = -_sel * ROW_H;
        else if (_sel * ROW_H > -_camY + (VISIBLE - 1) * ROW_H)
            _camY = -((_sel - VISIBLE + 1) * ROW_H);
        _camY = constrain(_camY, -(max(0, _count - VISIBLE) * ROW_H), 0);

        for (int i = 0; i < _count; i++) {
            int y = LIST_Y + i * ROW_H + _camY;
            if (y < LIST_Y - ROW_H || y > SCREEN_H - FOOTER_H) continue;

            bool sel = (i == _sel);
            uint16_t bg = sel ? COL_SELECTED : COL_BG;
            uint16_t fg = sel ? WHITE : COL_TEXT;

            _canvas.fillRect(4, y, SCREEN_W - 8, ROW_H - 1, bg);

            // Type with color
            _canvas.setTextColor(sel ? WHITE : _typeColor(i), bg);
            _canvas.setCursor(8, y + 3);
            _canvas.print(_typeLabel(i));

            // Name without extension
            char buf[30]; strncpy(buf, _games[i].name, 29); buf[29] = '\0';
            int l = strlen(buf);
            for (int j = l-1; j > 0; j--) { if (buf[j]=='.') { buf[j]='\0'; break; } }

            _canvas.setTextColor(fg, bg);
            _canvas.setCursor(36, y + 3);
            _canvas.print(buf);
        }

        // Count + emulator status
        char cnt[24];
        snprintf(cnt, sizeof(cnt), "%d/%d", _sel + 1, _count);
        _canvas.setTextColor(COL_DIM, COL_BG);
        _canvas.setTextDatum(top_right);
        _canvas.drawString(cnt, SCREEN_W - 4, CONTENT_Y + 2);
        _canvas.setTextDatum(top_left);

        if (_emulatorAlreadyInstalled()) {
            _canvas.setTextColor(COL_OK, COL_BG);
            _canvas.setTextDatum(top_right);
            _canvas.drawString("EMU", SCREEN_W - 4, CONTENT_Y + 12);
            _canvas.setTextDatum(top_left);
        }

        _canvas.pushSprite(0, 0);
    }
};
