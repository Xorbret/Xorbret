#pragma once
#include <M5Cardputer.h>
#include <SD.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "../../system/tsukumo.h"
#include <Preferences.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorWAV.h>
#include <freertos/FreeRTOS.h>
#include "../../os/disp_lock.h"
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <vector>
#include <string>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/wifi_manager.h"
#include "../../config.h"
#include "../../system/paper_clock.h"
#include "../../libs/AudioOutputM5Speaker.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

// Lua memory cap. Without PSRAM the whole system shares one ~320KB heap;
// a runaway script (e.g. building an ever-growing table in a loop) could
// otherwise starve every other app and service of RAM. See
// lua_limited_alloc() further down for how this is enforced.
#define LUA_HEAP_LIMIT_BYTES (96 * 1024)

// Bytes currently allocated by Lua's sandboxed heap. Defined here (rather
// than next to lua_limited_alloc further down) so functions registered
// earlier in the file (e.g. os.luamem()) can read it directly.
static size_t g_lua_bytes_used = 0;
}

// Set true right when lua_core1_task starts, false right before it calls
// lua_close()+vTaskDelete(). onStart() waits on this (not a blind timeout)
// before spawning a new task, so two Lua tasks never touch the shared
// g_lstate/g_lua_bytes_used globals at the same time.
static volatile bool g_luaTaskAlive = false;

#define LT_LINES    40
#define LT_VISIBLE   6
#define LT_LINE_H   16
#define LT_MAX_IN   128
#define LT_HIST     32
#define LT_LINE_W   39


struct LuaTerm {
    String   lines[LT_LINES];
    uint16_t colors[LT_LINES];
    int      count  = 0;
    int      scroll = 0;
    SemaphoreHandle_t mtx = nullptr;

    void init()  { mtx = xSemaphoreCreateMutex(); }
    void lock()  { if (mtx) xSemaphoreTake(mtx, portMAX_DELAY); }
    void unlock(){ if (mtx) xSemaphoreGive(mtx); }

    void push(const String& s, uint16_t col = 0) {
        lock();
        String buf = s;
        buf.replace("\r\n", "\n"); buf.replace("\r", "\n");
        int start = 0;
        while (start <= (int)buf.length()) {
            int nl = buf.indexOf('\n', start);
            if (nl < 0) nl = buf.length();
            String chunk = buf.substring(start, nl);
            while ((int)chunk.length() > LT_LINE_W) {
                int cut = LT_LINE_W;
                for (int k = LT_LINE_W-1; k > LT_LINE_W/2; k--)
                    if (chunk[k] == ' ') { cut = k; break; }
                _push(chunk.substring(0, cut), col);
                chunk = chunk.substring(cut + (chunk[cut]==' ' ? 1 : 0));
            }
            _push(chunk, col);
            start = nl + 1;
        }
        scroll = (count > LT_VISIBLE) ? count - LT_VISIBLE : 0;
        unlock();
    }

    void clear() {
        lock();
        count = 0; scroll = 0;
        for (int i = 0; i < LT_LINES; i++) { lines[i] = ""; colors[i] = 0; }
        unlock();
    }

    void scrollUp()   { if (scroll > 0) scroll--; }
    void scrollDown() { if (scroll + LT_VISIBLE < count) scroll++; }

private:
    void _push(const String& s, uint16_t col) {
        if (count >= LT_LINES) {
            // drop oldest half to free space
            int keep = LT_LINES / 2;
            int drop = count - keep;
            for (int i = 0; i < keep; i++) { lines[i] = lines[i+drop]; colors[i] = colors[i+drop]; }
            for (int i = keep; i < LT_LINES; i++) { lines[i] = ""; colors[i] = 0; }
            count = keep;
            if (scroll >= drop) scroll -= drop; else scroll = 0;
        }
        lines[count]  = s;
        colors[count] = col;
        count++;
    }
} g_lt;


struct LuaExecState {
    volatile bool   pending   = false;   // core0 sets: new command ready
    volatile bool   running   = false;   // core1 sets: currently executing
    volatile bool   interrupt = false;   // core0 sets: Ctrl+C kill signal
    volatile bool   quit      = false;   // core0 sets: app closing
    String          cmd;
    SemaphoreHandle_t cmdMtx  = nullptr;
    void init() { cmdMtx = xSemaphoreCreateMutex(); }
} g_exec;

// ── New scripting surface: graphics/input/sound/ui/store/json/timers ──
// Set by LuaTermApp's constructor so free C functions (Lua bindings) can
// draw to the same sprite the app already owns and pushes to the LCD.
static LGFX_Sprite* g_lcanvas = nullptr;

// While a script is driving the screen itself (via disp.* + loop()), the
// REPL's own per-tick chrome redraw (see LuaTermApp::update()) must stay
// out of the way instead of painting over it on every keypress. Set true
// for the duration of loop()/any disp.* draw call, cleared (with a forced
// one-shot redraw to restore the terminal view) when the script returns.
static volatile bool g_lua_gfx_active   = false;
static volatile bool g_lua_force_redraw = false;

// key.pressed()/key.any() read live hardware state directly (always
// correct). key.justPressed()/key.released() need an edge between two
// snapshots; that snapshot is advanced once per loop() tick (or manually
// via key.poll() for scripts driving their own loop). Named keys plus
// a-z/0-9 share this small fixed table, indexed by ASCII code.
static bool g_key_cur[128]  = {false};
static bool g_key_prev[128] = {false};

static int _lua_key_slot(const String& nameLower) {
    if (nameLower == "up")                       return ';';
    if (nameLower == "down")                      return '.';
    if (nameLower == "left")                      return ',';
    if (nameLower == "right")                     return '/';
    if (nameLower == "enter")                     return '\n';
    if (nameLower == "tab")                       return '\t';
    if (nameLower == "del" || nameLower == "backspace") return 8;
    if (nameLower == "ctrl")                      return 17;
    if (nameLower == "shift")                     return 18;
    if (nameLower == "fn")                        return 19;
    if (nameLower == "space")                     return ' ';
    if (nameLower == "esc"  || nameLower == "back")     return '`';
    if (nameLower.length() == 1) return (int)(unsigned char)tolower(nameLower[0]);
    return -1;
}

static void _lua_key_poll() {
    memcpy(g_key_prev, g_key_cur, sizeof(g_key_cur));
    memset(g_key_cur, 0, sizeof(g_key_cur));
    auto ks = M5Cardputer.Keyboard.keysState();
    for (char c : ks.word) {
        unsigned char idx = (unsigned char)tolower(c);
        if (idx < 128) g_key_cur[idx] = true;
    }
    if (ks.enter) g_key_cur[(int)'\n'] = true;
    if (ks.tab)   g_key_cur[(int)'\t'] = true;
    if (ks.del)   g_key_cur[8]  = true;
    if (ks.ctrl)  g_key_cur[17] = true;
    if (ks.shift) g_key_cur[18] = true;
    if (ks.fn)    g_key_cur[19] = true;
    if (M5Cardputer.Keyboard.isKeyPressed(';')) g_key_cur[(int)';'] = true;
    if (M5Cardputer.Keyboard.isKeyPressed('.')) g_key_cur[(int)'.'] = true;
    if (M5Cardputer.Keyboard.isKeyPressed(',')) g_key_cur[(int)','] = true;
    if (M5Cardputer.Keyboard.isKeyPressed('/')) g_key_cur[(int)'/'] = true;
    if (M5Cardputer.Keyboard.isKeyPressed('`')) g_key_cur[(int)'`'] = true;
}

static bool _lua_key_held(const String& nameLower) {
    if (nameLower == "up")    return M5Cardputer.Keyboard.isKeyPressed(';');
    if (nameLower == "down")  return M5Cardputer.Keyboard.isKeyPressed('.');
    if (nameLower == "left")  return M5Cardputer.Keyboard.isKeyPressed(',');
    if (nameLower == "right") return M5Cardputer.Keyboard.isKeyPressed('/');
    if (nameLower == "space") return M5Cardputer.Keyboard.isKeyPressed(' ');
    if (nameLower == "esc" || nameLower == "back") return M5Cardputer.Keyboard.isKeyPressed('`');
    auto ks = M5Cardputer.Keyboard.keysState();
    if (nameLower == "enter") return ks.enter;
    if (nameLower == "tab")   return ks.tab;
    if (nameLower == "del" || nameLower == "backspace") return ks.del;
    if (nameLower == "ctrl")  return ks.ctrl;
    if (nameLower == "shift") return ks.shift;
    if (nameLower == "fn")    return ks.fn;
    if (nameLower.length() == 1) return M5Cardputer.Keyboard.isKeyPressed(nameLower[0]);
    return false;
}

// timer.every()/timer.after() — only ticked from inside loop() (documented
// to the user), since Lua isn't safe to re-enter from anywhere else.
struct LuaTimer { uint32_t next; uint32_t interval; bool repeat; int ref; bool alive; };
static std::vector<LuaTimer> g_timers;

static void _lua_timers_clear(lua_State* L) {
    for (auto& t : g_timers) if (t.alive) luaL_unref(L, LUA_REGISTRYINDEX, t.ref);
    g_timers.clear();
}

static void _lua_timers_tick(lua_State* L) {
    uint32_t now = millis();
    // Iterate by index (not range-for) because a timer callback can itself
    // call timer.every()/timer.after() which push_back() onto g_timers —
    // that reallocation invalidates any range-for reference/iterator into
    // the vector, causing undefined behaviour/crashes. Index-based iteration
    // is safe: we snapshot the size before the loop and only visit timers
    // that existed at tick-start; newly added ones are picked up next tick.
    int n = (int)g_timers.size();
    for (int i = 0; i < n; i++) {
        if (!g_timers[i].alive || now < g_timers[i].next) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, g_timers[i].ref);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) lua_pop(L, 1); // one bad timer shouldn't kill the whole loop
        // re-read g_timers[i] after pcall — vector may have reallocated if
        // the callback called timer.every()/after(), but the INDEX is stable
        if (g_timers[i].repeat) g_timers[i].next = millis() + g_timers[i].interval;
        else { luaL_unref(L, LUA_REGISTRYINDEX, g_timers[i].ref); g_timers[i].alive = false; }
    }
}

// store.* — small persistent key/value scratchpad for scripts (Preferences/NVS).
static Preferences g_luaPrefs;

// sound.play() — WAV only for now (see chat note): its own speaker channel
// so it can't collide with the Music app's own channel/decode task.
static AudioOutputM5Speaker _luaSndOut(&M5.Speaker, 1);

static File _fsOpen(const char* path, const char* mode = "r") {
    if (strncmp(path, "/sd", 3) == 0 || strncmp(path, "/SD", 3) == 0) {
        const char* p = (strlen(path) > 3) ? path + 3 : "/";
        if (!p[0]) p = "/";
        return SD.open(p, mode);
    }
    return LittleFS.open(path, mode);
}
static bool _fsExists(const char* p) { return (strncmp(p,"/sd",3)==0||strncmp(p,"/SD",3)==0) ? SD.exists(p+3) : LittleFS.exists(p); }
static bool _fsRemove(const char* p) { return (strncmp(p,"/sd",3)==0||strncmp(p,"/SD",3)==0) ? SD.remove(p+3) : LittleFS.remove(p); }
static bool _fsMkdir(const char*  p) { return (strncmp(p,"/sd",3)==0||strncmp(p,"/SD",3)==0) ? SD.mkdir(p+3)  : LittleFS.mkdir(p); }
static bool _fsRename(const char* from, const char* to) {
    bool fromSd = (strncmp(from,"/sd",3)==0 || strncmp(from,"/SD",3)==0);
    bool toSd   = (strncmp(to,  "/sd",3)==0 || strncmp(to,  "/SD",3)==0);
    if (fromSd) return SD.rename(from+3, toSd ? to+3 : to);
    return LittleFS.rename(from, to);
}

static String _luaValToStr(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    if (t == LUA_TNIL)     return "nil";
    if (t == LUA_TBOOLEAN) return lua_toboolean(L, idx) ? "true" : "false";
    size_t len = 0;
    const char* s = luaL_tolstring(L, idx, &len);
    String r = s ? String(s) : (String("<") + lua_typename(L, t) + ">");
    lua_pop(L, 1);
    return r;
}

static void lua_interrupt_hook(lua_State* L, lua_Debug*) {
    if (g_exec.interrupt) {
        g_exec.interrupt = false;
        luaL_error(L, "interrupted");
    }
}


static int lb_print(lua_State* L) {
    int n = lua_gettop(L); String out;
    for (int i = 1; i <= n; i++) { if (i>1) out+="\t"; out+=_luaValToStr(L,i); }
    g_lt.push(out, COL_TEXT); return 0;
}
// sleep() used to be a single vTaskDelay(), which is NOT Lua bytecode, so
// the instruction-count hook (lua_interrupt_hook) never got a chance to run
// until the delay had *already* fully elapsed — Ctrl+C during sleep(5000)
// would do nothing for 5 seconds and then report "interrupted" right after
// the sleep had finished anyway. Chunk the delay and poll the flag
// ourselves so a Ctrl+C actually cuts the sleep short within ~20ms.
// sleep(ms) — PREVIOUSLY: timer.every()/timer.after() only ever fired
// from inside loop(), because that was the only place _lua_timers_tick()
// was called. A script doing its own `while true do ... sleep(50) end`
// instead of loop() would see its timers simply never fire — confusing,
// and only documented as a caveat rather than fixed. sleep() already
// wakes up every 20ms anyway (to check for Ctrl+C), so it costs nothing
// extra to also tick due timers on each of those wake-ups.
static int lb_sleep(lua_State* L) {
    long remaining = luaL_optinteger(L, 1, 0);
    const long CHUNK_MS = 20;
    while (remaining > 0) {
        if (g_exec.interrupt) { g_exec.interrupt = false; luaL_error(L, "interrupted"); }
        long d = (remaining < CHUNK_MS) ? remaining : CHUNK_MS;
        vTaskDelay(pdMS_TO_TICKS(d));
        _lua_timers_tick(L);
        remaining -= d;
    }
    return 0;
}

// io.*
struct LuaFile { File f; bool open=false; };
static const char* LUA_FILE_MT = "PaperOS.File";
static LuaFile* _checkFile(lua_State* L, int idx=1) {
    LuaFile* lf = (LuaFile*)luaL_checkudata(L, idx, LUA_FILE_MT);
    if (!lf->open) luaL_error(L, "attempt to use a closed file");
    return lf;
}
static int lb_io_open(lua_State* L) {
    const char* path = luaL_checkstring(L,1);
    const char* mode = luaL_optstring(L,2,"r");
    const char* fm = FILE_READ; bool w=false;
    if (strchr(mode,'w')) { fm=FILE_WRITE;  w=true; }
    if (strchr(mode,'a')) { fm=FILE_APPEND; w=true; }
    LuaFile* lf = (LuaFile*)lua_newuserdata(L, sizeof(LuaFile));
    new (lf) LuaFile(); luaL_setmetatable(L, LUA_FILE_MT);
    lf->f = _fsOpen(path, fm);
    if (!lf->f) { lua_pushnil(L); lua_pushstring(L,"cannot open file"); return 2; }
    lf->open = true; (void)w; return 1;
}
static int lb_file_read(lua_State* L) {
    LuaFile* lf = _checkFile(L);
    const char* fmt = luaL_optstring(L,2,"*l");
    if (strcmp(fmt,"*a")==0||strcmp(fmt,"a")==0) {
        String s; while(lf->f.available()) s+=(char)lf->f.read();
        lua_pushstring(L,s.c_str()); return 1;
    }
    if (!lf->f.available()) { lua_pushnil(L); return 1; }
    String line;
    while(lf->f.available()) { char c=lf->f.read(); if(c=='\n') break; if(c!='\r') line+=c; }
    lua_pushstring(L,line.c_str()); return 1;
}
static int lb_file_write(lua_State* L) {
    LuaFile* lf = _checkFile(L); int n=lua_gettop(L);
    for(int i=2;i<=n;i++){size_t l;const char* s=luaL_tolstring(L,i,&l);lf->f.write((const uint8_t*)s,l);lua_pop(L,1);}
    lua_pushvalue(L,1); return 1;
}
static int lb_file_lines(lua_State* L) {
    _checkFile(L); lua_pushvalue(L,1);
    lua_pushcclosure(L,[](lua_State* L2)->int{
        LuaFile* lf2=(LuaFile*)luaL_checkudata(L2,lua_upvalueindex(1),LUA_FILE_MT);
        if(!lf2->open||!lf2->f.available()){lua_pushnil(L2);return 1;}
        String line; while(lf2->f.available()){char c=lf2->f.read();if(c=='\n')break;if(c!='\r')line+=c;}
        lua_pushstring(L2,line.c_str());return 1;
    },1); return 1;
}
static int lb_file_seek(lua_State* L) {
    LuaFile* lf=_checkFile(L); const char* w=luaL_optstring(L,2,"cur"); long off=luaL_optinteger(L,3,0);
    size_t pos=0;
    if(strcmp(w,"set")==0) pos=lf->f.seek(off,SeekSet)?lf->f.position():0;
    else if(strcmp(w,"end")==0) pos=lf->f.seek(off,SeekEnd)?lf->f.position():0;
    else pos=lf->f.seek(off,SeekCur)?lf->f.position():0;
    lua_pushinteger(L,pos); return 1;
}
static int lb_file_flush(lua_State* L) { _checkFile(L)->f.flush(); lua_pushvalue(L,1); return 1; }
static int lb_file_close(lua_State* L) {
    LuaFile* lf=(LuaFile*)luaL_checkudata(L,1,LUA_FILE_MT);
    if(lf->open){lf->f.close();lf->open=false;}
    lua_pushboolean(L,true); return 1;
}
static int lb_file_gc(lua_State* L) {
    LuaFile* lf=(LuaFile*)luaL_checkudata(L,1,LUA_FILE_MT);
    if(lf->open){lf->f.close();lf->open=false;} lf->~LuaFile(); return 0;
}
static int lb_io_lines(lua_State* L) {
    const char* path=luaL_checkstring(L,1);
    LuaFile* lf=(LuaFile*)lua_newuserdata(L,sizeof(LuaFile));
    new(lf)LuaFile(); luaL_setmetatable(L,LUA_FILE_MT);
    lf->f=_fsOpen(path,"r"); lf->open=lf->f?true:false;
    if(!lf->open) luaL_error(L,"cannot open '%s'",path);
    lua_pushcclosure(L,[](lua_State* L2)->int{
        LuaFile* lf2=(LuaFile*)luaL_checkudata(L2,lua_upvalueindex(1),LUA_FILE_MT);
        if(!lf2->open||!lf2->f.available()){if(lf2->open){lf2->f.close();lf2->open=false;}lua_pushnil(L2);return 1;}
        String line; while(lf2->f.available()){char c=lf2->f.read();if(c=='\n')break;if(c!='\r')line+=c;}
        lua_pushstring(L2,line.c_str());return 1;
    },1); return 1;
}
static int lb_io_write(lua_State* L) {
    int n=lua_gettop(L); String out;
    for(int i=1;i<=n;i++){size_t l;const char* s=luaL_tolstring(L,i,&l);out+=s;lua_pop(L,1);}
    g_lt.push(out,COL_TEXT); return 0;
}
static int lb_io_read(lua_State* L) { lua_pushnil(L); return 1; }

// os.*
// os.launch() is async: returns true if the app name was found and the
// switch was queued, not whether it actually started yet (the real switch
// happens on core0 on the next tick, since switching apps is not safe to
// do from core1 where Lua code runs).
static int lb_os_launch(lua_State* L)    { lua_pushboolean(L,AppManager::get().requestLaunch(luaL_checkstring(L,1))); return 1; }
static int lb_os_home(lua_State* L)      { AppManager::get().requestHome(); return 0; }
static int lb_os_heap(lua_State* L)      { lua_pushinteger(L,ESP.getFreeHeap()); return 1; }
// os.luamem() -> bytes used, byte limit. Lua's own sandboxed heap (see
// lua_limited_alloc/LUA_HEAP_LIMIT_BYTES below), separate from os.heap()
// which reports the whole board's free heap.
static int lb_os_luamem(lua_State* L)    { lua_pushinteger(L,(lua_Integer)g_lua_bytes_used);
                                            lua_pushinteger(L,LUA_HEAP_LIMIT_BYTES);
                                            return 2; }
static int lb_os_psram(lua_State* L)     { lua_pushinteger(L,ESP.getFreePsram()); return 1; }
static int lb_os_reset(lua_State* L)     { ESP.restart(); return 0; }
static int lb_os_bright(lua_State* L)    { M5Cardputer.Display.setBrightness(constrain(luaL_checkinteger(L,1),0,100)*255/100); return 0; }
static int lb_os_millis(lua_State* L)    { lua_pushinteger(L,millis()); return 1; }
static int lb_os_micros(lua_State* L)    { lua_pushinteger(L,micros()); return 1; }
static int lb_os_cpu(lua_State* L)       { lua_pushinteger(L,ESP.getCpuFreqMHz()); return 1; }
static int lb_os_flash(lua_State* L)     { lua_pushinteger(L,ESP.getFlashChipSize()); return 1; }
static int lb_os_sketchsize(lua_State* L){ lua_pushinteger(L,ESP.getSketchSize()); return 1; }
static int lb_os_clock(lua_State* L)     { lua_pushnumber(L,(lua_Number)millis()/1000.0); return 1; }
// os.time() — real Unix timestamp once NTP has synced (PaperClock, already
// used by the clock app / settings for wall-clock time), falls back to
// uptime-in-seconds if the device hasn't synced yet (offline / no Wi-Fi).
// PREVIOUSLY: always returned uptime, even when NTP time was available
// elsewhere in the system — os.date()/os.time() just weren't wired to it.
static int lb_os_time(lua_State* L) {
    if (PaperClock::isSynced()) lua_pushinteger(L,(lua_Integer)::time(nullptr));
    else                        lua_pushinteger(L,(lua_Integer)(millis()/1000));
    return 1;
}
static int lb_os_exit(lua_State* L)      { AppManager::get().requestHome(); return 0; }
static int lb_os_getenv(lua_State* L)    { lua_pushnil(L); return 1; }
static int lb_os_tmpname(lua_State* L)   { char t[32]; snprintf(t,sizeof(t),"/tmp_%lu",millis()); lua_pushstring(L,t); return 1; }
// os.date([format]) — real calendar date/time (via strftime) once NTP has
// synced; falls back to the old "uptime Ns" string if not synced yet, so
// existing scripts that don't check for a real date still get *something*
// sane instead of a garbage/garbled date. format defaults to "%c" (like
// stock Lua's os.date()), e.g. os.date("%Y-%m-%d %H:%M:%S").
static int lb_os_date(lua_State* L) {
    const char* fmt = luaL_optstring(L,1,"%c");
    struct tm timeinfo;
    if (!PaperClock::isSynced() || !getLocalTime(&timeinfo, 80)) {
        char b[32]; snprintf(b,sizeof(b),"uptime %lus",(unsigned long)(millis()/1000));
        lua_pushstring(L,b);
        return 1;
    }
    char buf[128];
    if (strftime(buf, sizeof(buf), fmt, &timeinfo) == 0) buf[0] = '\0'; // strftime returns 0 if result doesn't fit; ensure null-terminated empty string rather than garbage
    lua_pushstring(L, buf);
    return 1;
}
static int lb_os_difftime(lua_State* L)  { lua_pushnumber(L,luaL_checknumber(L,1)-luaL_checknumber(L,2)); return 1; }
static int lb_os_remove(lua_State* L)    { bool ok=_fsRemove(luaL_checkstring(L,1)); if(ok){lua_pushboolean(L,true);return 1;} lua_pushnil(L);lua_pushstring(L,"remove failed");return 2; }
static int lb_os_rename(lua_State* L)    { bool ok=_fsRename(luaL_checkstring(L,1),luaL_checkstring(L,2)); if(ok){lua_pushboolean(L,true);return 1;} lua_pushnil(L);lua_pushstring(L,"rename failed");return 2; }
static int lb_os_apps(lua_State* L) {
    lua_newtable(L); int n=AppManager::get().getCount();
    for(int i=0;i<n;i++){App* a=AppManager::get().getApp(i);if(a){lua_pushstring(L,a->getName());lua_rawseti(L,-2,i+1);}}
    return 1;
}
// os.core() — returns current FreeRTOS core id
static int lb_os_core(lua_State* L) { lua_pushinteger(L, xPortGetCoreID()); return 1; }

// fs.*
static int lb_fs_ls(lua_State* L) {
    const char* path=luaL_optstring(L,1,"/sd"); bool asList=lua_toboolean(L,2);
    File dir=_fsOpen(path);
    if(!dir||!dir.isDirectory()){if(asList){lua_newtable(L);return 1;}g_lt.push(String("ls: cannot open ")+path,COL_ERR);return 0;}
    if(asList){
        lua_newtable(L); int idx=1; File f=dir.openNextFile();
        while(f){lua_newtable(L);lua_pushstring(L,f.name());lua_setfield(L,-2,"name");lua_pushboolean(L,f.isDirectory());lua_setfield(L,-2,"dir");lua_pushinteger(L,f.isDirectory()?0:(lua_Integer)f.size());lua_setfield(L,-2,"size");lua_rawseti(L,-2,idx++);f=dir.openNextFile();}
        return 1;
    }
    String out; int cnt=0; File f=dir.openNextFile();
    while(f){out+=f.isDirectory()?"[D] ":"    ";out+=f.name();if(!f.isDirectory()){out+="  ";out+=f.size();out+="B";}out+="\n";f=dir.openNextFile();cnt++;}
    g_lt.push(cnt?out:"(empty)",COL_DIM); return 0;
}
static int lb_fs_read(lua_State* L) {
    File f=_fsOpen(luaL_checkstring(L,1)); if(!f){lua_pushnil(L);lua_pushstring(L,"not found");return 2;}
    int maxb=luaL_optinteger(L,2,8192); String out; int n=0;
    while(f.available()&&n<maxb){out+=(char)f.read();n++;} f.close();
    lua_pushstring(L,out.c_str()); return 1;
}
static int lb_fs_write(lua_State* L) {
    File f=_fsOpen(luaL_checkstring(L,1),lua_toboolean(L,3)?FILE_APPEND:FILE_WRITE);
    if(!f){lua_pushboolean(L,false);lua_pushstring(L,"open failed");return 2;}
    size_t w=f.print(luaL_checkstring(L,2)); f.close();
    lua_pushboolean(L,true); lua_pushinteger(L,w); return 2;
}
static int lb_fs_rm(lua_State* L)    { lua_pushboolean(L,_fsRemove(luaL_checkstring(L,1))); return 1; }
static int lb_fs_mkdir(lua_State* L) { lua_pushboolean(L,_fsMkdir(luaL_checkstring(L,1)));  return 1; }
static int lb_fs_exists(lua_State* L){ lua_pushboolean(L,_fsExists(luaL_checkstring(L,1))); return 1; }
static int lb_fs_size(lua_State* L)  { File f=_fsOpen(luaL_checkstring(L,1)); if(!f){lua_pushinteger(L,-1);return 1;} size_t s=f.size();f.close();lua_pushinteger(L,s);return 1; }
static int lb_fs_rename(lua_State* L){ lua_pushboolean(L,_fsRename(luaL_checkstring(L,1),luaL_checkstring(L,2))); return 1; }
static int lb_fs_copy(lua_State* L) {
    File in=_fsOpen(luaL_checkstring(L,1),"r");
    if(!in){lua_pushboolean(L,false);lua_pushstring(L,"cannot open source");return 2;}
    File out=_fsOpen(luaL_checkstring(L,2),FILE_WRITE);
    if(!out){in.close();lua_pushboolean(L,false);lua_pushstring(L,"cannot open dest");return 2;}
    uint8_t buf[256]; size_t n; bool ok=true;
    while((n=in.read(buf,sizeof(buf)))>0){
        if(out.write(buf,n)!=n){ok=false;break;} // detect SD-full / write error
    }
    in.close(); out.close();
    if(ok){lua_pushboolean(L,true);return 1;}
    lua_pushboolean(L,false);lua_pushstring(L,"write failed (disk full?)");return 2;
}
static int lb_fs_free(lua_State* L)  {
    bool useSd = lua_gettop(L) < 1 || !lua_isstring(L,1) || strncmp(luaL_optstring(L,1,"/sd"),"/sd",3)==0;
    if (useSd) lua_pushinteger(L,(lua_Integer)(SD.totalBytes()-SD.usedBytes()));
    else       lua_pushinteger(L,(lua_Integer)(LittleFS.totalBytes()-LittleFS.usedBytes()));
    return 1;
}
static int lb_fs_total(lua_State* L) {
    bool useSd = lua_gettop(L) < 1 || !lua_isstring(L,1) || strncmp(luaL_optstring(L,1,"/sd"),"/sd",3)==0;
    if (useSd) lua_pushinteger(L,(lua_Integer)SD.totalBytes());
    else       lua_pushinteger(L,(lua_Integer)LittleFS.totalBytes());
    return 1;
}

// net.*
static int lb_net_ip(lua_State* L)     { lua_pushstring(L,WiFi.localIP().toString().c_str()); return 1; }
static int lb_net_mac(lua_State* L)    { lua_pushstring(L,WiFi.macAddress().c_str()); return 1; }
static int lb_net_ssid(lua_State* L)   { lua_pushstring(L,WiFi.SSID().c_str()); return 1; }
static int lb_net_rssi(lua_State* L)   { lua_pushinteger(L,WiFi.RSSI()); return 1; }
// net.get()/net.post() used to just fail with "not connected" the moment
// WiFi wasn't already up - now that Lua turns WiFi off by default on entry
// to save the ~40-70KB its radio/buffers otherwise sit on permanently (see
// LuaTermApp::onStart()), that would make EVERY script using net.* fail
// unless the user remembered to call net.connect() by hand first. Instead,
// bring the saved network back up on first actual use, and only then -
// same helper (and same saved credentials) the rest of the OS already uses
// for background reconnect (see wifi_manager.h). A short, bounded timeout:
// long enough for a normal reconnect to an already-known AP, not so long
// that a script with no saved WiFi/no AP in range hangs for a long time
// before getting its "not connected" error.
static void _lua_net_ensure_wifi() {
    if (WiFi.status() != WL_CONNECTED) WiFiManager::get().connectFirst(4000);
}
static int lb_net_status(lua_State* L) { lua_pushboolean(L,WiFi.status()==WL_CONNECTED); return 1; }
static int lb_net_scan(lua_State* L) {
    bool asList=lua_toboolean(L,1); g_lt.push("Scanning...",COL_DIM);
    WiFi.mode(WIFI_STA);  // in case Lua turned the radio fully off (see onStart())
    // Use async scan so Ctrl+C can interrupt the wait instead of blocking
    // the whole core1 task for several seconds with no escape.
    WiFi.scanNetworks(/*async=*/true);
    // Poll until done or interrupted (~10s timeout, 200ms steps)
    int waited=0;
    while(waited < 10000) {
        int st=WiFi.scanComplete();
        if(st==WIFI_SCAN_FAILED){g_lt.push("Scan failed",COL_ERR);if(asList){lua_newtable(L);return 1;}return 0;}
        if(st>=0) break; // scan done, st = number of networks found
        if(g_exec.interrupt){g_exec.interrupt=false;WiFi.scanDelete();luaL_error(L,"interrupted");}
        vTaskDelay(pdMS_TO_TICKS(200)); waited+=200;
    }
    int n=WiFi.scanComplete();
    if(n<0){WiFi.scanDelete();g_lt.push("Scan timeout",COL_ERR);if(asList){lua_newtable(L);return 1;}return 0;}
    if(asList){lua_newtable(L);for(int i=0;i<n;i++){lua_newtable(L);lua_pushstring(L,WiFi.SSID(i).c_str());lua_setfield(L,-2,"ssid");lua_pushinteger(L,WiFi.RSSI(i));lua_setfield(L,-2,"rssi");lua_pushinteger(L,WiFi.channel(i));lua_setfield(L,-2,"ch");lua_rawseti(L,-2,i+1);}WiFi.scanDelete();return 1;}
    String out; for(int i=0;i<n&&i<10;i++){out+=WiFi.SSID(i);out+=" (";out+=WiFi.RSSI(i);out+="dB)\n";}
    WiFi.scanDelete();
    g_lt.push(n?out:"No networks",COL_TEXT); return 0;
}
static int lb_net_connect(lua_State* L) {
    WiFi.mode(WIFI_STA);  // in case Lua turned the radio fully off (see onStart())
    WiFi.begin(luaL_checkstring(L,1),luaL_optstring(L,2,""));
    int t=0;
    while(WiFi.status()!=WL_CONNECTED&&t<20){
        if (g_exec.interrupt) { g_exec.interrupt=false; luaL_error(L,"interrupted"); }
        vTaskDelay(pdMS_TO_TICKS(500));t++;
    }
    lua_pushboolean(L,WiFi.status()==WL_CONNECTED);
    lua_pushstring(L,WiFi.status()==WL_CONNECTED?WiFi.localIP().toString().c_str():"timeout");
    return 2;
}
static int lb_net_disconnect(lua_State* L) { WiFi.disconnect(); return 0; }
// net.get()/net.post() block inside HTTPClient for the whole request, which
// the instruction-count hook can't see into (same issue as old sleep()).
// A bounded timeout at least guarantees Ctrl+C-less recovery instead of an
// indefinite hang on a dead/slow server; a quick pre-check honors an
// already-pending Ctrl+C so mashing it before the call still works.
// Picks a plain WiFiClient for http:// and a WiFiClientSecure (TLS, no cert
// pinning - setInsecure(), same trust model the browser app already uses)
// for https://. Needed because raw.githubusercontent.com (and virtually
// every real API/CDN today) is https-only; without this, net.get()/post()
// could only ever reach unencrypted, mostly-dead http:// endpoints.
static bool _lua_http_begin(HTTPClient& http, WiFiClientSecure& secureClient, WiFiClient& plainClient, const String& url, String& errOut) {
    bool isHttps = url.startsWith("https://");
    if (isHttps) {
        // ESP32's mbedTLS-based TLS stack needs ~45KB+ free heap for a
        // handshake (cert parsing + fixed 16KB rx/tx session buffers -
        // that buffer size is baked into the prebuilt framework library
        // on this build setup, there's no runtime knob for it here).
        // This board has no PSRAM and often only 30-40KB free with SD +
        // display active, so this fails more often than not - that's a
        // hardware/framework ceiling, not a logic bug. Check up front so
        // the error at least says why, instead of a bare "connection
        // refused" that looks like a network problem.
        uint32_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < 45000) {
            errOut = "low memory for TLS (need ~45K free, have " + String(freeHeap/1024) + "K) - use the Store app for HTTPS instead, it has more headroom";
            return false;
        }
        secureClient.setInsecure();
    }
    WiFiClient& client = isHttps ? static_cast<WiFiClient&>(secureClient) : plainClient;
    client.setTimeout(8000); // ms, not seconds -- was "8" (8ms), which always
                              // killed the TLS handshake before it could
                              // finish, showing up as "connection refused"
                              // on https:// while http:// kept working fine
    return http.begin(client, url);
}
static int lb_net_get(lua_State* L) {
    if (g_exec.interrupt) { g_exec.interrupt=false; luaL_error(L,"interrupted"); }
    _lua_net_ensure_wifi();
    if(WiFi.status()!=WL_CONNECTED){lua_pushnil(L);lua_pushstring(L,"not connected");return 2;}
    String url = luaL_checkstring(L,1);
    WiFiClientSecure secureClient; WiFiClient plainClient;
    HTTPClient http; http.setTimeout(8000);
    String beginErr;
    if (!_lua_http_begin(http, secureClient, plainClient, url, beginErr)) { lua_pushnil(L); lua_pushstring(L,beginErr.length()?beginErr.c_str():"begin failed"); return 2; }
    int code=http.GET();
    if(code>0){lua_pushinteger(L,code);lua_pushstring(L,http.getString().c_str());}
    else{lua_pushnil(L);lua_pushstring(L,http.errorToString(code).c_str());}
    http.end(); return 2;
}
static int lb_net_post(lua_State* L) {
    if (g_exec.interrupt) { g_exec.interrupt=false; luaL_error(L,"interrupted"); }
    _lua_net_ensure_wifi();
    if(WiFi.status()!=WL_CONNECTED){lua_pushnil(L);lua_pushstring(L,"not connected");return 2;}
    String url = luaL_checkstring(L,1);
    WiFiClientSecure secureClient; WiFiClient plainClient;
    HTTPClient http; http.setTimeout(8000);
    String beginErr;
    if (!_lua_http_begin(http, secureClient, plainClient, url, beginErr)) { lua_pushnil(L); lua_pushstring(L,beginErr.length()?beginErr.c_str():"begin failed"); return 2; }
    http.addHeader("Content-Type",luaL_optstring(L,3,"application/json"));
    int code=http.POST(luaL_optstring(L,2,""));
    if(code>0){lua_pushinteger(L,code);lua_pushstring(L,http.getString().c_str());}
    else{lua_pushnil(L);lua_pushstring(L,http.errorToString(code).c_str());}
    http.end(); return 2;
}

// gpio.*
// Pins the Cardputer v1.1 board uses internally, per M5Stack's official
// pinmap (Stamp-S3A). Letting Lua scripts pinMode()/digitalWrite() these
// can corrupt the keyboard matrix scan, SD SPI bus, display, or audio.
// NOTE: G1/G2 are the Grove port (HY2.0-4P) — genuinely free GPIO with
// nothing else on the board wired to them, so they are intentionally
// NOT in this list (an earlier revision of this patch wrongly treated
// them as an internal I2C bus — checked against the official pinmap,
// that's incorrect for this board; they're just unconnected Grove pins).
static bool _gpioReserved(int p) {
    switch (p) {
        case 3: case 4: case 5: case 6: case 7:       // keyboard matrix cols
        case 8: case 9: case 10: case 11: case 13: case 15: // keyboard matrix rows + battery ADC
        case 12: case 14: case 39: case 40:            // SD card (CS/MOSI/CLK/MISO)
        case 33: case 34: case 35: case 36: case 37: case 38: // display + RGB LED power switch
        case 41: case 42: case 43: case 44: case 46:   // speaker (BCLK/SDATA/LRCLK), IR TX, mic
            return true;
        default: return false;
    }
}
static int lb_gpio_mode(lua_State* L)  {
    int p=luaL_checkinteger(L,1),m=luaL_checkinteger(L,2);
    if (_gpioReserved(p)) return luaL_error(L, "gpio %d is reserved by the system (I2C/SD/keyboard/audio/USB) - use a free GPIO", p);
    if(m==0)pinMode(p,INPUT);else if(m==1)pinMode(p,OUTPUT);else if(m==2)pinMode(p,INPUT_PULLUP);else if(m==3)pinMode(p,INPUT_PULLDOWN);return 0;
}
static int lb_gpio_write(lua_State* L) {
    int p=luaL_checkinteger(L,1);
    if (_gpioReserved(p)) return luaL_error(L, "gpio %d is reserved by the system - use a free GPIO", p);
    digitalWrite(p,luaL_checkinteger(L,2)); return 0;
}
static int lb_gpio_read(lua_State* L)  { lua_pushinteger(L,digitalRead(luaL_checkinteger(L,1))); return 1; }
static int lb_gpio_aread(lua_State* L) { lua_pushinteger(L,analogRead(luaL_checkinteger(L,1))); return 1; }
static int lb_gpio_awrite(lua_State* L){
    int p=luaL_checkinteger(L,1);
    if (_gpioReserved(p)) return luaL_error(L, "gpio %d is reserved by the system - use a free GPIO", p);
    analogWrite(p,luaL_checkinteger(L,2)); return 0;
}
static int lb_gpio_tone(lua_State* L)  {
    int p=luaL_checkinteger(L,1);
    if (_gpioReserved(p)) return luaL_error(L, "gpio %d is reserved by the system - use a free GPIO", p);
    tone(p,luaL_checkinteger(L,2),luaL_optinteger(L,3,0)); return 0;
}
static int lb_gpio_notone(lua_State* L){ noTone(luaL_checkinteger(L,1)); return 0; }

// disp.*  (cls/print/bright/w/h are the original terminal-log helpers;
// everything below is new raw-pixel drawing for game/graphics scripts,
// all operating on the same sprite the REPL already owns and pushes)
static int lb_disp_cls(lua_State* L)    { g_lt.clear(); return 0; }
static int lb_disp_print(lua_State* L)  { g_lt.push(luaL_checkstring(L,1),COL_TEXT); return 0; }
static int lb_disp_bright(lua_State* L) { M5Cardputer.Display.setBrightness(constrain(luaL_checkinteger(L,1),0,255)); return 0; }
static int lb_disp_w(lua_State* L)      { lua_pushinteger(L,SCREEN_W); return 1; }
static int lb_disp_h(lua_State* L)      { lua_pushinteger(L,SCREEN_H); return 1; }

static int lb_disp_rgb(lua_State* L) {
    if (!g_lcanvas) { lua_pushinteger(L,0); return 1; }
    lua_pushinteger(L, g_lcanvas->color565((uint8_t)luaL_checkinteger(L,1),(uint8_t)luaL_checkinteger(L,2),(uint8_t)luaL_checkinteger(L,3)));
    return 1;
}
static int lb_disp_clear(lua_State* L) {
    if (!g_lcanvas) return 0;
    g_lcanvas->fillSprite((uint16_t)luaL_optinteger(L,1,0x0000));
    g_lua_gfx_active = true;
    return 0;
}
static int lb_disp_pixel(lua_State* L) {
    if (!g_lcanvas) return 0;
    g_lcanvas->drawPixel((int32_t)luaL_checkinteger(L,1),(int32_t)luaL_checkinteger(L,2),(uint16_t)luaL_optinteger(L,3,0xFFFF));
    g_lua_gfx_active = true;
    return 0;
}
static int lb_disp_line(lua_State* L) {
    if (!g_lcanvas) return 0;
    g_lcanvas->drawLine((int32_t)luaL_checkinteger(L,1),(int32_t)luaL_checkinteger(L,2),(int32_t)luaL_checkinteger(L,3),(int32_t)luaL_checkinteger(L,4),(uint16_t)luaL_optinteger(L,5,0xFFFF));
    g_lua_gfx_active = true;
    return 0;
}
static int lb_disp_rect(lua_State* L) {
    if (!g_lcanvas) return 0;
    g_lcanvas->drawRect((int32_t)luaL_checkinteger(L,1),(int32_t)luaL_checkinteger(L,2),(int32_t)luaL_checkinteger(L,3),(int32_t)luaL_checkinteger(L,4),(uint16_t)luaL_optinteger(L,5,0xFFFF));
    g_lua_gfx_active = true;
    return 0;
}
static int lb_disp_fillrect(lua_State* L) {
    if (!g_lcanvas) return 0;
    g_lcanvas->fillRect((int32_t)luaL_checkinteger(L,1),(int32_t)luaL_checkinteger(L,2),(int32_t)luaL_checkinteger(L,3),(int32_t)luaL_checkinteger(L,4),(uint16_t)luaL_optinteger(L,5,0xFFFF));
    g_lua_gfx_active = true;
    return 0;
}
static int lb_disp_circle(lua_State* L) {
    if (!g_lcanvas) return 0;
    g_lcanvas->drawCircle((int32_t)luaL_checkinteger(L,1),(int32_t)luaL_checkinteger(L,2),(int32_t)luaL_checkinteger(L,3),(uint16_t)luaL_optinteger(L,4,0xFFFF));
    g_lua_gfx_active = true;
    return 0;
}
static int lb_disp_fillcircle(lua_State* L) {
    if (!g_lcanvas) return 0;
    g_lcanvas->fillCircle((int32_t)luaL_checkinteger(L,1),(int32_t)luaL_checkinteger(L,2),(int32_t)luaL_checkinteger(L,3),(uint16_t)luaL_optinteger(L,4,0xFFFF));
    g_lua_gfx_active = true;
    return 0;
}
static int lb_disp_triangle(lua_State* L) {
    if (!g_lcanvas) return 0;
    g_lcanvas->drawTriangle((int32_t)luaL_checkinteger(L,1),(int32_t)luaL_checkinteger(L,2),(int32_t)luaL_checkinteger(L,3),(int32_t)luaL_checkinteger(L,4),(int32_t)luaL_checkinteger(L,5),(int32_t)luaL_checkinteger(L,6),(uint16_t)luaL_optinteger(L,7,0xFFFF));
    g_lua_gfx_active = true;
    return 0;
}
static int lb_disp_text(lua_State* L) {
    if (!g_lcanvas) return 0;
    int x = (int)luaL_checkinteger(L,1), y = (int)luaL_checkinteger(L,2);
    const char* str = luaL_checkstring(L,3);
    uint16_t col = (uint16_t)luaL_optinteger(L,4,0xFFFF);
    int size = (int)luaL_optinteger(L,5,1);
    g_lcanvas->setTextColor(col);
    g_lcanvas->setTextSize(size);
    g_lcanvas->setCursor(x,y);
    g_lcanvas->print(str);
    g_lua_gfx_active = true;
    return 0;
}
static int lb_disp_clip(lua_State* L) {
    if (!g_lcanvas) return 0;
    if (lua_gettop(L) == 0) { g_lcanvas->clearClipRect(); return 0; }
    g_lcanvas->setClipRect((int32_t)luaL_checkinteger(L,1),(int32_t)luaL_checkinteger(L,2),(int32_t)luaL_checkinteger(L,3),(int32_t)luaL_checkinteger(L,4));
    return 0;
}
static int lb_disp_flip(lua_State* L) {
    if (!g_lcanvas) return 0;
    { DispLock _lk; g_lcanvas->pushSprite(0,0); }
    g_lua_gfx_active = true;
    return 0;
}

// key.*
static int lb_key_pressed(lua_State* L) {
    String n = luaL_checkstring(L,1); n.toLowerCase();
    lua_pushboolean(L, _lua_key_held(n));
    return 1;
}
static int lb_key_just_pressed(lua_State* L) {
    String n = luaL_checkstring(L,1); n.toLowerCase();
    int slot = _lua_key_slot(n);
    lua_pushboolean(L, slot >= 0 && g_key_cur[slot] && !g_key_prev[slot]);
    return 1;
}
static int lb_key_released(lua_State* L) {
    String n = luaL_checkstring(L,1); n.toLowerCase();
    int slot = _lua_key_slot(n);
    lua_pushboolean(L, slot >= 0 && !g_key_cur[slot] && g_key_prev[slot]);
    return 1;
}
static int lb_key_any(lua_State* L) { lua_pushboolean(L, M5Cardputer.Keyboard.isPressed()); return 1; }
static int lb_key_poll(lua_State* L) { _lua_key_poll(); return 0; }

// sound.*
static float _lua_note_freq(const String& name) {
    if (name.length() < 2) return 0;
    static const int SEMITONES[7] = {9,11,0,2,4,5,7}; // A B C D E F G, relative to C
    int idx = toupper(name[0]) - 'A';
    if (idx < 0 || idx > 6) return 0;
    int semitone = SEMITONES[idx];
    int p = 1;
    if (name[p] == '#')      { semitone++; p++; }
    else if (name[p] == 'b') { semitone--; p++; }
    if (p >= (int)name.length()) return 0;
    int octave = name.substring(p).toInt();
    float base = 261.63f * powf(2.0f, (float)(octave - 4));
    return base * powf(2.0f, semitone / 12.0f);
}
static int lb_sound_tone(lua_State* L) {
    M5Cardputer.Speaker.tone((int)luaL_checkinteger(L,1), (int)luaL_optinteger(L,2,200));
    return 0;
}
static int lb_sound_note(lua_State* L) {
    String n = luaL_checkstring(L,1);
    float f = _lua_note_freq(n);
    if (f > 0) M5Cardputer.Speaker.tone(f, (int)luaL_optinteger(L,2,300));
    return 0;
}
static int lb_sound_stop(lua_State* L)   { M5Cardputer.Speaker.stop(); return 0; }
static int lb_sound_volume(lua_State* L) { M5Cardputer.Speaker.setVolume((uint8_t)constrain((int)luaL_checkinteger(L,1),0,255)); return 0; }
static int lb_sound_play(lua_State* L) {
    String path = luaL_checkstring(L,1);
    if (!path.startsWith("/")) path = "/" + path;
    if (!SD.exists(path)) { g_lt.push(String("sound.play: not found ")+path, COL_ERR); return 0; }
    String low = path; low.toLowerCase();
    if (!low.endsWith(".wav")) {
        // Only WAV is wired up here — reusing Music's MP3/ID3 decode chain
        // needs its own core1 task and would fight the Lua task for the
        // same core, so it's out of scope for this simple effects player.
        g_lt.push("sound.play: only .wav supported for now", COL_WARN);
        return 0;
    }
    AudioFileSourceSD file(path.c_str());
    AudioGeneratorWAV wav;
    if (!wav.begin(&file, &_luaSndOut)) { g_lt.push("sound.play: begin failed", COL_ERR); return 0; }
    while (wav.isRunning() && !g_exec.interrupt && !g_exec.quit) {
        if (!wav.loop()) break;
    }
    wav.stop();
    return 0;
}

// timer.*
static int lb_timer_every(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_checkinteger(L,1);
    luaL_checktype(L,2,LUA_TFUNCTION);
    lua_pushvalue(L,2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    g_timers.push_back({millis()+ms, ms, true, ref, true});
    lua_pushinteger(L, (lua_Integer)g_timers.size()-1);
    return 1;
}
static int lb_timer_after(lua_State* L) {
    uint32_t ms = (uint32_t)luaL_checkinteger(L,1);
    luaL_checktype(L,2,LUA_TFUNCTION);
    lua_pushvalue(L,2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    g_timers.push_back({millis()+ms, ms, false, ref, true});
    lua_pushinteger(L, (lua_Integer)g_timers.size()-1);
    return 1;
}
static int lb_timer_cancel(lua_State* L) {
    int id = (int)luaL_checkinteger(L,1);
    if (id >= 0 && id < (int)g_timers.size() && g_timers[id].alive) {
        luaL_unref(L, LUA_REGISTRYINDEX, g_timers[id].ref);
        g_timers[id].alive = false;
    }
    return 0;
}

// os.frameTime() — seconds since the previous call (0.0 on the first call).
static uint32_t g_lua_frameTimeLast = 0;
static int lb_os_frametime(lua_State* L) {
    uint32_t now = millis();
    float dt = g_lua_frameTimeLast ? (now - g_lua_frameTimeLast) / 1000.0f : 0.0f;
    g_lua_frameTimeLast = now;
    lua_pushnumber(L, dt);
    return 1;
}

// os.stack() -> bytes of the Lua task's 16KB stack (see lua_core1_task's
// xTaskCreatePinnedToCore call) that have NEVER been touched since this
// Lua session started - i.e. how much margin is actually there. This is
// the safe way to answer "can the 16KB stack be made smaller": run the
// heaviest/most recursive script you actually intend to use, then call
// os.stack() - whatever it reports has genuinely never been needed even
// once, so the stack size could be lowered by roughly that much (minus a
// safety margin) without guessing. Shrinking it blindly without measuring
// first risks silent stack-corruption crashes on deep recursion, which is
// worse than the memory it would save - this function exists so nobody
// has to guess.
static int lb_os_stack(lua_State* L) {
    UBaseType_t words = uxTaskGetStackHighWaterMark(nullptr);
    lua_pushinteger(L, (lua_Integer)(words * sizeof(StackType_t)));
    return 1;
}

// loop(fn, fps) — the game-loop driver. Runs fn() at (up to) fps times a
// second until fn() returns false, Ctrl+C interrupts, or the app closes.
// Advances the key.* edge snapshot and fires due timer.* callbacks once
// per tick, then hands control to fn().
static int lb_loop(lua_State* L) {
    luaL_checktype(L,1,LUA_TFUNCTION);
    int fps = (int)luaL_optinteger(L,2,30);
    if (fps < 1) fps = 1; if (fps > 60) fps = 60;
    uint32_t frameMs = 1000 / fps;
    g_lua_gfx_active = true;
    g_lua_frameTimeLast = 0;

    while (!g_exec.interrupt && !g_exec.quit) {
        uint32_t t0 = millis();
        _lua_key_poll();
        _lua_timers_tick(L);

        lua_pushvalue(L,1);
        if (lua_pcall(L,0,1,0) != LUA_OK) {
            g_lua_gfx_active   = false;
            g_lua_force_redraw = true;
            _lua_timers_clear(L);
            return lua_error(L); // message already on the stack from pcall
        }
        bool stop = lua_isboolean(L,-1) && !lua_toboolean(L,-1);
        lua_pop(L,1);
        if (stop) break;

        uint32_t el = millis() - t0;
        if (el < frameMs) delay(frameMs - el); else vTaskDelay(1);
    }
    // Ctrl+C stopping loop() counts as loop() having handled the
    // interrupt - clear the flag here so the *next* statement (sleep(),
    // net.get(), json.decode(), etc.) doesn't immediately throw a stale
    // "interrupted" error for a Ctrl+C that already did its job.
    g_exec.interrupt = false;
    _lua_timers_clear(L);
    g_lua_gfx_active   = false;
    g_lua_force_redraw = true;
    return 0;
}

// ui.* — thin wrappers over the same dialogs every built-in app uses.
static int lb_ui_confirm(lua_State* L) {
    if (!g_lcanvas) { lua_pushboolean(L,false); return 1; }
    const char* msg = luaL_optstring(L,1,"Exit to home?");
    g_lua_gfx_active = true; // blocking multi-frame dialog - don't let the terminal's own periodic redraw fight it for the screen
    bool r = OsUI::confirmExit(*g_lcanvas, msg);
    g_lua_gfx_active = false;
    lua_pushboolean(L, r);
    g_lua_force_redraw = true;
    return 1;
}
static int lb_ui_toast(lua_State* L) {
    if (!g_lcanvas) return 0;
    const char* msg = luaL_checkstring(L,1);
    uint16_t col = (uint16_t)luaL_optinteger(L,2,COL_OK);
    OsUI::showToast(*g_lcanvas, msg, col);
    { DispLock _lk; g_lcanvas->pushSprite(0,0); }
    // showToast() is a synchronous, instant draw (no auto-dismiss timer),
    // so nothing ongoing needs protecting — leaving gfx_active=true here
    // would freeze the terminal's own redraw for the rest of the script
    // (and beyond, until the next command), hiding any print() after this.
    return 0;
}
static int lb_ui_input_text(lua_State* L) {
    if (!g_lcanvas) { lua_pushstring(L,""); return 1; }
    const char* prompt = luaL_optstring(L,1,"Input:");
    g_lua_gfx_active = true;
    String r = OsUI::inputText(*g_lcanvas, prompt);
    g_lua_gfx_active = false;
    lua_pushstring(L, r.c_str());
    g_lua_force_redraw = true;
    return 1;
}
static int lb_ui_progress(lua_State* L) {
    if (!g_lcanvas) return 0;
    OsUI::drawProgressBar(*g_lcanvas, (int)luaL_checkinteger(L,1));
    { DispLock _lk; g_lcanvas->pushSprite(0,0); }
    // same reasoning as ui.toast — instant draw, don't freeze the terminal
    return 0;
}
static int lb_ui_menu(lua_State* L) {
    if (!g_lcanvas) { lua_pushnil(L); return 1; }
    luaL_checktype(L,1,LUA_TTABLE);
    int n = (int)lua_rawlen(L,1);
    if (n <= 0) { lua_pushnil(L); return 1; }
    std::vector<String> items;
    for (int i=1;i<=n;i++){ lua_geti(L,1,i); items.push_back(String(luaL_optstring(L,-1,""))); lua_pop(L,1); }
    int sel = 0;
    OsUI::waitRelease();
    g_lua_gfx_active = true; // own the screen for the whole blocking selection loop
    while (true) {
        M5Cardputer.update();
        g_lcanvas->fillSprite(COL_BG);
        OsUI::drawHeader(*g_lcanvas, "Select", COL_HEADER);
        int y = HEADER_H + 6;
        for (int i=0;i<(int)items.size();i++) {
            if (i==sel) { g_lcanvas->fillRect(4,y-2,SCREEN_W-8,14,COL_SELECTED); g_lcanvas->setTextColor(BLACK,COL_SELECTED); }
            else        { g_lcanvas->setTextColor(COL_TEXT,COL_BG); }
            g_lcanvas->setCursor(8,y);
            g_lcanvas->print(items[i]);
            y += 16;
        }
        OsUI::drawFooter(*g_lcanvas, "ENT=select  `=cancel");
        { DispLock _lk; g_lcanvas->pushSprite(0,0); }
        if (g_exec.interrupt || g_exec.quit) { g_lua_gfx_active = false; g_lua_force_redraw = true; lua_pushnil(L); return 1; }
        if (M5Cardputer.Keyboard.isKeyPressed(';')) { if (sel>0) sel--; delay(120); }
        if (M5Cardputer.Keyboard.isKeyPressed('.')) { if (sel<(int)items.size()-1) sel++; delay(120); }
        auto ks = M5Cardputer.Keyboard.keysState();
        if (ks.enter) { OsUI::waitRelease(); g_lua_gfx_active = false; g_lua_force_redraw = true; lua_pushinteger(L, sel+1); return 1; }
        if (M5Cardputer.Keyboard.isKeyPressed('`')) { OsUI::waitRelease(); g_lua_gfx_active = false; g_lua_force_redraw = true; lua_pushnil(L); return 1; }
        delay(5);
    }
}

// store.* — persistent key/value scratchpad for scripts (NVS-backed).
static int lb_store_get(lua_State* L) {
    const char* key = luaL_checkstring(L,1);
    const char* def = luaL_optstring(L,2,"");
    g_luaPrefs.begin("luastore", true);
    String v = g_luaPrefs.getString(key, def);
    g_luaPrefs.end();
    lua_pushstring(L, v.c_str());
    return 1;
}
static int lb_store_set(lua_State* L) {
    const char* key = luaL_checkstring(L,1);
    const char* val = luaL_checkstring(L,2);
    g_luaPrefs.begin("luastore", false);
    g_luaPrefs.putString(key, val);
    g_luaPrefs.end();
    return 0;
}

// json.* — small encoder/decoder, enough for config files and simple
// data exchange. Not a full RFC8259 validator (e.g. it's forgiving about
// trailing commas it never produces itself), but round-trips correctly.
static void _json_escape(const char* s, String& out) {
    out += '"';
    for (const char* p=s; *p; p++) {
        char c = *p;
        if (c=='"' || c=='\\')      { out += '\\'; out += c; }
        else if (c=='\n')            out += "\\n";
        else if (c=='\t')            out += "\\t";
        else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b,sizeof(b),"\\u%04x",c); out += b; }
        else                          out += c;
    }
    out += '"';
}
static void _json_encode_value(lua_State* L, int idx, String& out);
static void _json_encode_table(lua_State* L, int idx, String& out) {
    int absIdx = lua_absindex(L, idx);
    lua_len(L, absIdx);
    lua_Integer n = lua_tointeger(L, -1);
    lua_pop(L, 1);
    int count = 0; bool isArray = true;
    lua_pushnil(L);
    while (lua_next(L, absIdx)) {
        count++;
        if (lua_type(L,-2) != LUA_TNUMBER) isArray = false;
        lua_pop(L,1);
    }
    if (count == 0) { out += "{}"; return; }
    if (isArray && count == (int)n) {
        out += "[";
        for (lua_Integer i=1;i<=n;i++) {
            if (i>1) out += ",";
            lua_geti(L, absIdx, i);
            _json_encode_value(L, -1, out);
            lua_pop(L,1);
        }
        out += "]";
    } else {
        out += "{";
        bool first = true;
        lua_pushnil(L);
        while (lua_next(L, absIdx)) {
            if (!first) out += ","; first = false;
            String keyStr;
            if (lua_type(L,-2)==LUA_TSTRING) keyStr = lua_tostring(L,-2);
            else if (lua_isinteger(L,-2))    keyStr = String((long)lua_tointeger(L,-2));
            else if (lua_type(L,-2)==LUA_TNUMBER) { char b[32]; snprintf(b,sizeof(b),"%g",lua_tonumber(L,-2)); keyStr=b; }
            else keyStr = "?";
            _json_escape(keyStr.c_str(), out);
            out += ":";
            _json_encode_value(L, -1, out);
            lua_pop(L,1);
        }
        out += "}";
    }
}
static void _json_encode_value(lua_State* L, int idx, String& out) {
    switch (lua_type(L, idx)) {
        case LUA_TNIL:     out += "null"; break;
        case LUA_TBOOLEAN: out += (lua_toboolean(L,idx) ? "true" : "false"); break;
        case LUA_TNUMBER:
            if (lua_isinteger(L,idx)) out += String((long)lua_tointeger(L,idx));
            else { char b[32]; snprintf(b,sizeof(b),"%g",lua_tonumber(L,idx)); out += b; }
            break;
        case LUA_TSTRING:  _json_escape(lua_tostring(L,idx), out); break;
        case LUA_TTABLE:   _json_encode_table(L, idx, out); break;
        default:           out += "null"; break;
    }
}
static int lb_json_encode(lua_State* L) {
    String out;
    _json_encode_value(L, 1, out);
    lua_pushstring(L, out.c_str());
    return 1;
}
struct _JsonParser {
    const char* s; int pos;
    void skipWs() { while (s[pos]==' '||s[pos]=='\t'||s[pos]=='\n'||s[pos]=='\r') pos++; }
    bool parseString(String& out) {
        if (s[pos] != '"') return false;
        pos++;
        out = "";
        while (s[pos] && s[pos] != '"') {
            if (s[pos]=='\\' && s[pos+1]) {
                pos++;
                switch (s[pos]) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case '"': out += '"';  break;
                    case '\\':out += '\\'; break;
                    case '/': out += '/';  break;
                    default:  out += s[pos]; break;
                }
                pos++;
            } else out += s[pos++];
        }
        if (s[pos] != '"') return false;
        pos++;
        return true;
    }
    bool parseValue(lua_State* L) {
        skipWs();
        char c = s[pos];
        if (c == '"') { String str; if (!parseString(str)) return false; lua_pushstring(L, str.c_str()); return true; }
        if (c == '{') return parseObject(L);
        if (c == '[') return parseArray(L);
        if (!strncmp(s+pos,"true",4))  { lua_pushboolean(L,1); pos+=4; return true; }
        if (!strncmp(s+pos,"false",5)) { lua_pushboolean(L,0); pos+=5; return true; }
        if (!strncmp(s+pos,"null",4))  { lua_pushnil(L);       pos+=4; return true; }
        // number
        int start = pos;
        if (s[pos]=='-') pos++;
        while (isdigit((unsigned char)s[pos])) pos++;
        bool isFloat = false;
        if (s[pos]=='.') { isFloat=true; pos++; while (isdigit((unsigned char)s[pos])) pos++; }
        if (s[pos]=='e'||s[pos]=='E') { isFloat=true; pos++; if (s[pos]=='+'||s[pos]=='-') pos++; while (isdigit((unsigned char)s[pos])) pos++; }
        if (pos == start) return false;
        String numStr = String(s).substring(start, pos);
        if (isFloat) lua_pushnumber(L, numStr.toFloat());
        else lua_pushinteger(L, numStr.toInt());
        return true;
    }
    bool parseArray(lua_State* L) {
        pos++; // [
        lua_newtable(L);
        int idx = 1;
        skipWs();
        if (s[pos]==']') { pos++; return true; }
        while (true) {
            if (!parseValue(L)) return false;
            lua_rawseti(L, -2, idx++);
            skipWs();
            if (s[pos]==',') { pos++; continue; }
            if (s[pos]==']') { pos++; break; }
            return false;
        }
        return true;
    }
    bool parseObject(lua_State* L) {
        pos++; // {
        lua_newtable(L);
        skipWs();
        if (s[pos]=='}') { pos++; return true; }
        while (true) {
            skipWs();
            String key;
            if (!parseString(key)) return false;
            skipWs();
            if (s[pos] != ':') return false;
            pos++;
            if (!parseValue(L)) return false;
            lua_setfield(L, -2, key.c_str());
            skipWs();
            if (s[pos]==',') { pos++; continue; }
            if (s[pos]=='}') { pos++; break; }
            return false;
        }
        return true;
    }
};
static int lb_json_decode(lua_State* L) {
    const char* str = luaL_checkstring(L,1);
    _JsonParser p{str, 0};
    p.skipWs();
    if (!p.parseValue(L)) return luaL_error(L, "json.decode: invalid JSON");
    return 1;
}

// bit.*
static int lb_bit_band(lua_State* L)   { lua_pushinteger(L,luaL_checkinteger(L,1)& luaL_checkinteger(L,2)); return 1; }
static int lb_bit_bor(lua_State* L)    { lua_pushinteger(L,luaL_checkinteger(L,1)| luaL_checkinteger(L,2)); return 1; }
static int lb_bit_bxor(lua_State* L)   { lua_pushinteger(L,luaL_checkinteger(L,1)^ luaL_checkinteger(L,2)); return 1; }
static int lb_bit_bnot(lua_State* L)   { lua_pushinteger(L,~luaL_checkinteger(L,1)); return 1; }
static int lb_bit_lshift(lua_State* L) { lua_pushinteger(L,luaL_checkinteger(L,1)<<luaL_checkinteger(L,2)); return 1; }
static int lb_bit_rshift(lua_State* L) { lua_pushinteger(L,(lua_Integer)((uint32_t)luaL_checkinteger(L,1)>>luaL_checkinteger(L,2))); return 1; }
static int lb_bit_arshift(lua_State* L){ lua_pushinteger(L,luaL_checkinteger(L,1)>>luaL_checkinteger(L,2)); return 1; }
static int lb_bit_tohex(lua_State* L)  { char b[32]; snprintf(b,sizeof(b),"%0*llX",(int)luaL_optinteger(L,2,8),(unsigned long long)(uint64_t)luaL_checkinteger(L,1)); lua_pushstring(L,b); return 1; }

// i2c.* - defaults to the Grove port (SDA G2, SCL G1). Wire's own default
// pins are the internal keyboard bus, which M5Unified already drives.
#define g_i2cBegun Tsukumo::luaWire   // shared with the module scanner
static int lb_i2c_begin(lua_State* L)  { int sda=luaL_optinteger(L,1,-1),scl=luaL_optinteger(L,2,-1); if(sda<0||scl<0){sda=2;scl=1;} if(sda==8||sda==9||scl==8||scl==9) return luaL_error(L,"i2c.begin: G8/G9 are the internal keyboard bus, which MitamaOS already runs"); Wire.begin(sda,scl); g_i2cBegun=true; lua_pushboolean(L,true);return 1; }
static int lb_i2c_scan(lua_State* L) {
    if (!g_i2cBegun) { Wire.begin(2, 1); g_i2cBegun = true; } // scan()/write()/read() without begin() first used to hang instead of erroring - don't rely on the user remembering
    Wire.setTimeOut(20); // default timeout multiplied by 126 addresses is what made this feel "slow"
    lua_newtable(L); int idx=1;
    for(uint8_t addr=0x08;addr<=0x77;addr++){Wire.beginTransmission(addr);if(Wire.endTransmission()==0){lua_pushinteger(L,addr);lua_rawseti(L,-2,idx++);}vTaskDelay(1);}
    return 1;
}
static int lb_i2c_write(lua_State* L)  { if(!g_i2cBegun){Wire.begin(2,1);g_i2cBegun=true;} uint8_t addr=luaL_checkinteger(L,1);size_t len;const char* data=luaL_checklstring(L,2,&len);Wire.beginTransmission(addr);Wire.write((const uint8_t*)data,len);uint8_t err=Wire.endTransmission();lua_pushboolean(L,err==0);lua_pushinteger(L,err);return 2; }
static int lb_i2c_read(lua_State* L)   { if(!g_i2cBegun){Wire.begin(2,1);g_i2cBegun=true;} Wire.requestFrom((uint8_t)luaL_checkinteger(L,1),(uint8_t)luaL_checkinteger(L,2));String out;while(Wire.available())out+=(char)Wire.read();lua_pushlstring(L,out.c_str(),out.length());return 1; }
static int lb_i2c_end(lua_State* L)    { Wire.end(); g_i2cBegun = false; return 0; }

// require(modname) — standalone implementation, fully independent of the
// (deliberately empty) stub `package` library in loadlib.c on ESP32.
//
// PREVIOUSLY: this hooked lb_require_loader into package.searchers, but
// loadlib.c's luaopen_package() creates an EMPTY package table (no
// searchers/loaded/path), so lua_setup_require() always bailed out
// silently and `require` was never even defined as a global. Confirmed
// broken both by source inspection and by a live _G dump on hardware
// ("MISSING require").
//
// FIX: implement require() as its own global C function that does its
// own file search + its own module cache (in the Lua registry, under
// key "PAPEROS_REQUIRE_CACHE") + its own circular-require guard. No
// dependency on package/searchers/loaded at all.
//
// Semantics (matches stock Lua require() as closely as makes sense here):
//   - Searches, in order: /sd/lua/<name>.lua, /sd/<name>.lua,
//     /<name>.lua, /sd/lib/<name>.lua
//   - Loads + runs the file ONCE; whatever it `return`s is cached and
//     returned on every subsequent require() of the same name (no
//     re-reading the SD card, no re-running side effects twice).
//   - If the module doesn't `return` anything, caches `true` (like
//     stock Lua does for modules with no explicit return value).
//   - require()-ing a module from within itself (circular require)
//     raises a clear Lua error instead of infinite-looping or crashing.
static int lb_require(lua_State* L) {
    const char* m = luaL_checkstring(L, 1);

    luaL_getsubtable(L, LUA_REGISTRYINDEX, "PAPEROS_REQUIRE_CACHE"); // stack: [cache]
    int cacheIdx = lua_gettop(L);

    lua_getfield(L, cacheIdx, m); // stack: [cache, cached-or-nil]
    if (!lua_isnil(L, -1)) {
        if (lua_isboolean(L, -1) && lua_toboolean(L, -1) == 0) {
            lua_pop(L, 2);
            return luaL_error(L, "circular require of module '%s'", m);
        }
        lua_remove(L, cacheIdx); // leave just the cached value on top
        return 1;
    }
    lua_pop(L, 1); // pop nil, stack: [cache]

    // Mark as "currently loading" so a circular require() fails fast
    // instead of re-entering this function and re-reading the file.
    lua_pushboolean(L, 0);
    lua_setfield(L, cacheIdx, m); // cache[m] = false

    String paths[] = {
        String("/sd/lua/") + m + ".lua",
        String("/sd/")     + m + ".lua",
        String("/")        + m + ".lua",
        String("/sd/lib/") + m + ".lua",
    };
    File f; String foundPath;
    for (auto& p : paths) {
        f = _fsOpen(p.c_str());
        if (f) { foundPath = p; break; }
    }
    if (!f) {
        lua_pushnil(L); lua_setfield(L, cacheIdx, m); // clear "loading" marker so a later require() can retry (e.g. after the user copies the file onto the SD card)
        lua_pop(L, 1); // pop cache
        return luaL_error(L, "module '%s' not found (checked /sd/lua, /sd, /, /sd/lib)", m);
    }

    String code; code.reserve(f.size()+1); while (f.available()) code += (char)f.read(); f.close();

    if (luaL_loadbuffer(L, code.c_str(), code.length(), foundPath.c_str()) != LUA_OK) {
        String err = lua_tostring(L, -1); lua_pop(L, 1);
        lua_pushnil(L); lua_setfield(L, cacheIdx, m);
        lua_pop(L, 1);
        return luaL_error(L, "error loading module '%s': %s", m, err.c_str());
    }

    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        String err = lua_tostring(L, -1); lua_pop(L, 1);
        lua_pushnil(L); lua_setfield(L, cacheIdx, m);
        lua_pop(L, 1);
        return luaL_error(L, "error running module '%s': %s", m, err.c_str());
    }
    // stack: [cache, result]

    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1); // module returned nothing -> cache `true`, like stock Lua
    }

    lua_pushvalue(L, -1);         // [cache, result, result]
    lua_setfield(L, cacheIdx, m); // cache[m] = result -> [cache, result]
    lua_remove(L, cacheIdx);      // [result]
    return 1;
}

static void lua_register_file_mt(lua_State* L) {
    luaL_newmetatable(L,LUA_FILE_MT); lua_pushvalue(L,-1); lua_setfield(L,-2,"__index");
    lua_pushcfunction(L,lb_file_gc);    lua_setfield(L,-2,"__gc");
    lua_pushcfunction(L,lb_file_read);  lua_setfield(L,-2,"read");
    lua_pushcfunction(L,lb_file_write); lua_setfield(L,-2,"write");
    lua_pushcfunction(L,lb_file_lines); lua_setfield(L,-2,"lines");
    lua_pushcfunction(L,lb_file_seek);  lua_setfield(L,-2,"seek");
    lua_pushcfunction(L,lb_file_flush); lua_setfield(L,-2,"flush");
    lua_pushcfunction(L,lb_file_close); lua_setfield(L,-2,"close");
    lua_pushcfunction(L,[](lua_State* L2)->int{lua_pushstring(L2,"file");return 1;}); lua_setfield(L,-2,"__tostring");
    lua_pop(L,1);
}

static void lua_register_all(lua_State* L) {
    lua_register_file_mt(L);
    lua_pushcfunction(L,lb_print); lua_setglobal(L,"print");
    lua_pushcfunction(L,lb_sleep); lua_setglobal(L,"sleep");

    // run(path)
    lua_pushcfunction(L,[](lua_State* L2)->int{
        const char* path=luaL_checkstring(L2,1); File f=_fsOpen(path);
        if(!f){g_lt.push(String("run: not found: ")+path,COL_ERR);return 0;}
        String code;code.reserve(f.size()+1);while(f.available())code+=(char)f.read();f.close();
        if(luaL_loadbuffer(L2,code.c_str(),code.length(),path)!=LUA_OK||lua_pcall(L2,0,LUA_MULTRET,0)!=LUA_OK){g_lt.push(String("ERR: ")+lua_tostring(L2,-1),COL_ERR);lua_pop(L2,1);}
        return 0;
    }); lua_setglobal(L,"run");

    // dofile(path)
    lua_pushcfunction(L,[](lua_State* L2)->int{
        const char* path=luaL_checkstring(L2,1); File f=_fsOpen(path);
        if(!f) return luaL_error(L2,"cannot open '%s'",path);
        String code;code.reserve(f.size()+1);while(f.available())code+=(char)f.read();f.close();
        if(luaL_loadbuffer(L2,code.c_str(),code.length(),path)!=LUA_OK) return lua_error(L2);
        lua_call(L2,0,LUA_MULTRET); return lua_gettop(L2);
    }); lua_setglobal(L,"dofile");

    // require(modname) — now a real, working global (see lb_require above)
    lua_pushcfunction(L,lb_require); lua_setglobal(L,"require");

    // io.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_io_open);  lua_setfield(L,-2,"open");
    lua_pushcfunction(L,lb_io_lines); lua_setfield(L,-2,"lines");
    lua_pushcfunction(L,lb_io_write); lua_setfield(L,-2,"write");
    lua_pushcfunction(L,lb_io_read);  lua_setfield(L,-2,"read");
    // io.stdin/stdout/stderr: nil stubs. PaperOS has no console I/O stream
    // objects — io.write() prints to the terminal log, io.read() always
    // returns nil, so these three can't be used as file objects anyway.
    // Setting them to nil (rather than the previous string stubs "stdin" etc.)
    // means code that does io.stdout:write("x") gets a clear Lua error
    // "attempt to index a nil value (global 'io')" instead of a confusing
    // "attempt to call a string value (method 'write')".
    lua_pushnil(L); lua_setfield(L,-2,"stdin");
    lua_pushnil(L); lua_setfield(L,-2,"stdout");
    lua_pushnil(L); lua_setfield(L,-2,"stderr");
    lua_setglobal(L,"io");

    // os.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_os_clock);      lua_setfield(L,-2,"clock");
    lua_pushcfunction(L,lb_os_time);       lua_setfield(L,-2,"time");
    lua_pushcfunction(L,lb_os_date);       lua_setfield(L,-2,"date");
    lua_pushcfunction(L,lb_os_difftime);   lua_setfield(L,-2,"difftime");
    lua_pushcfunction(L,lb_os_exit);       lua_setfield(L,-2,"exit");
    lua_pushcfunction(L,lb_os_getenv);     lua_setfield(L,-2,"getenv");
    lua_pushcfunction(L,lb_os_remove);     lua_setfield(L,-2,"remove");
    lua_pushcfunction(L,lb_os_rename);     lua_setfield(L,-2,"rename");
    lua_pushcfunction(L,lb_os_tmpname);    lua_setfield(L,-2,"tmpname");
    lua_pushcfunction(L,lb_os_launch);     lua_setfield(L,-2,"launch");
    lua_pushcfunction(L,lb_os_home);       lua_setfield(L,-2,"home");
    lua_pushcfunction(L,lb_os_heap);       lua_setfield(L,-2,"heap");
    lua_pushcfunction(L,lb_os_luamem);     lua_setfield(L,-2,"luamem");
    lua_pushcfunction(L,lb_os_psram);      lua_setfield(L,-2,"psram");
    lua_pushcfunction(L,lb_os_reset);      lua_setfield(L,-2,"reset");
    lua_pushcfunction(L,lb_os_bright);     lua_setfield(L,-2,"bright");
    lua_pushcfunction(L,lb_os_millis);     lua_setfield(L,-2,"millis");
    lua_pushcfunction(L,lb_os_micros);     lua_setfield(L,-2,"micros");
    lua_pushcfunction(L,lb_os_cpu);        lua_setfield(L,-2,"cpu");
    lua_pushcfunction(L,lb_os_flash);      lua_setfield(L,-2,"flash");
    lua_pushcfunction(L,lb_os_sketchsize); lua_setfield(L,-2,"sketchsize");
    lua_pushcfunction(L,lb_os_apps);       lua_setfield(L,-2,"apps");
    lua_pushcfunction(L,lb_os_core);       lua_setfield(L,-2,"core");
    lua_pushcfunction(L,lb_os_frametime);  lua_setfield(L,-2,"frameTime");
    lua_pushcfunction(L,lb_os_stack);      lua_setfield(L,-2,"stack");
    lua_pushcfunction(L,[](lua_State* L2)->int{lua_gc(L2,LUA_GCCOLLECT,0);lua_pushinteger(L2,ESP.getFreeHeap());return 1;}); lua_setfield(L,-2,"gc");
    lua_pushcfunction(L,[](lua_State* L2)->int{char b[48];snprintf(b,sizeof(b),"heap:%dK psram:%dK",(int)(ESP.getFreeHeap()/1024),(int)(ESP.getFreePsram()/1024));g_lt.push(b,COL_OK);return 0;}); lua_setfield(L,-2,"mem");
    lua_setglobal(L,"os");

    // term.* — access to the terminal's own on-screen scrollback, so a
    // script can save literally what's visible/was visible in the log
    // (capped at LT_LINES=40 lines, oldest get dropped by g_lt itself —
    // this just exposes whatever g_lt currently still has).
    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* L2)->int{
        g_lt.lock();
        lua_newtable(L2);
        for (int i = 0; i < g_lt.count; i++) {
            lua_pushstring(L2, g_lt.lines[i].c_str());
            lua_rawseti(L2, -2, i+1);
        }
        g_lt.unlock();
        return 1;
    });
    lua_setfield(L,-2,"dump");
    lua_pushcfunction(L, [](lua_State* L2)->int{
        g_lt.lock();
        String all;
        for (int i = 0; i < g_lt.count; i++) { all += g_lt.lines[i]; all += "\n"; }
        g_lt.unlock();
        lua_pushstring(L2, all.c_str());
        return 1;
    });
    lua_setfield(L,-2,"dumpText");
    lua_setglobal(L,"term");

    // fs.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_fs_ls);     lua_setfield(L,-2,"ls");
    lua_pushcfunction(L,lb_fs_read);   lua_setfield(L,-2,"read");
    lua_pushcfunction(L,lb_fs_write);  lua_setfield(L,-2,"write");
    lua_pushcfunction(L,lb_fs_rm);     lua_setfield(L,-2,"rm");
    lua_pushcfunction(L,lb_fs_mkdir);  lua_setfield(L,-2,"mkdir");
    lua_pushcfunction(L,lb_fs_exists); lua_setfield(L,-2,"exists");
    lua_pushcfunction(L,lb_fs_size);   lua_setfield(L,-2,"size");
    lua_pushcfunction(L,lb_fs_rename); lua_setfield(L,-2,"rename");
    lua_pushcfunction(L,lb_fs_copy);   lua_setfield(L,-2,"copy");
    lua_pushcfunction(L,lb_fs_free);   lua_setfield(L,-2,"free");
    lua_pushcfunction(L,lb_fs_total);  lua_setfield(L,-2,"total");
    lua_setglobal(L,"fs");

    // net.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_net_ip);         lua_setfield(L,-2,"ip");
    lua_pushcfunction(L,lb_net_mac);        lua_setfield(L,-2,"mac");
    lua_pushcfunction(L,lb_net_ssid);       lua_setfield(L,-2,"ssid");
    lua_pushcfunction(L,lb_net_rssi);       lua_setfield(L,-2,"rssi");
    lua_pushcfunction(L,lb_net_status);     lua_setfield(L,-2,"status");
    lua_pushcfunction(L,lb_net_scan);       lua_setfield(L,-2,"scan");
    lua_pushcfunction(L,lb_net_connect);    lua_setfield(L,-2,"connect");
    lua_pushcfunction(L,lb_net_disconnect); lua_setfield(L,-2,"disconnect");
    lua_pushcfunction(L,lb_net_get);        lua_setfield(L,-2,"get");
    lua_pushcfunction(L,lb_net_post);       lua_setfield(L,-2,"post");
    lua_setglobal(L,"net");

    // gpio.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_gpio_mode);   lua_setfield(L,-2,"mode");
    lua_pushcfunction(L,lb_gpio_write);  lua_setfield(L,-2,"write");
    lua_pushcfunction(L,lb_gpio_read);   lua_setfield(L,-2,"read");
    lua_pushcfunction(L,lb_gpio_aread);  lua_setfield(L,-2,"aread");
    lua_pushcfunction(L,lb_gpio_awrite); lua_setfield(L,-2,"awrite");
    lua_pushcfunction(L,lb_gpio_tone);   lua_setfield(L,-2,"tone");
    lua_pushcfunction(L,lb_gpio_notone); lua_setfield(L,-2,"notone");
    lua_setglobal(L,"gpio");

    // bit.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_bit_band);    lua_setfield(L,-2,"band");
    lua_pushcfunction(L,lb_bit_bor);     lua_setfield(L,-2,"bor");
    lua_pushcfunction(L,lb_bit_bxor);    lua_setfield(L,-2,"bxor");
    lua_pushcfunction(L,lb_bit_bnot);    lua_setfield(L,-2,"bnot");
    lua_pushcfunction(L,lb_bit_lshift);  lua_setfield(L,-2,"lshift");
    lua_pushcfunction(L,lb_bit_rshift);  lua_setfield(L,-2,"rshift");
    lua_pushcfunction(L,lb_bit_arshift); lua_setfield(L,-2,"arshift");
    lua_pushcfunction(L,lb_bit_tohex);   lua_setfield(L,-2,"tohex");
    lua_setglobal(L,"bit");

    // i2c.* - defaults to the Grove port (SDA G2, SCL G1). Wire's own default
// pins are the internal keyboard bus, which M5Unified already drives.
    lua_newtable(L);
    lua_pushcfunction(L,lb_i2c_begin); lua_setfield(L,-2,"begin");
    lua_pushcfunction(L,lb_i2c_scan);  lua_setfield(L,-2,"scan");
    lua_pushcfunction(L,lb_i2c_write); lua_setfield(L,-2,"write");
    lua_pushcfunction(L,lb_i2c_read);  lua_setfield(L,-2,"read");
    lua_pushcfunction(L,lb_i2c_end);   lua_setfield(L,-2,"end");
    lua_setglobal(L,"i2c");

    // disp.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_disp_cls);        lua_setfield(L,-2,"cls");
    lua_pushcfunction(L,lb_disp_print);      lua_setfield(L,-2,"print");
    lua_pushcfunction(L,lb_disp_bright);     lua_setfield(L,-2,"bright");
    lua_pushcfunction(L,lb_disp_w);          lua_setfield(L,-2,"w");
    lua_pushcfunction(L,lb_disp_h);          lua_setfield(L,-2,"h");
    lua_pushcfunction(L,lb_disp_rgb);        lua_setfield(L,-2,"rgb");
    lua_pushcfunction(L,lb_disp_clear);      lua_setfield(L,-2,"clear");
    lua_pushcfunction(L,lb_disp_pixel);      lua_setfield(L,-2,"pixel");
    lua_pushcfunction(L,lb_disp_line);       lua_setfield(L,-2,"line");
    lua_pushcfunction(L,lb_disp_rect);       lua_setfield(L,-2,"rect");
    lua_pushcfunction(L,lb_disp_fillrect);   lua_setfield(L,-2,"fillRect");
    lua_pushcfunction(L,lb_disp_circle);     lua_setfield(L,-2,"circle");
    lua_pushcfunction(L,lb_disp_fillcircle); lua_setfield(L,-2,"fillCircle");
    lua_pushcfunction(L,lb_disp_triangle);   lua_setfield(L,-2,"triangle");
    lua_pushcfunction(L,lb_disp_text);       lua_setfield(L,-2,"text");
    lua_pushcfunction(L,lb_disp_clip);       lua_setfield(L,-2,"clip");
    lua_pushcfunction(L,lb_disp_flip);       lua_setfield(L,-2,"flip");
    lua_setglobal(L,"disp");

    // key.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_key_pressed);      lua_setfield(L,-2,"pressed");
    lua_pushcfunction(L,lb_key_just_pressed); lua_setfield(L,-2,"justPressed");
    lua_pushcfunction(L,lb_key_released);     lua_setfield(L,-2,"released");
    lua_pushcfunction(L,lb_key_any);          lua_setfield(L,-2,"any");
    lua_pushcfunction(L,lb_key_poll);         lua_setfield(L,-2,"poll");
    lua_setglobal(L,"key");

    // sound.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_sound_tone);   lua_setfield(L,-2,"tone");
    lua_pushcfunction(L,lb_sound_note);   lua_setfield(L,-2,"note");
    lua_pushcfunction(L,lb_sound_play);   lua_setfield(L,-2,"play");
    lua_pushcfunction(L,lb_sound_stop);   lua_setfield(L,-2,"stop");
    lua_pushcfunction(L,lb_sound_volume); lua_setfield(L,-2,"volume");
    lua_setglobal(L,"sound");

    // timer.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_timer_every);  lua_setfield(L,-2,"every");
    lua_pushcfunction(L,lb_timer_after);  lua_setfield(L,-2,"after");
    lua_pushcfunction(L,lb_timer_cancel); lua_setfield(L,-2,"cancel");
    lua_setglobal(L,"timer");

    // ui.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_ui_menu);       lua_setfield(L,-2,"menu");
    lua_pushcfunction(L,lb_ui_confirm);    lua_setfield(L,-2,"confirm");
    lua_pushcfunction(L,lb_ui_toast);      lua_setfield(L,-2,"toast");
    lua_pushcfunction(L,lb_ui_input_text); lua_setfield(L,-2,"inputText");
    lua_pushcfunction(L,lb_ui_progress);   lua_setfield(L,-2,"progress");
    lua_setglobal(L,"ui");

    // store.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_store_get); lua_setfield(L,-2,"get");
    lua_pushcfunction(L,lb_store_set); lua_setfield(L,-2,"set");
    lua_setglobal(L,"store");

    // json.*
    lua_newtable(L);
    lua_pushcfunction(L,lb_json_encode); lua_setfield(L,-2,"encode");
    lua_pushcfunction(L,lb_json_decode); lua_setfield(L,-2,"decode");
    lua_setglobal(L,"json");

    // loop(fn, fps) — global, like sleep()/print()
    lua_pushcfunction(L,lb_loop); lua_setglobal(L,"loop");

    lua_pushinteger(L,INPUT);          lua_setglobal(L,"INPUT");
    lua_pushinteger(L,OUTPUT);         lua_setglobal(L,"OUTPUT");
    lua_pushinteger(L,INPUT_PULLUP);   lua_setglobal(L,"INPUT_PULLUP");
    lua_pushinteger(L,INPUT_PULLDOWN); lua_setglobal(L,"INPUT_PULLDOWN");
    lua_pushinteger(L,HIGH);           lua_setglobal(L,"HIGH");
    lua_pushinteger(L,LOW);            lua_setglobal(L,"LOW");

    // (require is registered directly above as a real global — see lb_require)

    luaL_dostring(L,
        "function help()\n"
        "  print('=== MitamaOS Lua 5.4  core:'..os.core()..' ===')\n"
        "  print('os: heap psram cpu flash millis micros core gc mem')\n"
        "  print('fs: ls read write rm mkdir exists size copy rename free total')\n"
        "  print('io: open lines write')\n"
        "  print('net: ip mac ssid rssi status scan connect disconnect get post')\n"
        "  print('gpio: mode read write aread awrite tone notone')\n"
        "  print('i2c: begin scan write read end')\n"
        "  print('bit: band bor bxor bnot lshift rshift arshift tohex')\n"
        "  print('disp: cls print bright w h')\n"
        "  print('shortcuts: ls cat rm mkdir cp mv df heap gc mem clear reboot')\n"
        "  print('  wget ifconfig i2cscan hex dump split trim map filter reduce')\n"
        "  print('Ctrl+C or exit() to interrupt/quit')\n"
        "end\n"
        "function clear()    disp.cls() end\n"
        "function heap()     print(os.heap()..' free / '..os.psram()..' psram') end\n"
        "function gc()       local h=os.gc() print('GC done, heap: '..h) end\n"
        "function mem()      os.mem() end\n"
        "function reboot()   os.reset() end\n"
        "function exit()     os.home() end\n"
        "function quit()     os.home() end\n"
        "function ls(p)      fs.ls(p or '/sd') end\n"
        "function cat(p)     local s=fs.read(p) if s then print(s) else print('not found') end end\n"
        "function pwd()      print('/sd') end\n"
        "function touch(p)   fs.write(p,'') end\n"
        "function rm(p)      print(fs.rm(p) and 'ok' or 'fail') end\n"
        "function mkdir(p)   print(fs.mkdir(p) and 'ok' or 'fail') end\n"
        "function cp(a,b)    print(fs.copy(a,b) and 'ok' or 'fail') end\n"
        "function mv(a,b)    print(fs.rename(a,b) and 'ok' or 'fail') end\n"
        "function df()       print('free:'..fs.free()..' total:'..fs.total()) end\n"
        "function ifconfig() print('IP:'..net.ip()..' MAC:'..net.mac()) end\n"
        "function wget(u)    local c,b=net.get(u) if c then print(b:sub(1,200)) else print('ERR:'..b) end end\n"
        "function i2cscan()  local t=i2c.scan() for _,a in ipairs(t) do print(string.format('0x%02X',a)) end print(#t..' found') end\n"
        "function hex(n,d)   return bit.tohex(n,d or 8) end\n"
        "function dump(t,i)  i=i or 0 local sp=string.rep('  ',i) if type(t)~='table' then print(sp..tostring(t)) return end for k,v in pairs(t) do if type(v)=='table' then print(sp..tostring(k)..':') dump(v,i+1) else print(sp..tostring(k)..'='..tostring(v)) end end end\n"
        "function json_enc(t) if type(t)=='table' then local s,f='{',true for k,v in pairs(t) do if not f then s=s..',' end f=false s=s..'\"'..tostring(k)..'\":'..json_enc(v) end return s..'}' elseif type(t)=='string' then return '\"'..t..'\"' else return tostring(t) end end\n"
        "function split(s,sep) sep=sep or '%s+' local r={} local start=1 while true do local i,j=s:find(sep,start) if not i then r[#r+1]=s:sub(start) break end r[#r+1]=s:sub(start,i-1) start=j+1 end return r end\n"
        "function trim(s)     return s:match('^%s*(.-)%s*$') end\n"
        "function starts(s,p) return s:sub(1,#p)==p end\n"
        "function ends(s,p)   return p=='' or s:sub(-#p)==p end\n"
        "function tkeys(t)    local r={} for k in pairs(t) do r[#r+1]=k end return r end\n"
        "function tvals(t)    local r={} for _,v in pairs(t) do r[#r+1]=v end return r end\n"
        "function tlen(t)     local n=0 for _ in pairs(t) do n=n+1 end return n end\n"
        "function map(t,f)    local r={} for i,v in ipairs(t) do r[i]=f(v) end return r end\n"
        "function filter(t,f) local r={} for _,v in ipairs(t) do if f(v) then r[#r+1]=v end end return r end\n"
        "function reduce(t,f,a) for _,v in ipairs(t) do a=f(a,v) end return a end\n"
    );
}


// luaL_openlibs() opens Lua's *entire* standard library, but three of
// those are dead weight on this OS specifically:
//  - "package": require() is a fully custom C function (lb_require,
//    registered below) that never touches package.loaded/searchers/path -
//    the real package library's tables just sit there unused.
//  - "os" / "io": both get their global table immediately overwritten by
//    this file's own os.*/io.* (see lua_register_all below) - the real
//    versions' functions (and io's real stdin/stdout/stderr file objects)
//    become unreachable garbage the instant that happens. Confirms the
//    same conclusion as the io.stdin/stdout/stderr patch note: the real
//    io library's setup here was always thrown away, never used.
// "utf8" and "debug" are additionally skipped: real capability, but
// nothing in this OS's documented Lua API touches either, and dropping
// them is a one-line revert if that's ever wrong for someone. On a board
// with no PSRAM where a script can be fighting for the last 10-20KB, not
// paying for five libraries' worth of tables/closures that are either
// unreachable or unused is a real, permanent, zero-downside win - unlike
// the WiFi fix above, this isn't reclaimed later, it's just never spent.
static void lua_open_selected_libs(lua_State* L) {
    luaL_requiref(L, LUA_GNAME,       luaopen_base,      1); lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME,  luaopen_table,     1); lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME,  luaopen_string,    1); lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math,      1); lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME,   luaopen_coroutine, 1); lua_pop(L, 1);
}

static lua_State* g_lstate = nullptr;

// luaL_newstate() uses realloc/malloc directly with no ceiling, so we
// install our own allocator via lua_newstate() (see lua_core1_task below)
// that tracks bytes in use and refuses any allocation that would push Lua
// past LUA_HEAP_LIMIT_BYTES - the script gets a normal Lua "not enough
// memory" error instead of the whole board locking up or rebooting.
static void* lua_limited_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud;
    if (nsize == 0) {
        if (ptr) { g_lua_bytes_used -= osize; free(ptr); }
        return nullptr;
    }
    size_t used_after = g_lua_bytes_used - (ptr ? osize : 0) + nsize;
    if (nsize > osize && used_after > LUA_HEAP_LIMIT_BYTES) {
        return nullptr; // Lua treats a NULL realloc/malloc result as OOM and raises a normal Lua error
    }
    void* np = realloc(ptr, nsize);
    if (!np) return nullptr;
    g_lua_bytes_used = used_after;
    return np;
}

static void lua_core1_task(void* param) {
    g_luaTaskAlive = true;
    // Create Lua state on core 1, using the memory-limited allocator above
    // instead of luaL_newstate()'s unbounded default.
    g_lua_bytes_used = 0;
    g_lstate = lua_newstate(lua_limited_alloc, nullptr);
    if (!g_lstate) {
        g_lt.push("ERR: lua state alloc failed", COL_ERR);
        g_luaTaskAlive = false;
        vTaskDelete(nullptr); return;
    }
    lua_open_selected_libs(g_lstate);
    lua_register_all(g_lstate);
    // install interrupt hook every 1000 instructions
    lua_sethook(g_lstate, lua_interrupt_hook, LUA_MASKCOUNT, 1000);

    g_lt.push("Lua 5.4  core1  " OS_NAME, COL_OK);
    g_lt.push("Tab=complete  Ctrl+C=interrupt  exit()=home", COL_DIM);

    while (!g_exec.quit) {
        // wait for a command from core0
        if (!g_exec.pending) { vTaskDelay(1); continue; }

        xSemaphoreTake(g_exec.cmdMtx, portMAX_DELAY);
        String cmd = g_exec.cmd;
        g_exec.pending = false;
        xSemaphoreGive(g_exec.cmdMtx);

        g_exec.running  = true;
        g_exec.interrupt = false;
        g_lua_gfx_active = false;
        _lua_timers_clear(g_lstate);

        lua_State* L = g_lstate;
        lua_settop(L, 0);

        // try as expression
        String expr = String("return ") + cmd;
        int r = luaL_loadstring(L, expr.c_str());
        if (r == LUA_OK) {
            r = lua_pcall(L, 0, LUA_MULTRET, 0);
            if (r == LUA_OK) {
                int n = lua_gettop(L);
                if (n > 0) {
                    String out;
                    for (int i = 1; i <= n; i++) {
                        if (lua_type(L,i)==LUA_TNIL) continue;
                        if (out.length()) out+="\t";
                        out += _luaValToStr(L, i);
                    }
                    if (out.length()) g_lt.push(out, COL_TEXT);
                }
            } else {
                String e = lua_tostring(L, -1);
                // strip "[string...]:N:" prefix
                int c2 = e.indexOf(':', e.indexOf(':')+1);
                if (c2>=0) e=e.substring(c2+2);
                if (e.indexOf("attempt to call a nil value")>=0) {
                    int pa=e.indexOf("'"), pb=e.lastIndexOf("'");
                    if (pa>=0&&pb>pa) e="unknown: "+e.substring(pa+1,pb);
                } else if (e.indexOf("attempt to index a nil value")>=0) {
                    e = "nil value (not loaded?)";
                } else if (e == "interrupted") {
                    e = "^C";
                }
                g_lt.push(e, COL_ERR);
            }
            lua_settop(L, 0);
        } else {
            lua_settop(L, 0);
            // try as statement
            r = luaL_loadstring(L, cmd.c_str());
            if (r == LUA_OK) {
                r = lua_pcall(L, 0, LUA_MULTRET, 0);
                if (r != LUA_OK) {
                    String e = lua_tostring(L, -1);
                    int c2 = e.indexOf(':', e.indexOf(':')+1);
                    if (c2>=0) e=e.substring(c2+2);
                    if (e.indexOf("attempt to call a nil value")>=0) {
                        int pa=e.indexOf("'"), pb=e.lastIndexOf("'");
                        if (pa>=0&&pb>pa) e="unknown: "+e.substring(pa+1,pb);
                    } else if (e == "interrupted") {
                        e = "^C";
                    }
                    g_lt.push(e, COL_ERR);
                }
            } else {
                String e = lua_tostring(L, -1);
                int c2 = e.indexOf(':', e.indexOf(':')+1);
                if (c2>=0) e=e.substring(c2+2);
                g_lt.push(e, COL_ERR);
            }
            lua_settop(L, 0);
        }

        // full GC after each exec — memory is tight (96K Lua heap cap),
        // so reclaim everything a command left behind right away instead
        // of only nibbling at it incrementally.
        lua_gc(L, LUA_GCCOLLECT, 0);

        // Give the terminal's own screen back the moment the command is
        // done — otherwise any disp.*/ui.* call anywhere in the script
        // (outside loop(), which manages this flag itself) leaves the
        // terminal's _draw() permanently skipped (it bails out early
        // while gfx_active is true) until the user blindly types another
        // command. That hid the tail of print()s and even the input
        // cursor, looking exactly like a hang.
        g_lua_gfx_active   = false;
        g_lua_force_redraw = true;
        g_exec.running = false;
    }

    lua_close(g_lstate);
    g_lstate = nullptr;
    g_luaTaskAlive = false;
    vTaskDelete(nullptr);
}


class LuaTermApp : public App {
public:
    LuaTermApp(LGFX_Sprite& c) : _canvas(c) { g_lcanvas = &c; }
    const char* getName() const override { return "Lua"; }
    const char* getIcon() const override { return "L"; }
    const char* getDesc() const override { return "Lua 5.4 terminal"; }

    void onStart() override {
        _input = ""; _cursor = 0; _histIdx = -1;
        _tabActive = false; _tabIdx = -1; _tabPrefix = "";

        // WiFi's radio + lwip/mbedTLS buffers stay resident in heap the
        // whole time it's connected - on this board (no PSRAM) that alone
        // can be the difference between "a script has room to run" and
        // "os.heap() reads 20-30K before the script has allocated a single
        // byte". Most Lua scripts (drawing, GPIO, math, games) never touch
        // net.* at all, so drop the radio here and only pay for it if/when
        // a script actually calls net.get()/post()/connect() (see
        // _lua_net_ensure_wifi below) - same pattern myAI already uses for
        // its model cache, for the identical reason.
        _wifiWasOn = WiFiManager::get().isConnected();
        if (_wifiWasOn) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); delay(50); }

        if (!_started) {
            g_lt.init();
            g_exec.init();
            _started = true;
        } else {
            g_lt.clear();
        }

        // kill old core1 task if running — wait for it to actually finish
        // (g_luaTaskAlive goes false right before it calls lua_close()+
        // vTaskDelete()) instead of a blind timeout. Spawning a new task
        // while the old one is still alive means two tasks touch the same
        // g_lstate/g_lua_bytes_used globals at once, which corrupts both
        // and is exactly what caused "83k free, can't type, empty log"
        // after exiting and re-entering.
        if (_taskHandle) {
            g_exec.quit = true;
            g_exec.interrupt = true;
            int waited = 0;
            while (g_luaTaskAlive && waited < 4000) { delay(10); waited += 10; }
            if (g_luaTaskAlive) {
                // old task truly stuck (e.g. blocked on I/O) — do NOT spawn
                // a second one on top of it, that's how things got corrupted
                // last time. Tell the user instead of silently racing.
                g_lt.push("ERR: previous Lua task didn't exit, reboot device", COL_ERR);
                _taskHandle = nullptr;
                _draw();
                return;
            }
            _taskHandle = nullptr;
        }

        g_exec.quit      = false;
        g_exec.pending   = false;
        g_exec.running   = false;
        g_exec.interrupt = false;

        // spawn Lua on core 1, 16KB stack
        xTaskCreatePinnedToCore(
            lua_core1_task, "LuaREPL",
            16384, nullptr, 1,
            &_taskHandle, 1
        );

        _draw();
    }

    void onStop() override {
        // signal core1 to stop but don't block UI
        g_exec.quit      = true;
        g_exec.interrupt = true;

        // Restore WiFi to whatever state it was in before Lua turned it off
        // above - mirrors myAI's onStop() exactly. connectFirst(0) is
        // non-blocking (just calls WiFi.begin() and returns); the launcher's
        // own background reconnect loop (see wifi_manager.h) takes it from
        // there, same as after any other app that had briefly dropped WiFi.
        if (_wifiWasOn) { WiFi.mode(WIFI_STA); WiFiManager::get().connectFirst(0); }
    }

    void update() override {
        M5Cardputer.update();

        // ` key -> confirm exit
        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            OsUI::waitRelease();
            if (OsUI::confirmExit(_canvas)) {
                AppManager::get().goHome(); return;
            }
            _draw(); return;
        }

        // Ctrl+C — interrupt running Lua. Checked BEFORE the isChange()
        // early-return below: isChange() only fires on the tick where the
        // pressed-key COUNT changes, so if Ctrl and C are pressed on
        // separate ticks (near-unavoidable for a human), or if this app
        // missed the single-tick window while core1 was busy, the
        // combination would otherwise never register. isPressed() reflects
        // the keys currently held down regardless of change, so checking
        // it directly here is more reliable for a "kill switch" hotkey.
        // Combo itself is read via OsUI::ctrlChar() — see its comment for
        // why pairing .ctrl with a fresh isKeyPressed('c') call doesn't
        // reliably fire on this hardware.
        if (M5Cardputer.Keyboard.isPressed()) {
            Keyboard_Class::KeysState stCtrl = M5Cardputer.Keyboard.keysState();
            if (OsUI::ctrlChar(stCtrl, 'c')) {
                if (g_exec.running) {
                    g_exec.interrupt = true;
                    g_lt.push("^C (interrupting...)", COL_WARN);
                } else {
                    _input = ""; _cursor = 0;
                    g_lt.push("^C", COL_WARN);
                }
                _tabActive = false; _draw();
                OsUI::waitRelease();
                return;
            }
        }

        // While a script has the screen (disp.*/loop()), it reads keys via
        // key.* itself and manages the canvas itself — the REPL must not
        // also interpret keystrokes as command-line editing or repaint
        // chrome over it. Only the `\`` (exit) and Ctrl+C (interrupt)
        // checks above stay live as an escape hatch. Once the script hands
        // control back, g_lua_force_redraw restores the normal terminal view.
        if (g_lua_gfx_active) return;

        if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
            // still redraw if core1 pushed new output
            static int lastCount = -1;
            static uint32_t lastSpin = 0;
            bool dueToSpin = g_exec.running && (millis() - lastSpin > 150);
            if (g_lt.count != lastCount || g_lua_force_redraw || dueToSpin) {
                lastCount = g_lt.count; g_lua_force_redraw = false;
                if (dueToSpin) lastSpin = millis();
                _draw();
            }
            return;
        }

        Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();

        // Fn combos
        if (st.fn) {
            bool handled = true;
            if      (M5Cardputer.Keyboard.isKeyPressed('<')) { _cursorLeft();  _tabActive=false; delay(80); }
            else if (M5Cardputer.Keyboard.isKeyPressed('>')) { _cursorRight(); _tabActive=false; delay(80); }
            else if (M5Cardputer.Keyboard.isKeyPressed('a')) { _cursorHome();  _tabActive=false; delay(80); }
            else if (M5Cardputer.Keyboard.isKeyPressed('e')) { _cursorEnd();   _tabActive=false; delay(80); }
            else if (M5Cardputer.Keyboard.isKeyPressed(';')) { g_lt.scrollUp();   delay(80); }
            else if (M5Cardputer.Keyboard.isKeyPressed('.')) { g_lt.scrollDown(); delay(80); }
            else if (M5Cardputer.Keyboard.isKeyPressed(',')) {
                if (!_history.empty()) {
                    if (_histIdx<0) _histIdx=(int)_history.size()-1;
                    else if (_histIdx>0) _histIdx--;
                    _input=_history[_histIdx]; _cursor=_input.length(); _tabActive=false;
                } delay(120);
            }
            else if (M5Cardputer.Keyboard.isKeyPressed('/')) {
                if (_histIdx>=0) { _histIdx++; if(_histIdx>=(int)_history.size()){_histIdx=-1;_input="";}else _input=_history[_histIdx]; _cursor=_input.length(); _tabActive=false; } delay(120);
            }
            else if (M5Cardputer.Keyboard.isKeyPressed('k')) { _input="";_cursor=0;_tabActive=false; delay(80); }
            else if (M5Cardputer.Keyboard.isKeyPressed('c')) { g_lt.clear(); _tabActive=false; delay(80); }
            else { handled=false; }
            if (handled) { _draw(); return; }
        }

        if (st.del) { _inputDel(); _tabActive=false; _draw(); return; }

        if (st.enter) {
            if (_input.length()==0) { _draw(); return; }
            if (g_exec.running) { g_lt.push("busy (Ctrl+C to interrupt)", COL_WARN); _draw(); return; }
            if (_history.empty()||_history.back()!=_input) _history.push_back(_input);
            if ((int)_history.size()>LT_HIST) _history.erase(_history.begin());
            _histIdx=-1;
            g_lt.push(String("> ")+_input, COL_ACCENT);
            xSemaphoreTake(g_exec.cmdMtx, portMAX_DELAY);
            g_exec.cmd = _input;
            g_exec.pending = true;
            xSemaphoreGive(g_exec.cmdMtx);
            _input=""; _cursor=0;
            _draw(); return;
        }

        if (st.tab) { OsUI::waitRelease(); _autocomplete(); _draw(); return; }

        if (!st.word.empty()) {
            _tabActive=false;
            for (char c : st.word) {
                if ((int)_input.length()<LT_MAX_IN) {
                    _input=_input.substring(0,_cursor)+c+_input.substring(_cursor);
                    _cursor++;
                }
            }
            _draw();
        }
    }

private:
    LGFX_Sprite&        _canvas;
    TaskHandle_t        _taskHandle = nullptr;
    bool                _wifiWasOn = false;  // see onStart()/onStop()
    bool                _started    = false;
    String              _input;
    int                 _cursor     = 0;
    std::vector<String> _history;
    int                 _histIdx    = -1;
    int                 _tabIdx     = -1;
    String              _tabPrefix  = "";
    bool                _tabActive  = false;

    void _cursorLeft()  { if (_cursor>0) _cursor--; }
    void _cursorRight() { if (_cursor<(int)_input.length()) _cursor++; }
    void _cursorHome()  { _cursor=0; }
    void _cursorEnd()   { _cursor=_input.length(); }
    void _inputDel()    { if (_cursor>0){_input.remove(_cursor-1,1);_cursor--;} }

    static const int COMP_MAX = 180;
    const char* _completions[COMP_MAX] = {
        "help()","heap()","gc()","mem()","clear()","reboot()","exit()","quit()",
        "ls()","cat(\"","pwd()","rm(\"","mkdir(\"","cp(\"","mv(\"",
        "df()","touch(\"","ifconfig()","wget(\"","i2cscan()",
        "dump(","map(","filter(","reduce(",
        "split(","trim(","starts(","ends(",
        "tkeys(","tvals(","tlen(",
        "hex(","json_enc(",
        "run(\"","dofile(\"","require(\"",
        "os.heap()","os.psram()","os.millis()","os.micros()","os.stack()",
        "os.cpu()","os.flash()","os.bright(","os.reset()",
        "os.apps()","os.home()","os.launch(\"",
        "os.clock()","os.time()","os.date()",
        "os.remove(\"","os.rename(\"","os.gc()","os.mem()","os.core()",
        "fs.ls()","fs.ls(\"/sd\")","fs.read(\"","fs.write(\"",
        "fs.rm(\"","fs.mkdir(\"","fs.exists(\"",
        "fs.size(\"","fs.copy(\"","fs.rename(\"",
        "fs.free()","fs.total()",
        "io.open(\"","io.lines(\"","io.write(",
        "net.ip()","net.mac()","net.ssid()","net.rssi()",
        "net.status()","net.scan()","net.connect(\"",
        "net.disconnect()","net.get(\"","net.post(\"",
        "gpio.mode(","gpio.read(","gpio.write(",
        "gpio.aread(","gpio.awrite(","gpio.tone(","gpio.notone(",
        "i2c.begin()","i2c.scan()","i2c.write(","i2c.read(","i2c.end()",
        "bit.band(","bit.bor(","bit.bxor(","bit.bnot(",
        "bit.lshift(","bit.rshift(","bit.arshift(","bit.tohex(",
        "disp.cls()","disp.print(\"","disp.bright(","disp.w()","disp.h()",
        "print(","tostring(","tonumber(","type(","pairs(","ipairs(",
        "pcall(","xpcall(","error(","assert(",
        "string.format(","string.find(","string.match(","string.gmatch(",
        "string.sub(","string.len(","string.rep(","string.upper(",
        "string.lower(","string.byte(","string.char(","string.reverse(",
        "table.insert(","table.remove(","table.sort(","table.concat(",
        "table.unpack(","table.move(",
        "math.abs(","math.floor(","math.ceil(","math.sqrt(",
        "math.sin(","math.cos(","math.tan(","math.atan(",
        "math.random(","math.randomseed(","math.max(","math.min(",
        "math.log(","math.exp(","math.huge","math.pi","math.maxinteger",
        nullptr
    };

    String _tokenBeforeCursor() {
        int s=_cursor;
        while(s>0&&(isalnum(_input[s-1])||_input[s-1]=='.'||_input[s-1]=='_')) s--;
        return _input.substring(s,_cursor);
    }
    void _applyCompletion(const char* comp) {
        int end=_cursor,s=end;
        while(s>0&&(isalnum(_input[s-1])||_input[s-1]=='.'||_input[s-1]=='_')) s--;
        _input=_input.substring(0,s)+comp+_input.substring(end);
        _cursor=s+strlen(comp);
    }
    void _autocomplete() {
        String token=_tokenBeforeCursor();
        if (!token.length()) { g_lt.push("Tab: type prefix first",COL_DIM); return; }
        if (!_tabActive||token!=_tabPrefix) { _tabPrefix=token; _tabIdx=-1; }
        _tabActive=true;
        std::vector<int> matches;
        for (int i=0;_completions[i];i++)
            if (strncmp(_completions[i],_tabPrefix.c_str(),_tabPrefix.length())==0)
                matches.push_back(i);
        if (matches.empty()) { g_lt.push(String("no match: ")+_tabPrefix,COL_WARN); _tabActive=false; return; }
        if ((int)matches.size()==1) { _applyCompletion(_completions[matches[0]]); _tabActive=false; return; }
        _tabIdx=(_tabIdx+1)%(int)matches.size();
        _applyCompletion(_completions[matches[_tabIdx]]);
        if (_tabIdx==0) {
            String hint; for(int idx:matches){hint+=_completions[idx];hint+=" ";if((int)hint.length()>76){hint+="...";break;}}
            g_lt.push(hint,COL_DIM);
        }
    }

    // Lua terminal uses its own black-bg/white-text scheme (classic
    // console look) instead of the OS-wide white Win95 theme - the
    // default white-bg + yellow prompt combo was unreadable.
    static const uint16_t TERM_BG   = 0x0000; // black
    static const uint16_t TERM_TEXT = 0xFFFF; // white

    void _draw() {
        _canvas.fillSprite(TERM_BG);
        _canvas.setTextSize(1);

        // standard OS header (clock/wifi/battery), with busy spinner + heap
        // squeezed into the title text since the header is shared.
        String title = "Lua  " + String(ESP.getFreeHeap()/1024) + "K c" + String(xPortGetCoreID());
        if (g_exec.running) {
            static const char spin[]="/-\\|";
            static uint8_t si=0; si=(si+1)&3;
            title += " ";
            title += spin[si];
        }
        OsUI::drawHeader(_canvas, title.c_str(), COL_HEADER);
        _canvas.drawFastHLine(0, HEADER_H, SCREEN_W, COL_ACCENT);

        // input line — drawn inside the standard OS footer bar
        const int INPUT_Y = SCREEN_H - FOOTER_H + 3;
        const int DIVIDER_Y = SCREEN_H - FOOTER_H;
        _canvas.fillRect(0, DIVIDER_Y, SCREEN_W, FOOTER_H, COL_FOOTER);
        _canvas.drawFastHLine(0, DIVIDER_Y, SCREEN_W, COL_ACCENT);

        // prompt changes colour when busy
        uint16_t promptCol = g_exec.running ? COL_WARN : COL_TEXT;
        String full = String("> ")+_input;
        int cur=2+_cursor, visStart=(cur>=LT_LINE_W)?cur-LT_LINE_W+1:0;
        String vis=full.substring(visStart);
        if ((int)vis.length()>LT_LINE_W) vis=vis.substring(0,LT_LINE_W);
        int lc=cur-visStart;

        if (lc>0) { _canvas.setTextColor(promptCol,COL_FOOTER); _canvas.drawString(vis.substring(0,lc),2,INPUT_Y); }
        int cx=2+lc*6;
        _canvas.fillRect(cx,INPUT_Y,5,8,COL_ACCENT);
        if (lc<(int)vis.length()) { _canvas.setTextColor(COL_FOOTER,COL_ACCENT); _canvas.drawChar(vis[lc],cx,INPUT_Y); }
        if (lc+1<(int)vis.length()) { _canvas.setTextColor(promptCol,COL_FOOTER); _canvas.drawString(vis.substring(lc+1),cx+6,INPUT_Y); }

        // log
        const int LOG_TOP = HEADER_H + 1;
        g_lt.lock();
        for (int i=0;i<LT_VISIBLE;i++) {
            int idx=g_lt.scroll+i; if(idx>=g_lt.count) break;
            int ty=LOG_TOP+i*LT_LINE_H; if(ty+LT_LINE_H>DIVIDER_Y) break;
            _canvas.setTextColor(g_lt.colors[idx]?g_lt.colors[idx]:TERM_TEXT,TERM_BG);
            _canvas.drawString(g_lt.lines[idx],2,ty);
        }
        g_lt.unlock();

        { DispLock _lk; _canvas.pushSprite(0,0); }
    }
};
