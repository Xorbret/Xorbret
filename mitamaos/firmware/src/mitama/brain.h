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
#include "../system/sd_settings.h"
#include <WiFi.h>
#include "lines.h"
#include "../detect/detection.h"
#include "../detect/compendium.h"
#include "../net/kotodama.h"
#include "../detect/scry.h"
#include "../system/gps.h"
#include "../ui/mitama_ui.h"
#include "forms.h"

class MitamaBrain {
public:
    static MitamaBrain& get() { static MitamaBrain b; return b; }

    enum Mood : uint8_t { M_CALM, M_SMUG, M_CURIOUS, M_PASSIVE, M_CONTEMPT, M_ALARMED, M_SINCERE };

    // Call about once a second with fresh detection stats.
    void update(const DetectionEngine::Stats& st) {
        uint32_t now = millis();
        if (!_started) { _started = true; _sass = _loadSass(); Forms::get().begin(); _say(Lines::EV_BOOT, nullptr, false, M_CALM, now); }

        // ── events, most important first ──
        // One event line per check: a later event only speaks if nothing
        // more important did. Compendium news and Kotodama whispers stay
        // queued until there's room, and wait while a threat line is fresh.
        bool spoke = false;
        // New sightings: anything first seen since the last check.
        for (int i = st.recentN - 1; i >= 0; i--) {
            const auto& r = st.recent[i];
            if (r.firstMs <= _lastSightMs) continue;
            _lastSightMs = r.firstMs;
            uint8_t race = Sig::raceOf(r.type);
            { struct tm t; if (MUI::localNow(t) && t.tm_hour >= 1 && t.tm_hour < 5) Forms::get().setFlag(Forms::FL_NIGHT); }
            if (_sameDemon(r) ) continue;         // same demon within 60s: no repeat flavor
            Mood m = race == Sig::R_FIEND ? M_ALARMED
                   : race == Sig::R_FOUL  ? M_CONTEMPT
                   : (race == Sig::R_VILE || race == Sig::R_NIGHT) ? M_PASSIVE : M_CURIOUS;
            // Threats always speak (even at sass 0 the facts are on screen;
            // the line is the dry one-liner on top). Others respect sass.
            if (Sig::isThreat(r.type) || _sass > 0) {
                _say((Lines::Event)(Lines::EV_VILE + race), &r, true, m, now, _countRace(st, race));
                spoke = true;
                if (Sig::isThreat(r.type)) _threatLineMs = now;
            }
        }
        bool threatFresh = _threatLineMs && now - _threatLineMs < 8000;
        // A first-ever meeting speaks over an ordinary sighting line: a new
        // page in the Compendium (or a finished race) is the bigger news.
        // Threats never get the "race complete" cheer.
        Compendium::News news;
        if (!threatFresh && _sass > 0 && Compendium::get().takeNews(news)) {
            DetectionEngine::Sighting s; s.type = news.type;
            bool threat = Sig::isThreat(news.type);
            _say(news.raceDone && !threat ? Lines::EV_RACE_DONE : Lines::EV_NEW_DEMON, &s, false,
                 threat ? M_ALARMED : M_SMUG, now);
            spoke = true;
        }
        if (now - _cardAt > 30000 || !_cardAt) { _cardAt = now | 1; _shareCard(); }
        Kotodama::Arrival ka;
        if (!spoke && !threatFresh && _sass > 0 && Kotodama::get().takeArrival(ka)) {
            static char crewName[28];
            Kotodama::get().nameOf(ka.num, crewName, sizeof(crewName));
            DetectionEngine::Sighting s; s.detail = crewName;
            _say(ka.isFriend ? Lines::EV_CREW_FRIEND : Lines::EV_CREW_STRANGER, &s, false,
                 ka.isFriend ? M_SMUG : M_CURIOUS, now);
            spoke = true;
        }
        Kotodama::Msg km;
        if (!spoke && !threatFresh && _sass > 0 && Kotodama::get().takeIncoming(km)) {
            static char who[28];
            Kotodama::get().nameOf(km.from, who, sizeof(who));
            DetectionEngine::Sighting s; s.detail = who;
            _say(Lines::EV_WHISPER, &s, false, M_CURIOUS, now);
            spoke = true;
        }
        if (_hadThreat && st.threats == 0 && !spoke) {
            _hadThreat = false;
            _say(Lines::EV_CLEAR, nullptr, false, M_SMUG, now);
            spoke = true;
            _sincerePending = random(4) == 0;       // rare, earned
            _sincereAt = now + 20000;
        }
        if (st.threats > 0) _hadThreat = true;
        if (_sincerePending && now > _sincereAt && _sass > 0 && !spoke) {
            _sincerePending = false;
            _say(Lines::EV_SINCERE, nullptr, false, M_SINCERE, now);
            spoke = true;
        }
        bool fix = Gps::get().hasFix();
        if (fix && !_hadFix && _sass > 0 && !spoke) { _say(Lines::EV_GPS_FIX, nullptr, false, M_SMUG, now); spoke = true; }
        _hadFix = fix;
        bool scry = ScryLogger::get().recording();
        if (scry && !_hadScry && _sass > 0 && !spoke) { _say(Lines::EV_SCRY_ON, nullptr, false, M_CURIOUS, now); spoke = true; }
        _hadScry = scry;
        if (scry && fix && ScryLogger::get().wifiCount() >= 100) Forms::get().setFlag(Forms::FL_SCRY);
        if (Kotodama::get().sentCount()) Forms::get().setFlag(Forms::FL_KOTO);
        // A newly earned form: one announcement at a time, 20 s apart.
        uint8_t form;
        if (!spoke && !threatFresh && _sass > 0 && now - _formLineMs > 20000 && Forms::get().takeNew(form)) {
            _formName = Forms::name(form);
            _say(Lines::EV_FORM, nullptr, false, M_SMUG, now);
            _formLineMs = now;
            spoke = true;
        }
        bool online = WiFi.status() == WL_CONNECTED;
        if (online && !_wasOnline && now > 15000 && _sass > 0 && !spoke) _say(Lines::EV_WIFI_JOIN, nullptr, false, M_CALM, now);
        _wasOnline = online;

        // ── idle cadence ──
        if (_sass > 0 && now - _lastLineMs > _idleGapMs) {
            bool lore = random(3) == 0;
            Lines::Event ev = lore ? Lines::EV_LORE : (!online && random(2) ? Lines::EV_OFFLINE : Lines::EV_IDLE);
            _say(ev, nullptr, false, _temper() == Lines::T_WEARY ? M_PASSIVE : M_CALM, now);
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
        SdSettings::changed();
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
    uint32_t _lastLineMs = 0, _idleGapMs = 150000, _lastSightMs = 0, _threatLineMs = 0;
    bool     _hadThreat = false, _hadFix = false, _hadScry = false, _wasOnline = false;
    bool     _sincerePending = false;
    uint32_t _sincereAt = 0;
    uint32_t _formLineMs = 0, _cardAt = 0;

    // What the crew sees of us (Kotodama sends it with our name).
    static void _shareCard() {
        auto& comp = Compendium::get();
        auto& fm = Forms::get();
        Kotodama::Card c;
        c.form = fm.current();
        c.forms = fm.unlockedCount();
        c.species = comp.metCount();
        c.speciesTotal = Compendium::total();
        for (uint8_t r = 0; r < Sig::R_COUNT; r++) {
            int size = Compendium::raceSize(r);
            if (size > 1 && comp.raceMet(r) == size) c.races |= 1 << r;
        }
        for (uint8_t t = 0; t < Sig::T_COUNT; t++) if (Compendium::listed(t)) c.encounters += comp.entry(t).count;
        Kotodama::get().setMyCard(c);
    }
    const char* _formName = "";
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
    // For a sighting, lines written for that exact device join the
    // family's lines; their index is offset by BANK_N.
    // speciesLines: also consider lines written for r's device. Off for
    // Compendium news, so "new page" doesn't come out as a plain sighting.
    void _say(Lines::Event ev, const DetectionEngine::Sighting* r, bool speciesLines, Mood m, uint32_t now, int n = 0) {
        Lines::Temper tmp = _temper();
        int cand[48]; int nc = 0;
        for (int pass = 0; pass < 2 && nc == 0; pass++) {
            for (int i = 0; i < Lines::BANK_N && nc < 48; i++) {
                const auto& L = Lines::BANK[i];
                if (L.ev != ev) continue;
                // temperament-specific lines only when that temperament applies
                if (pass == 0 && L.temper != Lines::T_ANY && L.temper != tmp) continue;
                if (!_recent(i) || pass == 1) cand[nc++] = i;
            }
            for (int i = 0; r && speciesLines && i < Lines::SP_N && nc < 48; i++)
                if (Lines::SP_BANK[i].sp == r->type && (!_recent(Lines::BANK_N + i) || pass == 1))
                    cand[nc++] = Lines::BANK_N + i;
        }
        if (nc == 0) return;
        // Favor temperament-specific lines when they exist.
        int pick = cand[random(nc)];
        for (int tries = 0; tries < 2; tries++) {
            int c = cand[random(nc)];
            if (c < Lines::BANK_N && Lines::BANK[c].temper == tmp && tmp != Lines::T_ANY) { pick = c; break; }
        }
        memmove(_lastIdx + 1, _lastIdx, sizeof(int) * 3);
        _lastIdx[0] = pick;
        _fill(pick < Lines::BANK_N ? Lines::BANK[pick].text : Lines::SP_BANK[pick - Lines::BANK_N].text, r, n);
        _mood = m;
        _serial++;
        _lastLineMs = now;
        _idleGapMs = _nextGap();
    }

    bool _recent(int i) const {
        for (int k : _lastIdx) if (k == i) return true;
        return false;
    }

    void _fill(const char* t, const DetectionEngine::Sighting* r, int n) {
        size_t k = 0;
        while (*t && k < sizeof(_line) - 1) {
            const char* sub = nullptr;
            char num[8];
            if (strncmp(t, "{sp}", 4) == 0)     { sub = r ? Sig::typeName(r->type) : "something"; t += 4; }
            else if (strncmp(t, "{d}", 3) == 0) { sub = r && r->detail ? r->detail : (r ? Sig::typeName(r->type) : "Something"); t += 3; }
            else if (strncmp(t, "{n}", 3) == 0) { snprintf(num, sizeof(num), "%d", n); sub = num; t += 3; }
            else if (strncmp(t, "{f}", 3) == 0) { sub = _formName; t += 3; }
            else if (strncmp(t, "{r}", 3) == 0) { sub = r ? Sig::raceName(Sig::raceOf(r->type)) : "demon"; t += 3; }
            if (sub) { while (*sub && k < sizeof(_line) - 1) _line[k++] = *sub++; }
            else _line[k++] = *t++;
        }
        _line[k] = 0;
    }
};
