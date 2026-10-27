#pragma once
// Ki: the Mitama's experience. It comes from what MitamaOS is for (meeting
// devices, filling the Compendium, Scry drives, Crew, a daily visit) and
// never from anything that transmits at other devices. 50 levels in ten
// ranks. Daily limits stop farming (a rotating Bluetooth address would
// otherwise be a new "device" every few minutes).
//
// Most sources are read as changes in counters the OS already keeps, so
// nothing else has to call in; WiGLE uploads and omikuji call add().
// Stored in NVS "ki" and mirrored to settings.conf, so updates keep it.
// A device that had progress before ki existed starts from its Compendium.
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include "../detect/compendium.h"
#include "../detect/scry.h"
#include "../net/kotodama.h"
#include "../system/sd_settings.h"
#include "forms.h"

class Ki {
public:
    static Ki& get() { static Ki k; return k; }

    enum Source : uint8_t {
        S_ENC, S_SPECIES, S_RACE, S_THREAT, S_SCRY, S_WIGLE,
        S_KOTO, S_CREW, S_FRIEND, S_FORM, S_OMIKUJI, S_VISIT, S_COUNT
    };
    static constexpr int MAX_LEVEL = 50;

    static const char* sourceName(uint8_t s) {
        static const char* const N[S_COUNT] = {
            "Devices met", "New pages", "Races done", "Threats seen", "Scry", "WiGLE",
            "Kotodama", "New crew", "Friends", "Forms", "Omikuji", "Daily visit",
        };
        return s < S_COUNT ? N[s] : "?";
    }
    static const char* rankName(int r) {
        static const char* const R[10] = {
            "Kodama", "Kakure", "Hitodama", "Tsukumo", "Shikigami",
            "Onmyoji", "Kannushi", "Tengu", "Ryujin", "Kami",
        };
        return R[constrain(r, 0, 9)];
    }
    // Ki needed to reach level `l` (level 1 = 0).
    static uint32_t need(int l) { l = constrain(l, 1, MAX_LEVEL); uint32_t k = l - 1; return 12 * k * k + 40 * k; }
    static int levelFor(uint32_t ki) { int l = 1; while (l < MAX_LEVEL && ki >= need(l + 1)) l++; return l; }

    void begin() {
        Preferences p; p.begin("ki", true);
        bool have = p.isKey("total");
        _total = (uint32_t)p.getInt("total", 0);
        _day = (uint32_t)p.getInt("day", 0);
        _streak = p.getUChar("streak", 0);
        String d = p.getString("d", "");
        p.end();
        _parseToday(d);
        _baseline();
        if (!have) _backfill();
        _level = levelFor(_total);
        _began = true;
    }

    // Main loop, about once a second.
    void tick() {
        if (!_began) return;
        uint32_t now = millis();
        _rollDay();
        _poll();
        if (_dirty && (_urgent || now - _savedMs > 30000)) _save();
    }

    // A deed that isn't a counter elsewhere (WiGLE upload, omikuji draw).
    void add(Source s) {
        static const uint8_t KI[S_COUNT] = { 1, 50, 150, 10, 1, 25, 2, 30, 20, 40, 5, 0 };
        _award(s, KI[s]);
    }

    uint32_t total() const { return _total; }
    int level() const { return _level; }
    int rank() const { return (_level - 1) / 5; }
    uint8_t streak() const { return _streak; }
    uint16_t today(uint8_t s) const { return s < S_COUNT ? _today[s] : 0; }
    uint32_t todayTotal() const { uint32_t n = 0; for (auto v : _today) n += v; return n; }
    // Progress through the current level, 0..1 (1 at the top level).
    float progress() const {
        if (_level >= MAX_LEVEL) return 1.0f;
        uint32_t a = need(_level), b = need(_level + 1);
        return (float)(_total - a) / (float)(b - a);
    }
    uint32_t toNext() const { return _level >= MAX_LEVEL ? 0 : need(_level + 1) - _total; }

    // A level gained since the last call; `rankUp` when it began a new rank.
    bool takeLevelUp(int& lvl, bool& rankUp) {
        if (_announced >= _level) return false;
        int from = _announced;
        _announced = _level;
        lvl = _level;
        rankUp = (from - 1) / 5 != (_level - 1) / 5;
        return from > 0;    // the first call after boot just catches up
    }

private:
    // Daily limit per source, in ki (0 = none).
    static uint16_t _cap(uint8_t s) {
        static const uint16_t C[S_COUNT] = { 200, 0, 0, 100, 300, 25, 40, 150, 100, 0, 5, 0 };
        return C[s];
    }

    bool     _began = false, _dirty = false, _urgent = false;
    uint32_t _total = 0, _day = 0, _savedMs = 0;
    uint8_t  _streak = 0;
    int      _level = 1, _announced = 0;
    uint16_t _today[S_COUNT] = {};
    // last seen values of the counters we read
    uint32_t _enc = 0, _threat = 0, _scry = 0, _scryRest = 0, _sent = 0, _crew = 0;
    int      _species = 0, _races = 0, _forms = 0, _friends = -1;   // -1: friends.txt not read yet

    void _award(uint8_t s, uint32_t ki) {
        if (!ki || s >= S_COUNT) return;
        uint16_t cap = _cap(s);
        if (cap) {
            if (_today[s] >= cap) return;
            ki = min<uint32_t>(ki, cap - _today[s]);
        }
        _today[s] = (uint16_t)min<uint32_t>(0xFFFF, _today[s] + ki);
        _total += ki;
        _dirty = true;
        int l = levelFor(_total);
        if (l != _level) { _level = l; _urgent = true; }
    }

    // Counter readings, taken as they stand now.
    struct Reading { uint32_t enc, threat; int species, races; };
    static Reading _read() {
        auto& c = Compendium::get();
        Reading r{ 0, 0, c.metCount(), 0 };
        for (uint8_t t = 0; t < Sig::T_COUNT; t++) {
            if (!Compendium::listed(t)) continue;
            uint32_t n = c.entry(t).count;
            r.enc += n;
            uint8_t race = Sig::raceOf(t);
            if (race == Sig::R_FIEND || race == Sig::R_FOUL) r.threat += n;
        }
        for (uint8_t race = 0; race < Sig::R_COUNT; race++) {
            int size = Compendium::raceSize(race);
            if (size > 1 && c.raceMet(race) == size) r.races++;
        }
        return r;
    }

    void _baseline() {
        Reading r = _read();
        _enc = r.enc; _threat = r.threat; _species = r.species; _races = r.races;
        _forms = Forms::get().unlockedCount();
        auto& k = Kotodama::get();
        _sent = k.sentCount(); _crew = k.crewNewCount(); _friends = k.friendCount();
        _scry = ScryLogger::get().wifiCount();
    }

    // First run on a device with history: credit what the Compendium shows,
    // without daily limits (encounters capped at 1000).
    void _backfill() {
        Reading r = _read();
        _total = min<uint32_t>(r.enc, 1000) + 50u * r.species + 150u * r.races + 40u * max(0, _forms - 1);
        _dirty = _urgent = true;
        _announced = levelFor(_total);   // no fanfare for levels from the past
    }

    void _poll() {
        Reading r = _read();
        if (r.enc > _enc) _award(S_ENC, r.enc - _enc);
        if (r.threat > _threat) _award(S_THREAT, 10 * (r.threat - _threat));
        if (r.species > _species) _award(S_SPECIES, 50 * (r.species - _species));
        if (r.races > _races) _award(S_RACE, 150 * (r.races - _races));
        _enc = r.enc; _threat = r.threat; _species = r.species; _races = r.races;

        int forms = Forms::get().unlockedCount();
        if (forms > _forms) _award(S_FORM, 40 * (forms - _forms));
        _forms = forms;

        auto& k = Kotodama::get();
        uint32_t sent = k.sentCount(), crew = k.crewNewCount();
        int friends = k.friendCount();
        if (sent > _sent) _award(S_KOTO, 2 * (sent - _sent));
        if (crew > _crew) _award(S_CREW, 30 * (crew - _crew));
        if (_friends >= 0 && friends > _friends) _award(S_FRIEND, 20 * (friends - _friends));
        _sent = sent; _crew = crew; _friends = friends;

        // Scry restarts its count each drive.
        uint32_t scry = ScryLogger::get().wifiCount();
        if (scry < _scry) _scry = 0;
        _scryRest += scry - _scry;
        _scry = scry;
        if (_scryRest >= 10) { _award(S_SCRY, _scryRest / 10); _scryRest %= 10; }
    }

    static uint32_t _dayKey(time_t t) {
        struct tm lt; localtime_r(&t, &lt);
        return (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
    }

    // A new calendar day: limits reset, the streak grows or restarts, and
    // the first visit pays. Only with the clock set.
    void _rollDay() {
        time_t now = time(nullptr);
        if (now < 1600000000) return;
        uint32_t d = _dayKey(now);
        if (d == _day) return;
        bool yesterday = _day && _dayKey(now - 86400) == _day;
        _streak = yesterday ? (uint8_t)min(250, _streak + 1) : 1;
        _day = d;
        memset(_today, 0, sizeof(_today));
        _dirty = _urgent = true;
        _award(S_VISIT, 10 + 5 * min<int>(10, _streak - 1));
    }

    void _parseToday(const String& d) {
        int i = 0, from = 0;
        while (i < S_COUNT && from <= (int)d.length()) {
            int c = d.indexOf(',', from);
            if (c < 0) c = d.length();
            if (c > from) _today[i] = (uint16_t)d.substring(from, c).toInt();
            i++; from = c + 1;
        }
    }

    void _save() {
        String d;
        for (int i = 0; i < S_COUNT; i++) { if (i) d += ','; d += _today[i]; }
        Preferences p; p.begin("ki", false);
        p.putInt("total", (int32_t)_total);
        p.putInt("day", (int32_t)_day);
        p.putUChar("streak", _streak);
        p.putString("d", d);
        p.end();
        SdSettings::changed();
        _dirty = _urgent = false;
        _savedMs = millis();
    }
};
