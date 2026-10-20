#pragma once
// The Mitama's brain (outline §14, §15): turns detection and system events
// into a mood and a line from the line bank, with the locked temperament
// (time of day -> Energy, uptime -> Patience) and sass dial (0-3) shaping
// how often it talks unprompted.
//
// Invariants: facts are never suppressed (they're on screen regardless);
// the same demon within ~60s doesn't repeat flavor; nothing here blocks.
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include "lines.h"
#include "../detect/detection.h"
#include "../detect/scry.h"
#include "../system/gps.h"
#include "../ui/mitama_ui.h"

class MitamaBrain {
public:
    static MitamaBrain& get() { static MitamaBrain b; return b; }

    enum Mood : uint8_t { M_CALM, M_SMUG, M_CURIOUS, M_PASSIVE, M_CONTEMPT, M_ALARMED, M_SINCERE };

    // Call about once a second with fresh detection stats.
    void update(const DetectionEngine::Stats& st) {
        uint32_t now = millis();
        if (!_started) { _started = true; _sass = _loadSass(); _say(Lines::EV_BOOT, nullptr, M_CALM, now); }

        // ── events, most important first ──
        // New sightings: anything first seen since the last check.
        for (int i = st.recentN - 1; i >= 0; i--) {
            const auto& r = st.recent[i];
            if (r.firstMs <= _lastSightMs) continue;
            _lastSightMs = r.firstMs;
            uint8_t race = Sig::raceOf(r.type);
            if (_sameDemon(r) ) continue;         // same demon within 60s: no repeat flavor
            Mood m = race == Sig::R_FIEND ? M_ALARMED
                   : race == Sig::R_FOUL  ? M_CONTEMPT
                   : (race == Sig::R_VILE || race == Sig::R_NIGHT) ? M_PASSIVE : M_CURIOUS;
            // Threats always speak (even at sass 0 the facts are on screen;
            // the line is the dry one-liner on top). Others respect sass.
            if (Sig::isThreat(r.type) || _sass > 0)
                _say((Lines::Event)(Lines::EV_VILE + race), &r, m, now, _countRace(st, race));
        }
        if (_hadThreat && st.threats == 0) {
            _hadThreat = false;
            _say(Lines::EV_CLEAR, nullptr, M_SMUG, now);
            _sincerePending = random(4) == 0;       // rare, earned
            _sincereAt = now + 20000;
        }
        if (st.threats > 0) _hadThreat = true;
        if (_sincerePending && now > _sincereAt && _sass > 0) {
            _sincerePending = false;
            _say(Lines::EV_SINCERE, nullptr, M_SINCERE, now);
        }
        bool fix = Gps::get().hasFix();
        if (fix && !_hadFix && _sass > 0) _say(Lines::EV_GPS_FIX, nullptr, M_SMUG, now);
        _hadFix = fix;
        bool scry = ScryLogger::get().recording();
        if (scry && !_hadScry && _sass > 0) _say(Lines::EV_SCRY_ON, nullptr, M_CURIOUS, now);
        _hadScry = scry;
        bool online = WiFi.status() == WL_CONNECTED;
        if (online && !_wasOnline && now > 15000 && _sass > 0) _say(Lines::EV_WIFI_JOIN, nullptr, M_CALM, now);
        _wasOnline = online;

        // ── idle cadence ──
        if (_sass > 0 && now - _lastLineMs > _idleGapMs) {
            bool lore = random(3) == 0;
            Lines::Event ev = lore ? Lines::EV_LORE : (!online && random(2) ? Lines::EV_OFFLINE : Lines::EV_IDLE);
            _say(ev, nullptr, _temper() == Lines::T_WEARY ? M_PASSIVE : M_CALM, now);
        }

        // Mood relaxes back to calm a minute after the last event, unless a
        // threat is still around.
        if (st.threats > 0) _mood = M_ALARMED;
        else if (now - _lastLineMs > 60000) _mood = M_CALM;
    }

    const char* line() const { return _line; }
    uint32_t lineSerial() const { return _serial; }   // changes with every new line
    uint32_t lineAgeMs() const { return millis() - _lastLineMs; }
    Mood mood() const { return _mood; }

    uint16_t moodColor() const {
        switch (_mood) {
            case M_SMUG:     return MUI::GREEN;
            case M_CURIOUS:  return MUI::CYAN;
            case M_PASSIVE:  return MUI::MAGENTA;
            case M_CONTEMPT: return MUI::BONES;
            case M_ALARMED:  return MUI::RED;
            case M_SINCERE:  return MUI::CYAN;
            default:         return MUI::YELLOW;
        }
    }

    // Sass dial 0-3 (outline §15): 0 Mute Muse, 1 Dry, 2 Default, 3 Insufferable.
    uint8_t sass() const { return _sass; }
    void cycleSass() {
        _sass = (_sass + 1) % 4;
        Preferences p; p.begin("mitama", false); p.putUChar("sass", _sass); p.end();
        _idleGapMs = _nextGap();
    }
    static const char* sassName(uint8_t s) {
        static const char* N[] = { "Mute Muse", "Dry", "Default", "Insufferable" };
        return N[s & 3];
    }

private:
    bool     _started = false;
    volatile uint32_t _serial = 0;
    char     _line[96] = "";
    Mood     _mood = M_CALM;
    uint8_t  _sass = 2;
    uint32_t _lastLineMs = 0, _idleGapMs = 150000, _lastSightMs = 0;
    bool     _hadThreat = false, _hadFix = false, _hadScry = false, _wasOnline = false;
    bool     _sincerePending = false;
    uint32_t _sincereAt = 0;
    // last few demons spoken about, for the 60s collapse rule
    struct Seen { uint8_t type; uint8_t mac[6]; uint32_t ms; } _seen[6] = {};
    uint8_t  _seenNext = 0;
    int      _lastIdx[4] = { -1, -1, -1, -1 };   // recently used lines, to avoid repeats

    static uint8_t _loadSass() {
        Preferences p; p.begin("mitama", true);
        uint8_t s = p.getUChar("sass", 2); p.end();
        return s > 3 ? 2 : s;
    }

    // Time of day -> Energy tier; uptime -> Patience (locked §15).
    static Lines::Temper _temper() {
        if (millis() > 4UL * 3600 * 1000) return Lines::T_WEARY;
        struct tm t;
        if (!MUI::localNow(t)) return Lines::T_ANY;   // no clock: fatigue only
        int h = t.tm_hour;
        if (h >= 6 && h < 11) return Lines::T_FRESH;
        if (h >= 21 || h < 1) return Lines::T_SLEEPY;
        if (h >= 1 && h < 6)  return Lines::T_DROWSY;
        return Lines::T_ANY;
    }

    // Next unprompted remark: sass-scaled, stretched when sleepy.
    uint32_t _nextGap() const {
        uint32_t lo, hi;
        switch (_sass) {
            case 1:  lo = 600; hi = 900; break;     // ~10-15 min
            case 3:  lo = 60;  hi = 120; break;     // ~1-2 min
            default: lo = 180; hi = 360; break;     // ~3-6 min
        }
        uint32_t g = (lo + random(hi - lo + 1)) * 1000UL;
        Lines::Temper t = _temper();
        if (t == Lines::T_SLEEPY || t == Lines::T_DROWSY) g = g * 5 / 2;
        return g;
    }

    bool _sameDemon(const DetectionEngine::Sighting& r) {
        uint32_t now = millis();
        for (auto& s : _seen)
            if (s.ms && s.type == r.type && memcmp(s.mac, r.mac, 6) == 0 && now - s.ms < 60000) return true;
        Seen& s = _seen[_seenNext++ % 6];
        s.type = r.type; memcpy(s.mac, r.mac, 6); s.ms = now;
        return false;
    }

    static int _countRace(const DetectionEngine::Stats& st, uint8_t race) {
        int n = 0;
        for (int i = 0; i < st.recentN; i++) if (Sig::raceOf(st.recent[i].type) == race) n++;
        return n;
    }

    // Picks a line for the event (temperament-matched lines preferred,
    // avoiding the last few used), fills its slots and makes it current.
    void _say(Lines::Event ev, const DetectionEngine::Sighting* r, Mood m, uint32_t now, int n = 0) {
        Lines::Temper tmp = _temper();
        int cand[24]; int nc = 0;
        for (int pass = 0; pass < 2 && nc == 0; pass++) {
            for (int i = 0; i < Lines::BANK_N && nc < 24; i++) {
                const auto& L = Lines::BANK[i];
                if (L.ev != ev) continue;
                bool fits = pass == 1 || L.temper == Lines::T_ANY || L.temper == tmp;
                // temperament-specific lines only when that temperament applies
                if (pass == 0 && L.temper != Lines::T_ANY && L.temper != tmp) fits = false;
                if (!fits) continue;
                bool recent = false;
                for (int k : _lastIdx) if (k == i) recent = true;
                if (!recent || pass == 1) cand[nc++] = i;
            }
        }
        if (nc == 0) return;
        // Favor temperament-specific lines when they exist.
        int pick = cand[random(nc)];
        for (int tries = 0; tries < 2; tries++) {
            int c = cand[random(nc)];
            if (Lines::BANK[c].temper == tmp && tmp != Lines::T_ANY) { pick = c; break; }
        }
        memmove(_lastIdx + 1, _lastIdx, sizeof(int) * 3);
        _lastIdx[0] = pick;
        _fill(Lines::BANK[pick].text, r, n);
        _mood = m;
        _serial++;
        _lastLineMs = now;
        _idleGapMs = _nextGap();
    }

    void _fill(const char* t, const DetectionEngine::Sighting* r, int n) {
        size_t k = 0;
        while (*t && k < sizeof(_line) - 1) {
            const char* sub = nullptr;
            char num[8];
            if (strncmp(t, "{sp}", 4) == 0)     { sub = r ? Sig::typeName(r->type) : "something"; t += 4; }
            else if (strncmp(t, "{d}", 3) == 0) { sub = r && r->detail ? r->detail : (r ? Sig::typeName(r->type) : "Something"); t += 3; }
            else if (strncmp(t, "{n}", 3) == 0) { snprintf(num, sizeof(num), "%d", n); sub = num; t += 3; }
            if (sub) { while (*sub && k < sizeof(_line) - 1) _line[k++] = *sub++; }
            else _line[k++] = *t++;
        }
        _line[k] = 0;
    }
};
