#pragma once
// Settings that survive updates: a copy of the OS's saved settings (Wi-Fi
// networks, brightness, timezone, sass, WiGLE account, bookmarks, Grimoire
// notes, Kotodama region and name) in
// /MitamaOS/settings.conf on the SD card.
//
// The settings still live in NVS, where the apps read them. This file is
// the backup, kept in step by a generation number stored in both places:
//  - boot(): if the SD copy is newer (or NVS was wiped by an install), or
//    was edited on a PC since we last wrote it (its fingerprint changed),
//    it is written back into NVS; if NVS is newer, the copy is refreshed.
//  - changed(): call after saving a setting. Bumps the generation and
//    writes the file a couple of seconds later (tick()), so holding a key
//    on the brightness slider doesn't hammer the card.
// Plain text, one key=value per line under [namespace] headers, so it can
// be read or edited on a PC. Only the keys listed in _SCHEMA are ever read
// back; deleting a line under a [section] that's in the file deletes that
// setting. Saves are atomic: write .tmp, then swap it in.
#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>
#include "../config.h"

namespace SdSettings {

static constexpr const char* PATH = "/MitamaOS/settings.conf";
static constexpr const char* TMP  = "/MitamaOS/settings.tmp";

enum Kind : uint8_t { K_STR, K_INT, K_U8 };
// A key, or `count` indexed keys (s0, s1, ...) when count > 0.
struct Key { const char* ns; const char* key; Kind kind; uint8_t count; };
static const Key _SCHEMA[] = {
    { "wifi-config", "s",      K_STR, MAX_WIFI_SLOTS },
    { "wifi-config", "p",      K_STR, MAX_WIFI_SLOTS },
    { "display",     "bright", K_INT, 0 },
    { "timecfg",     "tz_offset", K_INT, 0 },
    { "timecfg",     "tz_zone", K_U8,  0 },
    { "mitama",      "sass",   K_U8,  0 },
    { "mitama",      "form",   K_U8,  0 },
    { "forms",       "flags",  K_INT, 0 },
    { "forms",       "seen",   K_INT, 0 },
    { "wigle",       "name",   K_STR, 0 },
    { "wigle",       "token",  K_STR, 0 },
    { "bmarks",      "cnt",    K_INT, 0 },
    { "bmarks",      "u",      K_STR, 8 },
    { "notes",       "n",      K_STR, 5 },
    { "kotodama",    "region", K_U8,  0 },
    { "kotodama",    "on",     K_U8,  0 },
    { "tsukumo",     "gpspin", K_U8,  0 },
    { "kotodama",    "name",   K_STR, 0 },
};

inline bool     _dirty = false;
inline uint32_t _dirtyMs = 0;

inline uint32_t _nvsGen() {
    Preferences p; p.begin("mitamaos", true);
    uint32_t g = p.getUInt("gen", 0); p.end();
    return g;
}
inline void _setNvsGen(uint32_t g) {
    Preferences p; p.begin("mitamaos", false); p.putUInt("gen", g); p.end();
}

// Fingerprint (FNV-1a) of the file as we last wrote or read it, so a PC
// edit - which doesn't bump the generation - is still noticed.
inline uint32_t _fileHash(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t h = 2166136261u;
    uint8_t buf[128];
    int n;
    while ((n = f.read(buf, sizeof(buf))) > 0)
        for (int i = 0; i < n; i++) { h ^= buf[i]; h *= 16777619u; }
    f.close();
    return h;
}
inline uint32_t _knownHash() {
    Preferences p; p.begin("mitamaos", true);
    uint32_t h = p.getUInt("fhash", 0); p.end();
    return h;
}
inline void _setKnownHash(uint32_t h) {
    Preferences p; p.begin("mitamaos", false); p.putUInt("fhash", h); p.end();
}

inline String _escape(const String& s) {
    String o; o.reserve(s.length() + 4);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o;
}
inline String _unescape(const String& s) {
    String o; o.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.length()) {
            char n = s[++i];
            o += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else o += c;
    }
    return o;
}

// The schema entry for ns/key, or nullptr if it isn't one we restore.
inline const Key* _find(const String& ns, const String& key) {
    for (auto& k : _SCHEMA) {
        if (ns != k.ns) continue;
        if (!k.count) { if (key == k.key) return &k; continue; }
        size_t pl = strlen(k.key);
        if (key.length() <= pl || !key.startsWith(k.key)) continue;
        String idx = key.substring(pl);
        bool digits = true;
        for (size_t i = 0; i < idx.length(); i++) if (!isdigit((unsigned char)idx[i])) digits = false;
        if (digits && idx.toInt() < k.count) return &k;
    }
    return nullptr;
}

// Reads the file's generation (0 = no file).
inline uint32_t _fileGen(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return 0;
    uint32_t g = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("gen=")) { g = line.substring(4).toInt(); break; }
        if (line.startsWith("[")) break;
    }
    f.close();
    return g;
}

inline void _write(uint32_t gen) {
    if (SD.cardType() == CARD_NONE) return;
    if (!SD.exists("/MitamaOS")) SD.mkdir("/MitamaOS");
    File f = SD.open(TMP, FILE_WRITE);
    if (!f) return;
    f.print("# MitamaOS settings, kept on the SD card so updates don't lose them.\n"
            "# Restored automatically after an install. Wi-Fi passwords and the\n"
            "# WiGLE token are stored here in plain text: keep the card private.\n");
    f.printf("gen=%lu\n", (unsigned long)gen);
    const char* openNs = nullptr;
    Preferences p;
    for (auto& k : _SCHEMA) {
        if (!openNs || strcmp(openNs, k.ns) != 0) {
            if (openNs) p.end();
            p.begin(k.ns, true);
            openNs = k.ns;
            f.printf("\n[%s]\n", k.ns);
        }
        int n = k.count ? k.count : 1;
        for (int i = 0; i < n; i++) {
            String key = k.count ? String(k.key) + i : String(k.key);
            if (!p.isKey(key.c_str())) continue;
            String v = k.kind == K_STR ? p.getString(key.c_str(), "")
                     : k.kind == K_INT ? String(p.getInt(key.c_str(), 0))
                     :                   String(p.getUChar(key.c_str(), 0));
            f.printf("%s=%s\n", key.c_str(), _escape(v).c_str());
        }
    }
    if (openNs) p.end();
    bool ok = f.getWriteError() == 0;
    f.flush();
    f.close();
    if (!ok) { SD.remove(TMP); return; }
    SD.remove(PATH);            // FAT can't rename over a file
    SD.rename(TMP, PATH);
    _setKnownHash(_fileHash(PATH));
}

static constexpr int _NS_MAX = 16, _SLOT_MAX = 64;

// Index of one key slot (schema row k, index i) in a flat "seen" table.
inline int _slot(const Key* k, int i) {
    int n = 0;
    for (auto& s : _SCHEMA) { if (&s == k) return n + i; n += s.count ? s.count : 1; }
    return -1;
}

inline void _restore(const char* path, uint32_t gen) {
    File f = SD.open(path, FILE_READ);
    if (!f) return;
    String ns;
    String sections[_NS_MAX]; int nsN = 0;      // [sections] the file has
    bool seen[_SLOT_MAX] = {};
    Preferences p;
    bool open = false;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        String t = line; t.trim();
        if (!t.length() || t[0] == '#') continue;
        if (t[0] == '[') {
            if (open) { p.end(); open = false; }
            int close = t.indexOf(']');
            ns = t.substring(1, close > 0 ? close : t.length());
            ns.trim();
            if (nsN < _NS_MAX) sections[nsN++] = ns;
            continue;
        }
        int eq = line.indexOf('=');
        if (eq <= 0 || !ns.length()) continue;
        String key = line.substring(0, eq), val = _unescape(line.substring(eq + 1));
        key.trim();                             // values keep their spaces (passwords)
        const Key* k = _find(ns, key);
        if (!k) continue;
        int sl = _slot(k, k->count ? key.substring(strlen(k->key)).toInt() : 0);
        if (sl >= 0 && sl < _SLOT_MAX) seen[sl] = true;
        if (!open) { p.begin(ns.c_str(), false); open = true; }
        if (k->kind == K_STR)      p.putString(key.c_str(), val);
        else if (k->kind == K_INT) p.putInt(key.c_str(), val.toInt());
        else                       p.putUChar(key.c_str(), (uint8_t)val.toInt());
    }
    if (open) p.end();
    f.close();
    // Lines removed from a section that is in the file: delete those
    // settings too. (A section missing altogether - e.g. a file from an
    // older build - leaves its settings alone.)
    for (auto& k : _SCHEMA) {
        bool has = false;
        for (int i = 0; i < nsN; i++) if (sections[i] == k.ns) has = true;
        if (!has) continue;
        int n = k.count ? k.count : 1;
        Preferences q; q.begin(k.ns, false);
        for (int i = 0; i < n; i++) {
            int sl = _slot(&k, i);
            if (sl < 0 || sl >= _SLOT_MAX || seen[sl]) continue;
            String key = k.count ? String(k.key) + i : String(k.key);
            if (q.isKey(key.c_str())) q.remove(key.c_str());
        }
        q.end();
    }
    _setNvsGen(gen);
}

// Call once after SD.begin(), before anything reads its settings.
inline void boot() {
    if (SD.cardType() == CARD_NONE) return;
    // Power died between "remove old" and "rename new": finish the swap.
    if (!SD.exists(PATH) && SD.exists(TMP)) SD.rename(TMP, PATH);
    const char* path = SD.exists(PATH) ? PATH : nullptr;
    uint32_t fg = path ? _fileGen(path) : 0, ng = _nvsGen();
    // Changed since we last wrote it, or never written by us at all (a
    // file made on a PC, with or without a gen= line): take it.
    bool edited = path && _fileHash(path) != _knownHash();
    if (path && (fg > ng || edited)) {
        _restore(path, max(fg, ng));
        _setKnownHash(_fileHash(path));
    }
    else if (ng > fg || !path) { if (!ng) _setNvsGen(ng = 1); _write(ng); }
}

// Call after saving a setting to NVS.
inline void changed() {
    _setNvsGen(_nvsGen() + 1);
    _dirty = true;
    _dirtyMs = millis();
}

// Main loop: writes the file once settings have been quiet for 2 s.
inline void tick() {
    if (!_dirty || millis() - _dirtyMs < 2000) return;
    _dirty = false;
    _write(_nvsGen());
}

// Before a restart: writes now if a change is still waiting.
inline void flush() {
    if (!_dirty) return;
    _dirty = false;
    _write(_nvsGen());
}

} // namespace SdSettings
