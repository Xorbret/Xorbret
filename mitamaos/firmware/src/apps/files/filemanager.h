#pragma once
#include <M5Cardputer.h>
#include <SD.h>
#include <Update.h>
#include <Preferences.h>
#include <vector>
#include "esp_ota_ops.h"
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/disp_lock.h"
#include "../../config.h"
#include "../../system/ota_tools.h"
#if PAPEROS_ENABLE_MUSIC
#include "../music/music.h"
#endif
#if PAPEROS_ENABLE_EDITOR3D
#include "../editor3d/editor3d.h"
#endif

// If PNG/JPG/QOI decoding still fails to build on this M5GFX version after
// moving SD.h above M5Cardputer.h in main.cpp, set this to 0 - it drops the
// three calls that need those decoders and falls back to "not supported"
// instead of a compile error, without touching the rest of this file
// (menu/hex/BMP/rom/audio/3d).
#define FM_NATIVE_IMAGE_DECODE 1

#define FM_MAX_FILES  300
#define FM_VISIBLE     6
// IMPORTANT: lines are stored on the heap via String[], NOT on the stack (else stack overflow + reboot)
#define FM_VIEW_LINES 500

// File type routing: don't dump every file as text. Games/audio/images each
// have an owning app + folder convention (/games, /music, /paint) already
// used elsewhere in the OS (see games.h) - the file manager now follows the
// same convention instead of always falling back to the raw text viewer,
// which used to print ROM/BMP bytes as garbled "text" and choke on large
// binaries claiming they were "too large" when it was really just the
// wrong viewer for the format.
enum FileKind {
    FK_TEXT = 0,   // read as lines: txt/md/log/ini/cfg/etc.
    FK_CODE,       // same viewer as text, different icon: py/lua/js/c/cpp/h/ino/json/xml
    FK_IMAGE,      // bmp (+ png/jpg where the display lib supports it)
    FK_AUDIO,      // wav/mp3/ogg -> Music app
    FK_ROM,        // nes/gb/gbc -> Games app (via PaperEMU)
    FK_ARDUBOY,    // ard -> Games app (direct OTA flash)
    FK_MODEL3D,    // obj (+ stl/gltf/fbx identified but not loadable yet) -> 3D Editor
    FK_ARCHIVE,    // zip/rar/7z/gz - can't extract, just identify it
    FK_FONT,       // ttf/vlw
    FK_BIN         // anything else unrecognized - refuse to text-dump it
};

struct FileTypeInfo { FileKind kind; const char* icon; };

inline FileTypeInfo fmClassify(const String& lowName) {
    // Order matters only in that longer/more specific suffixes should be
    // checked first where they could collide (none currently do).
    static const struct { const char* ext; FileKind kind; const char* icon; } table[] = {
        // images
        {".bmp", FK_IMAGE, "I"}, {".png", FK_IMAGE, "I"}, {".jpg", FK_IMAGE, "I"},
        {".jpeg", FK_IMAGE, "I"}, {".gif", FK_IMAGE, "I"}, {".qoi", FK_IMAGE, "I"},
        // audio
        {".wav", FK_AUDIO, "M"}, {".mp3", FK_AUDIO, "M"}, {".ogg", FK_AUDIO, "M"},
        {".flac", FK_AUDIO, "M"}, {".m4a", FK_AUDIO, "M"},
        // games / roms
        {".nes", FK_ROM, "G"}, {".gb", FK_ROM, "G"}, {".gbc", FK_ROM, "G"},
        {".sms", FK_ROM, "G"}, {".gg", FK_ROM, "G"}, {".pce", FK_ROM, "G"},
        {".ngp", FK_ROM, "G"}, {".ngc", FK_ROM, "G"}, {".ws", FK_ROM, "G"},
        {".wsc", FK_ROM, "G"}, {".sfc", FK_ROM, "G"}, {".smc", FK_ROM, "G"},
        {".md", FK_ROM, "G"}, {".a26", FK_ROM, "G"}, {".a78", FK_ROM, "G"},
        {".col", FK_ROM, "G"}, {".lnx", FK_ROM, "G"}, {".cpr", FK_ROM, "G"},
        {".ard", FK_ARDUBOY, "G"},
        // 3D models - only .obj actually loads (Editor3D), rest identified only
        {".obj", FK_MODEL3D, "3"}, {".stl", FK_MODEL3D, "3"},
        {".gltf", FK_MODEL3D, "3"}, {".glb", FK_MODEL3D, "3"}, {".fbx", FK_MODEL3D, "3"},
        // text / docs
        {".txt", FK_TEXT, "T"}, {".md", FK_TEXT, "T"}, {".log", FK_TEXT, "T"},
        {".ini", FK_TEXT, "T"}, {".cfg", FK_TEXT, "T"}, {".conf", FK_TEXT, "T"},
        {".yaml", FK_TEXT, "T"}, {".yml", FK_TEXT, "T"}, {".env", FK_TEXT, "T"},
        // data / code
        {".csv", FK_CODE, "C"}, {".json", FK_CODE, "J"}, {".xml", FK_CODE, "X"},
        {".html", FK_CODE, "X"}, {".htm", FK_CODE, "X"}, {".css", FK_CODE, "X"},
        {".py", FK_CODE, "P"}, {".lua", FK_CODE, "P"}, {".js", FK_CODE, "P"},
        {".c", FK_CODE, "P"}, {".cpp", FK_CODE, "P"}, {".h", FK_CODE, "P"},
        {".hpp", FK_CODE, "P"}, {".ino", FK_CODE, "P"}, {".sh", FK_CODE, "P"},
        // archives (identified, not extractable)
        {".zip", FK_ARCHIVE, "Z"}, {".rar", FK_ARCHIVE, "Z"}, {".7z", FK_ARCHIVE, "Z"},
        {".gz", FK_ARCHIVE, "Z"}, {".tar", FK_ARCHIVE, "Z"},
        // fonts
        {".ttf", FK_FONT, "F"}, {".vlw", FK_FONT, "F"},
        // firmware/binaries - never text-preview these
        {".bin", FK_BIN, "B"}, {".extension", FK_BIN, "B"}, {".elf", FK_BIN, "B"},
        {".dat", FK_BIN, "B"},
    };
    for (auto& e : table)
        if (lowName.endsWith(e.ext)) return { e.kind, e.icon };
    return { FK_BIN, "?" };
}

//  FileManager v2.0 - full-featured file explorer
//
//  REBOOT FIX: String lines[120] on the stack was ~7KB of stack -> overflow.
//  The text viewer now allocates lines on the heap (new String[]).
//
//  Keys:
//   ; / .         navigate up/down
//   ENT           open file/folder
//   DEL           back
//   M             context menu (Open/Copy/Cut/Paste/Rename/Delete/Info/Hex/New Folder)
//   D             delete (with confirmation)
//   R             rename
//   N             new folder
//   C             copy to clipboard
//   X             cut to clipboard
//   P             paste from clipboard
//   I             file info
//   H             view file as hex
//   F             find file in current folder
//   Tab           show/hide key hints
//   `             go home

class FileManager : public App {
public:
    FileManager(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Files"; }
    const char* getIcon() const override { return "F"; }
    const char* getDesc() const override { return "File manager"; }

    void onStart() override {
        _path     = "/";
        _clipPath = "";
        _clipMove = false;
        _hintPage = 0;

        // Set by Browser::_downloadAndViewImage right before launch("Files")
        // - same pending-file handoff pattern as g_musicPendingFile /
        // g_editor3dPendingFile, just declared in browser.h (included
        // before this file) instead of here.
        if (g_imagePendingFile[0]) {
            String pendingPath = g_imagePendingFile;
            g_imagePendingFile[0] = '\0';

            int slash = pendingPath.lastIndexOf('/');
            _path = (slash > 0) ? pendingPath.substring(0, slash) : "/";
            _loadDir();

            String lowExt = pendingPath; lowExt.toLowerCase();
            _viewImage(pendingPath, lowExt);
        }

        _loadDir();
        _draw();
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();

        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            _draw(); { DispLock _lk; _canvas.pushSprite(0, 0); }
            if (OsUI::confirmExit(_canvas)) AppManager::get().goHome();
            else _draw();
            return;
        }

        auto st  = M5Cardputer.Keyboard.keysState();
        bool redraw = false;

        if (M5Cardputer.Keyboard.isKeyPressed(';')) {
            if (_sel > 0) { _sel--; if (_sel < _scroll) _scroll = _sel; redraw = true; }
            delay(KEY_REPEAT_MS);
        }
        if (M5Cardputer.Keyboard.isKeyPressed('.')) {
            if (_sel < _count - 1) {
                _sel++;
                if (_sel >= _scroll + FM_VISIBLE) _scroll = _sel - FM_VISIBLE + 1;
                redraw = true;
            }
            delay(KEY_REPEAT_MS);
        }
        // Page Up/Down via Fn+;/.
        if (st.fn && M5Cardputer.Keyboard.isKeyPressed(';')) {
            _sel = max(0, _sel - FM_VISIBLE);
            if (_sel < _scroll) _scroll = _sel;
            redraw = true; delay(KEY_REPEAT_MS);
        }
        if (st.fn && M5Cardputer.Keyboard.isKeyPressed('.')) {
            _sel = min(_count - 1, _sel + FM_VISIBLE);
            if (_sel >= _scroll + FM_VISIBLE) _scroll = _sel - FM_VISIBLE + 1;
            redraw = true; delay(KEY_REPEAT_MS);
        }

        if (st.enter && _count > 0) {
            OsUI::waitRelease();
            if (_isParentEntry(_sel)) {
                _goUp();
            } else {
                String full = _fullPath(_sel);
                if (_isDir[_sel]) {
                    _path = full;
                    _loadDir();
                } else {
                    _viewFile(full);
                }
            }
            redraw = true;
        }

        if ((st.del || M5Cardputer.Keyboard.isKeyPressed('\b')) && _path != "/") {
            _goUp();
            redraw = true;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('d') && _count > 0 && !_isParentEntry(_sel)) {
            OsUI::waitRelease();
            _actDelete();
            redraw = true;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('r') && _count > 0 && !_isParentEntry(_sel)) {
            OsUI::waitRelease();
            _actRename();
            redraw = true;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('n')) {
            OsUI::waitRelease();
            _actNewFolder();
            redraw = true;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('c') && _count > 0 && !_isParentEntry(_sel)) {
            _actCopy();
            redraw = true;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('x') && _count > 0 && !_isParentEntry(_sel)) {
            _actCut();
            redraw = true;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('p') && _clipPath != "") {
            OsUI::waitRelease();
            _actPaste();
            redraw = true;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('i') && _count > 0 && !_isParentEntry(_sel)) {
            OsUI::waitRelease();
            _actInfo();
            redraw = true;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('h') && _count > 0 && !_isParentEntry(_sel) && !_isDir[_sel]) {
            OsUI::waitRelease();
            _viewHex(_fullPath(_sel));
            redraw = true;
        }

        // "Menu" - context menu (Open/Copy/Cut/Paste/Rename/Delete/Info/
        // Hex/New Folder) so hotkeys don't need memorizing. Works on the
        // selected file/folder and on empty space (New Folder/Paste only),
        // like a right-click in Windows Explorer.
        if (M5Cardputer.Keyboard.isKeyPressed('m')) {
            OsUI::waitRelease();
            _showContextMenu();
            redraw = true;
        }

        if (M5Cardputer.Keyboard.isKeyPressed('f')) {
            OsUI::waitRelease();
            String query = OsUI::inputText(_canvas, "Search name:");
            if (query.length() > 0) {
                query.toLowerCase();
                for (int i = 0; i < _count; i++) {
                    String n = String(_names[i]); n.toLowerCase();
                    if (n.indexOf(query) >= 0) {
                        _sel = i;
                        _scroll = max(0, i - FM_VISIBLE / 2);
                        break;
                    }
                }
            }
            redraw = true;
        }

        if (M5Cardputer.Keyboard.keysState().tab) {
            _hintPage = (_hintPage + 1) % 3;
            OsUI::waitRelease();
            redraw = true;
        }

        if (redraw) _draw();
    }

private:
    LGFX_Sprite& _canvas;
    String  _path;
    char    _names[FM_MAX_FILES][36];
    bool    _isDir[FM_MAX_FILES];
    uint32_t _sizes[FM_MAX_FILES];
    int     _count  = 0;
    int     _sel    = 0;
    int     _scroll = 0;
    String  _clipPath;
    bool    _clipMove  = false;
    int     _hintPage  = 0;
    bool    _hasParent = false;

    bool _isParentEntry(int idx) const {
        return idx == 0 && _hasParent && strcmp(_names[0], "..") == 0;
    }

    void _goUp() {
        if (_path == "/") return;
        int last = _path.lastIndexOf('/');
        _path = (last <= 0) ? "/" : _path.substring(0, last);
        _loadDir();
    }

    String _fullPath(int idx) {
        if (_isParentEntry(idx)) return _path;
        String dir = _path.endsWith("/") ? _path : _path + "/";
        return dir + String(_names[idx]);
    }

    void _loadDir() {
        _count = 0; _sel = 0; _scroll = 0;
        _hasParent = (_path != "/");
        if (_hasParent && _count < FM_MAX_FILES) {
            strncpy(_names[_count], "..", 35);
            _isDir[_count] = true;
            _sizes[_count] = 0;
            _count++;
        }
        if (SD.cardType() == CARD_NONE) return;
        File root = SD.open(_path.c_str());
        if (!root || !root.isDirectory()) return;

        // Two passes: folders, then files
        struct Entry { char name[36]; bool isDir; uint32_t size; };
        // Heap-allocated to avoid blowing the stack
        Entry* dirs  = new Entry[FM_MAX_FILES];
        Entry* files = new Entry[FM_MAX_FILES];
        int dc = 0, fc = 0;

        File f = root.openNextFile();
        while (f && (dc + fc) < FM_MAX_FILES) {
            const char* n = f.name();
            // Skip hidden system files
            if (n[0] == '.') { f = root.openNextFile(); continue; }
            // Name: SD.open() may return a full path or just the name
            const char* base = strrchr(n, '/');
            base = base ? base + 1 : n;
            if (f.isDirectory()) {
                strncpy(dirs[dc].name, base, 35); dirs[dc].name[35] = 0;
                dirs[dc].isDir = true; dirs[dc].size = 0; dc++;
            } else {
                strncpy(files[fc].name, base, 35); files[fc].name[35] = 0;
                files[fc].isDir = false; files[fc].size = f.size(); fc++;
            }
            f = root.openNextFile();
        }
        root.close();

        for (int i = 0; i < dc && _count < FM_MAX_FILES; i++) {
            strncpy(_names[_count], dirs[i].name, 35);
            _isDir[_count] = true; _sizes[_count] = 0; _count++;
        }
        for (int i = 0; i < fc && _count < FM_MAX_FILES; i++) {
            strncpy(_names[_count], files[i].name, 35);
            _isDir[_count] = false; _sizes[_count] = files[i].size; _count++;
        }
        delete[] dirs;
        delete[] files;
    }

    void _deleteDir(const String& path) {
        File dir = SD.open(path.c_str());
        if (!dir) return;
        File f = dir.openNextFile();
        while (f) {
            String fp = path + "/" + String(f.name());
            if (f.isDirectory()) _deleteDir(fp);
            else SD.remove(fp.c_str());
            f = dir.openNextFile();
        }
        dir.close();
        SD.rmdir(path.c_str());
    }

    bool _copyFile(const String& src, const String& dst) {
        File s = SD.open(src.c_str(), FILE_READ);
        if (!s) return false;
        File d = SD.open(dst.c_str(), FILE_WRITE);
        if (!d) { s.close(); return false; }
        uint8_t* buf = new uint8_t[512];
        if (!buf) { s.close(); d.close(); return false; }
        size_t r;
        while ((r = s.read(buf, 512)) > 0) d.write(buf, r);
        delete[] buf;
        s.close(); d.close();
        return true;
    }

    // Compatible with the Games app (games.h): same OTA scheme (ota_1
    // slot, Preferences "app"/RomToLoad/BootTo). BOOT_GAMES=7 must match
    // the value in games.h.
    static constexpr int FM_BOOT_GAMES = 7;

    void _saveBootTo(int val) {
        Preferences p;
        p.begin("app", false);
        p.putInt("BootTo", val);
        p.end();
    }

    void _saveRomPath(const String& path) {
        Preferences p;
        p.begin("app", false);
        p.putString("RomToLoad", path);
        p.end();
    }

    bool _flashBinToOtaSlot(const String& path, size_t& outSz) {
        File bin = SD.open(path.c_str(), FILE_READ);
        if (!bin) { _showToast("Cannot open file"); return false; }
        size_t sz = bin.size();
        if (sz == 0) { bin.close(); _showToast("File is empty"); return false; }

        const esp_partition_t* cur  = esp_ota_get_running_partition();
        const esp_partition_t* ota1 = esp_ota_get_next_update_partition(cur);
        if (!ota1 || !Update.begin(sz, U_FLASH, ota1->address)) {
            bin.close(); _showToast("Update.begin failed"); return false;
        }

        _canvas.fillSprite(COL_BG);
        OsUI::drawHeader(_canvas, "Flashing...", COL_HEADER);
        uint8_t* buf = new uint8_t[1024];
        size_t done = 0;
        while (done < sz) {
            size_t chunk = bin.read(buf, 1024);
            if (chunk == 0) break;
            Update.write(buf, chunk);
            done += chunk;
            _canvas.fillRect(20, 60, 200, 12, COL_BG2);
            _canvas.fillRect(20, 60, (int)(200.0f * done / sz), 12, COL_ACCENT);
            { DispLock _lk; _canvas.pushSprite(0, 0); }
        }
        delete[] buf;
        bin.close();

        if (!Update.end(false)) { _showToast("Flash error"); return false; }
        esp_ota_set_boot_partition(ota1);
        outSz = sz;
        return true;
    }

    // Launches .nes/.gb/.gbc/... via the same contract as the Games app -
    // works with any path on the SD card, not just /MitamaOS/games/.
    void _launchRom(const String& path) {
        if (!OsUI::confirmExit(_canvas, "Play this ROM?")) return;
        _saveRomPath(path);

        if (otaSlotHasEmulator()) {
            // Emulator is already flashed to ota_1 - just switch to it
            const esp_partition_t* cur  = esp_ota_get_running_partition();
            const esp_partition_t* ota1 = esp_ota_get_next_update_partition(cur);
            esp_ota_set_boot_partition(ota1);
            _saveBootTo(FM_BOOT_GAMES);
            _showToast("Loading ROM...");
            ESP.restart();
            return;
        }

        // No emulator flashed - look for one the same way the Games app does
        const char* candidates[] = {
            "/MitamaOS/games/PaperEMU.extension", "/MitamaOS/games/MiniEMU.extension",
            "/MitamaOS/games/EmulatorV3.4.extension", "/AdvanceOS/EmulatorV3.4.extension"
        };
        String emulPath;
        for (auto c : candidates) if (SD.exists(c)) { emulPath = c; break; }
        if (emulPath.isEmpty()) {
            _showToast("No emulator! Put PaperEMU.extension in /MitamaOS/games/");
            return;
        }

        size_t sz;
        if (!_flashBinToOtaSlot(emulPath, sz)) return;
        Preferences p; p.begin("app", false); p.putString("OtaApp", "emu"); p.end();
        _saveBootTo(FM_BOOT_GAMES);
        _showToast("OK! Rebooting...");
        ESP.restart();
    }

    void _launchArduboy(const String& path) {
        if (!OsUI::confirmExit(_canvas, "Play this Arduboy game?")) return;
        size_t sz;
        if (!_flashBinToOtaSlot(path, sz)) return;
        _saveBootTo(FM_BOOT_GAMES);
        _showToast("OK! Rebooting...");
        ESP.restart();
    }

    // Music app only scans its own /music folder - if the file lives
    // elsewhere, copy it there first, then switch to the app.
    void _launchAudio(const String& path) {
#if !PAPEROS_ENABLE_MUSIC
        (void)path; _showToast("No music player in MitamaOS"); return;
#else
        if (!SD.exists("/MitamaOS")) SD.mkdir("/MitamaOS");
        if (!SD.exists("/MitamaOS/music")) SD.mkdir("/MitamaOS/music");
        String target = path;
        if (!path.startsWith("/MitamaOS/music/")) {
            String fname = path.substring(path.lastIndexOf('/') + 1);
            target = "/MitamaOS/music/" + fname;
            if (!SD.exists(target.c_str())) {
                if (!_copyFile(path, target)) { _showToast("Copy failed"); return; }
            }
        }
        String fnameOnly = target.substring(target.lastIndexOf('/') + 1);
        strncpy(g_musicPendingFile, fnameOnly.c_str(), sizeof(g_musicPendingFile) - 1);
        g_musicPendingFile[sizeof(g_musicPendingFile) - 1] = '\0';
        _showToast("Opening in Music...");
        AppManager::get().launch("Music");
#endif
    }

    // ── Actions (used by both hotkeys and the context menu) ────────────

    void _actDelete() {
        String full = _fullPath(_sel);
        const char* msg = _isDir[_sel] ? "Delete folder?" : "Delete file?";
        if (OsUI::confirmExit(_canvas, msg)) {
            if (_isDir[_sel]) _deleteDir(full);
            else SD.remove(full.c_str());
            if (_sel >= _count - 1 && _sel > 0) _sel--;
            _loadDir();
        }
    }

    void _actRename() {
        String newName = OsUI::inputText(_canvas, "New name:");
        if (newName != "") {
            String oldFull = _fullPath(_sel);
            String dir = _path.endsWith("/") ? _path : _path + "/";
            SD.rename(oldFull.c_str(), (dir + newName).c_str());
            _loadDir();
        }
    }

    void _actNewFolder() {
        String name = OsUI::inputText(_canvas, "Folder name:");
        if (name != "") {
            String dir = _path.endsWith("/") ? _path : _path + "/";
            SD.mkdir((dir + name).c_str());
            _loadDir();
        }
    }

    void _actCopy() {
        _clipPath = _fullPath(_sel);
        _clipMove = false;
        OsUI::showToast(_canvas, _isDir[_sel] ? "Dir copied!" : "Copied!", COL_OK);
        { DispLock _lk; _canvas.pushSprite(0, 0); } delay(600);
    }

    void _actCut() {
        _clipPath = _fullPath(_sel);
        _clipMove = true;
        OsUI::showToast(_canvas, "Cut!", COL_WARN);
        { DispLock _lk; _canvas.pushSprite(0, 0); } delay(600);
    }

    void _actPaste() {
        String fileName = _clipPath.substring(_clipPath.lastIndexOf('/') + 1);
        String dir = _path.endsWith("/") ? _path : _path + "/";
        String dest = dir + fileName;

        bool ok = false;
        if (!SD.exists(_clipPath.c_str()) || _clipPath == dest) {
            OsUI::showToast(_canvas, "Skip: same/missing", COL_WARN);
        } else {
            ok = _copyFile(_clipPath, dest);
            if (ok && _clipMove) { SD.remove(_clipPath.c_str()); _clipPath = ""; }
            OsUI::showToast(_canvas, ok ? "Pasted!" : "Paste failed", ok ? COL_OK : COL_ERR);
        }
        { DispLock _lk; _canvas.pushSprite(0, 0); } delay(700);
        _loadDir();
    }

    void _actInfo() {
        _showInfo(_fullPath(_sel));
    }

    // ── Context menu (like a right-click in Windows Explorer) ──────────

    void _showContextMenu() {
        bool onEntry = (_count > 0 && !_isParentEntry(_sel));
        bool isDir   = onEntry && _isDir[_sel];
        bool canPaste = (_clipPath != "");

        struct MenuItem { const char* label; int action; };
        std::vector<MenuItem> items;
        // action codes: 0 Open,1 Copy,2 Cut,3 Paste,4 Rename,5 Delete,6 Info,7 Hex,8 New Folder
        if (onEntry) {
            items.push_back({"Open", 0});
            items.push_back({"Copy", 1});
            items.push_back({"Cut", 2});
            if (canPaste) items.push_back({"Paste", 3});
            items.push_back({"Rename", 4});
            items.push_back({"Delete", 5});
            items.push_back({"Info", 6});
            if (!isDir) items.push_back({"View as Hex", 7});
            items.push_back({"New Folder", 8});
        } else {
            // Empty space / ".." - actions on the folder itself
            if (canPaste) items.push_back({"Paste", 3});
            items.push_back({"New Folder", 8});
        }

        int sel = 0;
        int n = items.size();
        if (n == 0) return;

        const int rowH = 16;
        const int listTop = CONTENT_Y + 4;
        const int listBottom = SCREEN_H - FOOTER_H - 2; // stay above the footer
        const int visibleRows = (listBottom - listTop) / rowH;
        int scrollTop = 0; // index of the first visible item

        bool running = true;
        while (running) {
            M5Cardputer.update();
            auto ks = M5Cardputer.Keyboard.keysState();

            // keep the selection inside the scroll window
            if (sel < scrollTop) scrollTop = sel;
            if (sel >= scrollTop + visibleRows) scrollTop = sel - visibleRows + 1;
            if (scrollTop < 0) scrollTop = 0;
            if (scrollTop > n - visibleRows) scrollTop = (n > visibleRows) ? (n - visibleRows) : 0;

            _canvas.fillSprite(COL_BG);
            OsUI::drawHeader(_canvas, onEntry ? _names[_sel] : "Folder", COL_HEADER);

            int y0 = listTop;
            int lastVisible = min(n, scrollTop + visibleRows);
            for (int i = scrollTop; i < lastVisible; i++) {
                int row = i - scrollTop;
                int y = y0 + row * rowH;
                bool sSel = (i == sel);
                _canvas.fillRect(10, y, SCREEN_W - 20, 15,
                                  sSel ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG2);
                _canvas.setTextColor(sSel ? WHITE : (uint16_t)COL_TEXT,
                                      sSel ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG2);
                _canvas.setCursor(16, y + 3);
                _canvas.print(items[i].label);
            }
            // scroll indicators, so it's clear the list continues off-screen
            if (scrollTop > 0)              _canvas.fillTriangle(SCREEN_W-14, listTop+2,      SCREEN_W-9, listTop+2,      SCREEN_W-11, listTop-2,      COL_ACCENT);
            if (lastVisible < n)            _canvas.fillTriangle(SCREEN_W-14, listBottom-2,    SCREEN_W-9, listBottom-2,   SCREEN_W-11, listBottom+2,   COL_ACCENT);

            OsUI::drawFooter(_canvas, ";.=nav ENT=select `=cancel");
            { DispLock _lk; _canvas.pushSprite(0, 0); }

            if (M5Cardputer.Keyboard.isKeyPressed(';')) { sel = (sel + n - 1) % n; delay(KEY_REPEAT_MS); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { sel = (sel + 1) % n; delay(KEY_REPEAT_MS); }
            if (M5Cardputer.Keyboard.isKeyPressed('`') || ks.del) { OsUI::waitRelease(); return; }
            if (ks.enter) {
                OsUI::waitRelease();
                int act = items[sel].action;
                switch (act) {
                    case 0: _viewFile(_fullPath(_sel)); break;
                    case 1: _actCopy(); break;
                    case 2: _actCut(); break;
                    case 3: _actPaste(); break;
                    case 4: _actRename(); break;
                    case 5: _actDelete(); break;
                    case 6: _actInfo(); break;
                    case 7: _viewHex(_fullPath(_sel)); break;
                    case 8: _actNewFolder(); break;
                }
                return;
            }
            delay(10);
        }
    }

    // ── Hex viewer ───────────────────────────────────────────────────

    void _viewHex(const String& path) {
        File f = SD.open(path.c_str());
        if (!f) return;
        size_t sz = f.size();

        const int bytesPerLine = 8;
        const int linesVisible = 6;
        long offset = 0;
        bool running = true;

        while (running) {
            M5Cardputer.update();
            auto ks = M5Cardputer.Keyboard.keysState();

            _canvas.fillSprite(COL_BG);
            String fname = path.substring(path.lastIndexOf('/') + 1);
            OsUI::drawHeader(_canvas, ("Hex: " + fname).c_str(), COL_HEADER);

            f.seek(offset);
            uint8_t buf[bytesPerLine * linesVisible];
            size_t got = f.read(buf, sizeof(buf));

            _canvas.setTextColor(COL_TEXT, COL_BG);
            for (int line = 0; line < linesVisible; line++) {
                int lineOff = line * bytesPerLine;
                if ((size_t)lineOff >= got) break;
                int y = CONTENT_Y + 2 + line * 14;

                char addrBuf[8];
                snprintf(addrBuf, sizeof(addrBuf), "%04lX", offset + lineOff);
                _canvas.setTextColor(COL_DIM, COL_BG);
                _canvas.setCursor(2, y);
                _canvas.print(addrBuf);

                _canvas.setTextColor(COL_ACCENT, COL_BG);
                int hx = 40;
                String ascii = "";
                for (int b = 0; b < bytesPerLine; b++) {
                    int idx = lineOff + b;
                    if ((size_t)idx >= got) break;
                    char hexBuf[4];
                    snprintf(hexBuf, sizeof(hexBuf), "%02X ", buf[idx]);
                    _canvas.setCursor(hx, y);
                    _canvas.print(hexBuf);
                    hx += 16;
                    char c = buf[idx];
                    ascii += (c >= 32 && c < 127) ? String(c) : String('.');
                }
                _canvas.setTextColor(COL_DIM, COL_BG);
                _canvas.setCursor(hx + 4, y);
                _canvas.print(ascii);
            }

            char info[40];
            snprintf(info, sizeof(info), "off %ld/%u  ;.=scroll `=back", offset, (unsigned)sz);
            OsUI::drawFooter(_canvas, info);
            { DispLock _lk; _canvas.pushSprite(0, 0); }

            if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                offset = max(0L, offset - bytesPerLine * linesVisible);
                delay(KEY_REPEAT_MS);
            }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                if (offset + bytesPerLine * linesVisible < (long)sz)
                    offset += bytesPerLine * linesVisible;
                delay(KEY_REPEAT_MS);
            }
            if (ks.fn && M5Cardputer.Keyboard.isKeyPressed(';')) {
                offset = max(0L, offset - bytesPerLine * linesVisible * 8);
                delay(150);
            }
            if (ks.fn && M5Cardputer.Keyboard.isKeyPressed('.')) {
                offset = min((long)sz, offset + bytesPerLine * linesVisible * 8);
                delay(150);
            }
            if (M5Cardputer.Keyboard.isKeyPressed('`') || ks.del) { OsUI::waitRelease(); running = false; }
            delay(5);
        }
        f.close();
    }

    // Editor3D only scans its own /MitamaOS/3d/ folder and only .obj files - same as
    // Music, copy it there if needed, then switch to the app.
    void _launch3D(const String& path, const String& low) {
#if !PAPEROS_ENABLE_EDITOR3D
        (void)path; (void)low; _showToast("No 3D editor in MitamaOS"); return;
#else
        if (!low.endsWith(".obj")) {
            _showToast("Only .obj loads in 3D Editor (yet)");
            return;
        }
        if (!SD.exists("/MitamaOS")) SD.mkdir("/MitamaOS");
        if (!SD.exists("/MitamaOS/3d")) SD.mkdir("/MitamaOS/3d");
        String target = path;
        if (!path.startsWith("/MitamaOS/3d/")) {
            String fname = path.substring(path.lastIndexOf('/') + 1);
            target = "/MitamaOS/3d/" + fname;
            if (!SD.exists(target.c_str())) {
                if (!_copyFile(path, target)) { _showToast("Copy failed"); return; }
            }
        }
        strncpy(g_editor3dPendingFile, target.c_str(), sizeof(g_editor3dPendingFile) - 1);
        g_editor3dPendingFile[sizeof(g_editor3dPendingFile) - 1] = '\0';
        _showToast("Opening in 3D Editor...");
        AppManager::get().launch("3D Editor");
#endif
    }

    void _viewFile(const String& path) {
        String low = path; low.toLowerCase();
        FileTypeInfo ft = fmClassify(low);

        switch (ft.kind) {
            case FK_IMAGE:
                if (low.endsWith(".gif")) _showToast("GIF preview not supported (no decoder)");
                else _viewImage(path, low);
                return;
            case FK_ROM:     _launchRom(path);     return;
            case FK_ARDUBOY: _launchArduboy(path); return;
            case FK_MODEL3D: _launch3D(path, low); return;
            case FK_AUDIO:   _launchAudio(path);   return;
            case FK_ARCHIVE: _showToast("Archives can't be opened on-device"); return;
            case FK_FONT:    _showToast("Font file - nothing to preview"); return;
            case FK_BIN:     _showToast("Binary file - no preview available"); return;
            case FK_TEXT:
            case FK_CODE:
            default:
                break;  // falls through to the text viewer below
        }

        // Text preview - lines on the HEAP
        File f = SD.open(path.c_str());
        if (!f) return;

        // Allocate the line array on the heap - NOT the stack
        String* lines = new String[FM_VIEW_LINES];
        if (!lines) { f.close(); return; }
        int lc = 0;

        while (f.available() && lc < FM_VIEW_LINES) {
            String l = f.readStringUntil('\n');
            l.replace("\r", "");
            // Wrap long lines
            while (l.length() > 38 && lc < FM_VIEW_LINES - 1) {
                lines[lc++] = l.substring(0, 38);
                l = l.substring(38);
            }
            if (lc < FM_VIEW_LINES) lines[lc++] = l;
        }
        f.close();

        int scroll  = 0;
        int visible = 7;
        int maxScr  = max(0, lc - visible);

        bool running = true;
        while (running) {
            M5Cardputer.update();
            auto ks = M5Cardputer.Keyboard.keysState();
            _canvas.fillSprite(COL_BG);

            String fname = path.substring(path.lastIndexOf('/') + 1);
            if (fname.length() > 26) fname = "~" + fname.substring(fname.length() - 25);
            OsUI::drawHeader(_canvas, fname.c_str(), COL_HEADER);

            _canvas.setTextSize(1);
            _canvas.setTextColor(COL_TEXT, COL_BG);
            for (int i = 0; i < visible; i++) {
                int idx = scroll + i;
                if (idx >= lc) break;
                _canvas.setCursor(2, CONTENT_Y + 2 + i * 14);
                _canvas.print(lines[idx]);
            }

            char info[32];
            snprintf(info, sizeof(info), "line %d/%d  ;.=scroll  `=back", scroll+1, lc);
            OsUI::drawFooter(_canvas, info);
            { DispLock _lk; _canvas.pushSprite(0, 0); }

            if (M5Cardputer.Keyboard.isKeyPressed(';')) { if (scroll > 0) scroll--; delay(KEY_REPEAT_MS); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { if (scroll < maxScr) scroll++; delay(KEY_REPEAT_MS); }
            // Page up/down
            if (ks.fn && M5Cardputer.Keyboard.isKeyPressed(';')) { scroll = max(0, scroll - visible); delay(150); }
            if (ks.fn && M5Cardputer.Keyboard.isKeyPressed('.')) { scroll = min(maxScr, scroll + visible); delay(150); }
            if (M5Cardputer.Keyboard.isKeyPressed('`') || ks.del) { OsUI::waitRelease(); running = false; }
            delay(5);
        }

        delete[] lines;
    }

    // General-purpose image viewer: BMP via the built-in decoder below, PNG/
    // JPG via M5GFX's own decoders (lgfx_pngle/tjpgd, already linked into
    // the firmware for icons/fonts). Zoom (+/-), 90° rotate (R), pan while
    // zoomed in (;/./,//).
    void _viewImage(const String& path, const String& lowExt) {
        float zoom = 1.0f;
        int panX = 0, panY = 0;
        int rot = 0;  // 0..3, quarter turns
        bool isBmp = lowExt.endsWith(".bmp");
        bool isPng = lowExt.endsWith(".png");
        bool isJpg = lowExt.endsWith(".jpg") || lowExt.endsWith(".jpeg");
        bool isQoi = lowExt.endsWith(".qoi");

        const int drawX = 0, drawY = CONTENT_Y + 16;
        const int availW = SCREEN_W;
        const int availH = SCREEN_H - FOOTER_H - drawY;

        bool running = true;
        bool dirty = true;
        while (running) {
            M5Cardputer.update();
            auto ks = M5Cardputer.Keyboard.keysState();

            if (dirty) {
                _canvas.fillSprite(COL_BG);
                OsUI::drawHeader(_canvas, "Image Viewer", COL_HEADER);

                LGFX_Sprite tmp(&_canvas);
                tmp.setColorDepth(16);
                tmp.setRotation(rot);
                tmp.createSprite(availW, availH);
                tmp.fillSprite(TFT_BLACK);

                if (isBmp) {
                    _drawBmpInto(tmp, path, zoom, panX, panY);
#if FM_NATIVE_IMAGE_DECODE
                } else if (isPng) {
                    tmp.drawPngFile((fs::FS&)SD, path.c_str(), panX, panY, 0, 0, 0, 0, zoom, zoom);
                } else if (isJpg) {
                    tmp.drawJpgFile((fs::FS&)SD, path.c_str(), panX, panY, 0, 0, 0, 0, zoom, zoom);
                } else if (isQoi) {
                    tmp.drawQoiFile((fs::FS&)SD, path.c_str(), panX, panY, 0, 0, 0, 0, zoom, zoom);
#else
                } else if (isPng || isJpg || isQoi) {
                    tmp.setTextColor(TFT_WHITE, TFT_BLACK);
                    tmp.drawCenterString("Format not supported on this build", tmp.width()/2, tmp.height()/2);
#endif
                }

                tmp.pushSprite(drawX, drawY);
                tmp.deleteSprite();

                char info[40];
                snprintf(info, sizeof(info), "zoom %d%%  rot %d  R r0/+/- ;.,/", (int)(zoom*100), rot*90);
                OsUI::drawFooter(_canvas, info);
                { DispLock _lk; _canvas.pushSprite(0, 0); }
                dirty = false;
            }

            if (M5Cardputer.Keyboard.isKeyPressed('+') || M5Cardputer.Keyboard.isKeyPressed('=')) {
                zoom = min(zoom + 0.1f, 4.0f); dirty = true; delay(120);
            }
            if (M5Cardputer.Keyboard.isKeyPressed('-')) {
                zoom = max(zoom - 0.1f, 0.1f); dirty = true; delay(120);
            }
            if (M5Cardputer.Keyboard.isKeyPressed('r')) {
                rot = (rot + 1) % 4; dirty = true; delay(200);
            }
            int step = ks.shift ? 20 : 6;
            if (M5Cardputer.Keyboard.isKeyPressed(';')) { panY += step; dirty = true; delay(60); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { panY -= step; dirty = true; delay(60); }
            if (M5Cardputer.Keyboard.isKeyPressed(',')) { panX += step; dirty = true; delay(60); }
            if (M5Cardputer.Keyboard.isKeyPressed('/')) { panX -= step; dirty = true; delay(60); }
            if (M5Cardputer.Keyboard.isKeyPressed('`') || ks.del) { OsUI::waitRelease(); running = false; }
            delay(10);
        }
    }

    // Manual 24bpp BMP decoder with zoom/pan support, parametrized to draw
    // into any LGFX_Sprite instead of a hardcoded target.
    void _drawBmpInto(LGFX_Sprite& tmp, const String& path, float zoom, int panX, int panY) {
        File f = SD.open(path.c_str());
        if (!f) return;
        if (f.size() < 54) { f.close(); return; }
        uint8_t hdr[54];
        f.read(hdr, 54);
        if (hdr[0] != 'B' || hdr[1] != 'M') { f.close(); return; }

        int32_t w  = *(int32_t*)(hdr + 18);
        int32_t h  = *(int32_t*)(hdr + 22);
        uint16_t bpp = *(uint16_t*)(hdr + 28);
        uint32_t offset = *(uint32_t*)(hdr + 10);
        if (bpp != 24 || w <= 0 || h <= 0) { f.close(); return; }

        int dstW = (int)(w * zoom); if (dstW < 1) dstW = 1;
        int dstH = (int)(h * zoom); if (dstH < 1) dstH = 1;
        int rowSize = ((w * 3 + 3) & ~3);
        uint8_t* row = new uint8_t[rowSize];
        if (!row) { f.close(); return; }

        int lastSrcY = -1;
        for (int dy = 0; dy < dstH; dy++) {
            int sy = panY + dy;
            if (sy < 0 || sy >= tmp.height()) continue;
            int origY = (int)(dy / zoom); if (origY >= h) origY = h - 1;
            int fileRow = h - 1 - origY;
            if (fileRow != lastSrcY) {
                f.seek(offset + (uint32_t)fileRow * rowSize);
                f.read(row, rowSize);
                lastSrcY = fileRow;
            }
            for (int dx = 0; dx < dstW; dx++) {
                int sx = panX + dx;
                if (sx < 0 || sx >= tmp.width()) continue;
                int origX = (int)(dx / zoom); if (origX >= w) origX = w - 1;
                uint8_t b = row[origX*3], g = row[origX*3+1], r = row[origX*3+2];
                uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                tmp.drawPixel(sx, sy, c);
            }
        }
        delete[] row;
        f.close();
    }

    void _showToast(const char* msg) {
        OsUI::showToast(_canvas, msg, COL_WARN);
        { DispLock _lk; _canvas.pushSprite(0, 0); } delay(800);
    }

    void _showInfo(const String& path) {
        _canvas.fillSprite(COL_BG);
        OsUI::drawHeader(_canvas, "File Info", COL_HEADER);

        _canvas.setTextSize(1);
        int y = CONTENT_Y + 6;
        auto row = [&](const char* label, const String& val) {
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.setCursor(4, y);
            _canvas.print(label);
            _canvas.setTextColor(COL_TEXT, COL_BG);
            _canvas.setCursor(74, y);
            String v = val;
            if (v.length() > 22) v = v.substring(0, 21) + "~";
            _canvas.print(v);
            y += 14;
        };

        String name = path.substring(path.lastIndexOf('/') + 1);
        row("Name:", name);

        File f = SD.open(path.c_str());
        if (f) {
            if (f.isDirectory()) {
                // Count folder contents
                int cnt = 0;
                File c = f.openNextFile();
                while (c) { cnt++; c = f.openNextFile(); }
                row("Type:", "Directory");
                row("Items:", String(cnt));
            } else {
                uint32_t sz = f.size();
                String sizeStr;
                if (sz < 1024)         sizeStr = String(sz) + " B";
                else if (sz < 1<<20)   sizeStr = String(sz/1024) + " KB (" + String(sz) + ")";
                else                   sizeStr = String(sz>>20) + " MB";
                row("Type:", "File");
                row("Size:", sizeStr);
                // Extension
                int dot = name.lastIndexOf('.');
                if (dot >= 0) { String ext = name.substring(dot + 1); ext.toUpperCase(); row("Ext:", ext); }
            }
            f.close();
        } else {
            row("Error:", "Cannot open");
        }
        row("Path:", _path);

        if (_clipPath != "") {
            String clipName = _clipPath.substring(_clipPath.lastIndexOf('/') + 1);
            row(_clipMove ? "Cut:" : "Copied:", clipName);
        }

        // SD info
        uint64_t total = SD.totalBytes() / (1024*1024);
        uint64_t used  = SD.usedBytes()  / (1024*1024);
        char sdBuf[24];
        snprintf(sdBuf, sizeof(sdBuf), "%lluMB / %lluMB", used, total);
        row("SD:", sdBuf);

        OsUI::drawFooter(_canvas, "Any key = back");
        { DispLock _lk; _canvas.pushSprite(0, 0); }
        OsUI::waitRelease();
        while (true) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isPressed()) { OsUI::waitRelease(); return; }
            delay(10);
        }
    }

    void _draw() {
        _canvas.startWrite();
        _canvas.fillSprite(COL_BG);

        uint16_t hcol = (SD.cardType() == CARD_NONE)
            ? (uint16_t)COL_HEADER_BAD : (uint16_t)COL_HEADER;
        OsUI::drawHeader(_canvas, "File Explorer", hcol);

        // Address bar (like a file explorer)
        _canvas.fillRect(0, HEADER_H, SCREEN_W, 14, COL_BG2);
        _canvas.drawRect(0, HEADER_H, SCREEN_W, 14, COL_DIM);
        String p = _path;
        if (p.length() > 36) p = "..." + p.substring(p.length() - 33);
        _canvas.setTextColor(COL_TEXT, COL_BG2);
        _canvas.setTextSize(1);
        _canvas.setCursor(4, HEADER_H + 3);
        _canvas.print(p);
        if (_clipPath != "") {
            _canvas.setTextColor(COL_WARN, COL_BG2);
            _canvas.print(_clipMove ? "  [cut]" : "  [copy]");
        }

        const int LIST_Y = CONTENT_Y + 14;
        _canvas.setTextSize(1);

        if (SD.cardType() == CARD_NONE) {
            _canvas.setTextColor(COL_ERR, COL_BG);
            _canvas.drawCenterString("No SD card!", 120, 55);
        } else if (_count == 0) {
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.drawCenterString("Empty directory", 120, 55);
        } else {
            for (int i = 0; i < FM_VISIBLE; i++) {
                int idx = _scroll + i;
                if (idx >= _count) break;
                int y = LIST_Y + 2 + i * 15;
                bool sel = (idx == _sel);

                if (sel) {
                    _canvas.fillRect(0, y - 1, SCREEN_W, 14, COL_SELECTED);
                    _canvas.fillRect(0, y - 1, 3, 14, COL_ACCENT);
                    _canvas.setTextColor(COL_BG, COL_SELECTED);
                } else {
                    _canvas.setTextColor(
                        _isDir[idx] ? (uint16_t)COL_ACCENT : (uint16_t)COL_TEXT,
                        COL_BG);
                }

                _canvas.setCursor(8, y);

                if (_isParentEntry(idx)) {
                    _canvas.print("..");
                } else if (_isDir[idx]) {
                    _canvas.print("[D]");
                } else {
                    String n = String(_names[idx]); n.toLowerCase();
                    FileTypeInfo ft = fmClassify(n);
                    _canvas.printf("[%s]", ft.icon);
                }

                // Name (truncated if long)
                char namebuf[26];
                strncpy(namebuf, _names[idx], 25); namebuf[25] = '\0';
                _canvas.print(" ");
                _canvas.print(namebuf);

                // Size on the right
                if (!_isDir[idx]) {
                    uint32_t sz = _sizes[idx];
                    char szBuf[10];
                    if (sz < 1024)          snprintf(szBuf, 10, "%uB",  (unsigned)sz);
                    else if (sz < 1<<20)    snprintf(szBuf, 10, "%uK",  (unsigned)(sz>>10));
                    else                    snprintf(szBuf, 10, "%uM",  (unsigned)(sz>>20));
                    uint16_t dimC = sel ? (uint16_t)COL_BG : (uint16_t)COL_DIM;
                    _canvas.setTextColor(dimC, sel ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG);
                    int sw = strlen(szBuf) * 6 + 4;
                    _canvas.setCursor(SCREEN_W - sw - 6, y);
                    _canvas.print(szBuf);
                }
            }

            // Scrollbar
            if (_count > FM_VISIBLE) {
                int barH = max(4, (FM_VISIBLE * FM_VISIBLE * 15) / _count);
                int barY = LIST_Y + (_scroll * FM_VISIBLE * 15) / _count;
                _canvas.fillRect(SCREEN_W - 3, LIST_Y, 3, FM_VISIBLE * 15, COL_BG2);
                _canvas.fillRect(SCREEN_W - 3, barY, 3, barH, COL_ACCENT);
            }
        }

        if (_count > 0) {
            char cntBuf[10];
            snprintf(cntBuf, sizeof(cntBuf), "%d/%d", _sel + 1, _count);
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.setTextDatum(top_right);
            _canvas.drawString(cntBuf, SCREEN_W - 6, LIST_Y);
            _canvas.setTextDatum(top_left);
        }

        switch (_hintPage) {
        case 0:
            OsUI::drawFooter(_canvas, ";/. ENT=open BS/DEL=up D=del");
            break;
        case 1:
            OsUI::drawFooter(_canvas, "R=ren N=folder C/X/P clipboard");
            break;
        case 2:
            OsUI::drawFooter(_canvas, "M=menu F=find Tab=hints `=home");
            break;
        }

        { DispLock _lk; _canvas.pushSprite(0, 0); }
        _canvas.endWrite();
    }
};
