#pragma once
#include <M5Cardputer.h>
#include <time.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/disp_lock.h"
#include "../../config.h"
#include "../../ui/mitama_ui.h"

// Omikuji - draw the day's shrine fortune, or cast an I Ching hexagram,
// and let the Mitama read it for you.
//  - Omikuji: one slip per day (seeded by the date, so redrawing gives the
//    same answer; with no clock set every draw is fresh). A bad fortune can
//    be tied to the shrine tree to leave the bad luck behind, as at a real
//    shrine.
//  - I Ching: the three-coin method, six lines from the bottom up; 6 and 9
//    are changing lines, which point to a second hexagram.
//
// Keys: Enter draw / cast, Tab switch, T tie a bad fortune, ` back.
class OmikujiApp : public App {
public:
    OmikujiApp(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Omikuji"; }
    const char* getIcon() const override { return "O"; }
    const char* getDesc() const override { return "Fortunes and the I Ching"; }

    void onStart() override { OsUI::waitRelease(); _draw(); }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();
        auto& kb = M5Cardputer.Keyboard;
        if (!(kb.isChange() && kb.isPressed())) return;
        auto st = kb.keysState();
        for (char c : st.word) {
            if (c == '`') { OsUI::waitRelease(); AppManager::get().goHome(); return; }
            if ((c == 't' || c == 'T') && _mode == M_SLIP && _drawn && _rank >= R_KYO) _tied = true;
        }
        if (st.tab) _mode = _mode == M_SLIP ? M_ICHING : M_SLIP;
        if (st.enter) { if (_mode == M_SLIP) _drawSlip(); else _cast(); }
        _draw();
    }

private:
    enum Mode : uint8_t { M_SLIP, M_ICHING };
    enum Rank : uint8_t { R_DAIKICHI, R_KICHI, R_CHUKICHI, R_SHOKICHI, R_HANKICHI, R_SUEKICHI, R_KYO, R_DAIKYO, R_N };

    LGFX_Sprite& _canvas;
    Mode    _mode = M_SLIP;
    bool    _drawn = false, _tied = false, _cast_ = false, _seeded = false;
    uint8_t _rank = 0, _reading = 0, _asp[3] = {0, 0, 0};
    uint8_t _lines[6] = {0};              // coin totals 6..9, bottom first
    uint32_t _slipDay = 0;                // date the current slip was drawn for

    struct RankInfo { const char* name; const char* eng; uint8_t weight; uint16_t col; };
    static const RankInfo& _info(uint8_t r) {
        static const RankInfo R[R_N] = {
            { "Dai-kichi",  "Great blessing",  16, MUI::YELLOW },
            { "Kichi",      "Blessing",        20, MUI::GREEN },
            { "Chu-kichi",  "Middle blessing", 15, MUI::GREEN },
            { "Sho-kichi",  "Small blessing",  15, MUI::CYAN },
            { "Han-kichi",  "Half blessing",    8, MUI::CYAN },
            { "Sue-kichi",  "Future blessing", 14, MUI::LABEL },
            { "Kyo",        "Curse",            9, MUI::BONES },
            { "Dai-kyo",    "Great curse",      3, MUI::RED },
        };
        return R[r < R_N ? r : 0];
    }

    // The Mitama's reading: two per rank.
    static const char* _readingText(uint8_t r, uint8_t k) {
        static const char* const T[R_N][2] = {
            { "The best slip in the box. Don't waste it on errands.",
              "Great blessing. I'd say I arranged it, but you'd believe me." },
            { "Blessing. Solid, dependable, mildly boring. Take it.",
              "A plain good fortune. The gods are nodding, not cheering." },
            { "Middle blessing. Good things, if you meet them halfway.",
              "Fortune's on your side. Mostly. Try not to test it." },
            { "Small blessing. Small things go right. Notice them.",
              "A little luck. Spend it on something that matters." },
            { "Half a blessing. The other half is up to you.",
              "Half luck. Better than none, worse than pretending." },
            { "Future blessing. Not now. Later. Be patient, mortal.",
              "Luck is on its way. It's just taking the scenic route." },
            { "A curse. Tie it to the tree and walk away. (T)",
              "Bad slip. It happens. That's what the tree is for. (T)" },
            { "Great curse. Rare, honestly impressive. Tie it. Now. (T)",
              "Dai-kyo. Even I'm a little concerned. Tie it to the tree. (T)" },
        };
        return T[r < R_N ? r : 0][k & 1];
    }

    // Aspect lines by tone: good, mixed, bad.
    static const char* _aspect(uint8_t which, uint8_t tone, uint8_t k) {
        static const char* const A[3][3][3] = {
            {   // Wish
                { "Wish: it comes true.", "Wish: granted, with interest.", "Wish: closer than you think." },
                { "Wish: half of it, soon.", "Wish: needs one more push.", "Wish: wait for spring." },
                { "Wish: not this time.", "Wish: ask for something else.", "Wish: blocked. Try again later." },
            },
            {   // Lost things
                { "Lost things: found nearby.", "Lost things: under something.", "Lost things: someone has it." },
                { "Lost things: turn up late.", "Lost things: look up high.", "Lost things: ask around." },
                { "Lost things: let it go.", "Lost things: gone with the wind.", "Lost things: hard to find." },
            },
            {   // Journey
                { "Journey: go, all is well.", "Journey: a good road.", "Journey: travel east." },
                { "Journey: go, but slowly.", "Journey: pack a spare.", "Journey: short trips only." },
                { "Journey: stay home today.", "Journey: delays ahead.", "Journey: postpone it." },
            },
        };
        return A[which % 3][tone % 3][k % 3];
    }
    static uint8_t _tone(uint8_t r) { return r <= R_SHOKICHI ? 0 : r <= R_SUEKICHI ? 1 : 2; }

    static uint32_t _rng(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

    void _drawSlip() {
        uint32_t seed;
        struct tm t;
        _seeded = MUI::localNow(t);
        uint32_t day = 0;
        if (_seeded) {
            uint64_t mac = ESP.getEfuseMac();
            day = (uint32_t)((t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday);
            seed = day * 2654435761u ^ (uint32_t)mac ^ (uint32_t)(mac >> 32);
            if (!seed) seed = 1;
        } else seed = esp_random() | 1;
        for (int i = 0; i < 4; i++) _rng(seed);
        int total = 0;
        for (int r = 0; r < R_N; r++) total += _info(r).weight;
        int pick = _rng(seed) % total;
        _rank = 0;
        while (pick >= _info(_rank).weight) { pick -= _info(_rank).weight; _rank++; }
        _reading = _rng(seed) & 1;
        for (auto& a : _asp) a = _rng(seed) % 3;
        if (!_drawn || !_seeded || day != _slipDay) _tied = false;   // a new slip starts untied
        _slipDay = day;
        _drawn = true;
    }

    void _cast() {
        for (auto& l : _lines) {
            l = 0;
            for (int c = 0; c < 3; c++) l += (esp_random() & 1) ? 3 : 2;   // heads 3, tails 2
        }
        _cast_ = true;
    }

    // King Wen number for [upper][lower] trigram (bit 0 = bottom line, yang = 1).
    static uint8_t _kingWen(uint8_t upper, uint8_t lower) {
        static const uint8_t KW[8][8] = {
            {  2, 24,  7, 19, 15, 36, 46, 11 }, { 16, 51, 40, 54, 62, 55, 32, 34 },
            {  8,  3, 29, 60, 39, 63, 48,  5 }, { 45, 17, 47, 58, 31, 49, 28, 43 },
            { 23, 27,  4, 41, 52, 22, 18, 26 }, { 35, 21, 64, 38, 56, 30, 50, 14 },
            { 20, 42, 59, 61, 53, 37, 57,  9 }, { 12, 25,  6, 10, 33, 13, 44,  1 },
        };
        return KW[upper & 7][lower & 7];
    }
    static uint8_t _hexOf(const uint8_t* yang) {
        uint8_t lo = yang[0] | yang[1] << 1 | yang[2] << 2, up = yang[3] | yang[4] << 1 | yang[5] << 2;
        return _kingWen(up, lo);
    }

    static const char* _hexName(uint8_t n) {
        static const char* const N[64] = {
            "The Creative", "The Receptive", "Difficult Beginnings", "Youthful Folly", "Waiting", "Conflict",
            "The Army", "Holding Together", "Small Taming", "Treading", "Peace", "Standstill",
            "Fellowship", "Great Possession", "Modesty", "Enthusiasm", "Following", "Work on the Decayed",
            "Approach", "Contemplation", "Biting Through", "Grace", "Splitting Apart", "Return",
            "Innocence", "Great Taming", "Nourishment", "Great Exceeding", "The Abysmal", "The Clinging",
            "Influence", "Duration", "Retreat", "Great Power", "Progress", "Darkening of the Light",
            "The Family", "Opposition", "Obstruction", "Deliverance", "Decrease", "Increase",
            "Breakthrough", "Coming to Meet", "Gathering Together", "Pushing Upward", "Oppression", "The Well",
            "Revolution", "The Cauldron", "The Arousing", "Keeping Still", "Development", "The Marrying Maiden",
            "Abundance", "The Wanderer", "The Gentle", "The Joyous", "Dispersion", "Limitation",
            "Inner Truth", "Small Exceeding", "After Completion", "Before Completion",
        };
        return n >= 1 && n <= 64 ? N[n - 1] : "?";
    }

    // The Mitama's gloss on each hexagram.
    static const char* _hexGloss(uint8_t n) {
        static const char* const G[64] = {
            "Pure force. Act, but don't burn out.", "Yield and support. Strength that bends.",
            "A messy start. Push through the tangle.", "You don't know yet. Ask, and listen.",
            "Not yet. Eat, rest, keep watch.", "A quarrel. Winning it costs more than it pays.",
            "Discipline and a good leader carry the day.", "Stick with your people. Don't arrive late.",
            "Small restraints. Little by little.", "Step carefully on the tiger's tail.",
            "Heaven and earth in balance. Enjoy it.", "Things are stuck. Keep your head down.",
            "Find your allies out in the open.", "Plenty in hand. Share it wisely.",
            "Stay humble. It's annoyingly effective.", "Momentum. Music helps, apparently.",
            "Follow, for now. Choose who carefully.", "Fix what rotted. Blame no one, just work.",
            "Good things approach. Don't get complacent.", "Watch closely before you act.",
            "Bite through the obstacle. Be fair about it.", "Pretty surfaces. Look at what's beneath.",
            "Things fall apart. Don't fight the collapse.", "The light comes back. Begin again.",
            "Act without scheming. It works out.", "Hold your power in reserve.",
            "Mind what you feed yourself, and others.", "The beam sags. Something has to give.",
            "Danger on danger. Keep flowing, like water.", "Cling to what's bright. It needs fuel.",
            "Attraction. Stay open, stay steady.", "Endure. Keep the course.",
            "Retreat isn't defeat. Step back.", "Great power. Don't swing it carelessly.",
            "Rising like the sun. Progress comes easily.", "Hide your light a while. Endure.",
            "Tend the household. Roles matter.", "At odds. Agree on small things first.",
            "Blocked. Go around, or get help.", "The knot loosens. Forgive and move on.",
            "Lose a little now. It comes back.", "Gain. Use it to lift others.",
            "Breakthrough. Say the true thing out loud.", "A tempting encounter. Be wary.",
            "Gather together, with a purpose.", "Push upward steadily, one step at a time.",
            "Worn out. Words won't help; patience will.", "The well is there. Keep the rope intact.",
            "Change is due. Time it right.", "The cauldron: nourish what matters.",
            "Thunder! A shock, then laughter.", "Be still, like a mountain.",
            "Slow growth, like a tree on a hill.", "You're not in charge here. Mind your place.",
            "Abundance peaks. Enjoy noon; dusk follows.", "The wanderer. Be polite, travel light.",
            "Gentle wind gets everywhere. Persist.", "Joy shared is joy doubled.",
            "Dissolve what's rigid. Scatter the fog.", "Set limits. Not too tight.",
            "Inner truth. Even pigs and fish feel it.", "Small matters only. Stay low.",
            "Done. Now watch for things unravelling.", "Almost there. Don't get your tail wet.",
        };
        return n >= 1 && n <= 64 ? G[n - 1] : "";
    }

    int _wrap(const char* s, int x, int y, int w, int maxLines, uint16_t col) {
        char line[80];
        int lines = 0;
        while (*s && lines < maxLines) {
            int n = 0, lastSpace = -1;
            while (s[n] && n < (int)sizeof(line) - 1) {
                line[n] = s[n]; line[n + 1] = 0;
                if (MUI::width(_canvas, MUI::small(), line) > w) break;
                if (s[n] == ' ') lastSpace = n;
                n++;
            }
            if (s[n] && lastSpace > 0) n = lastSpace;
            if (n == 0) n = 1;
            memcpy(line, s, n); line[n] = 0;
            MUI::text(_canvas, MUI::small(), line, x, y + lines * 10, col);
            s += n;
            while (*s == ' ') s++;
            lines++;
        }
        return lines;
    }

    void _drawSlipView() {
        // the paper slip
        const int px = 10, py = HEADER_H + 5, pw = 40, ph = 96;
        _canvas.fillRect(px, py, pw, ph, MUI::CREAM);
        _canvas.drawRect(px - 1, py - 1, pw + 2, ph + 2, MUI::BONES_DIM);
        if (_drawn) {
            uint16_t ink = _rank >= R_KYO ? MUI::BONES : MUI::BG;
            for (int i = 0; i < 7; i++) _canvas.drawFastHLine(px + 8, py + 34 + i * 8, pw - 16, MUI::DIM);
            _canvas.fillCircle(px + pw / 2, py + 17, 9, MUI::BONES);           // the shrine's seal
            _canvas.fillCircle(px + pw / 2, py + 17, 5, MUI::CREAM);
            _canvas.fillCircle(px + pw / 2, py + 17, 2, ink);
            if (_tied) {   // folded and knotted on the tree
                _canvas.fillRect(px - 2, py + ph / 2 - 4, pw + 4, 8, MUI::BONES);
                _canvas.drawLine(px + pw / 2, py + ph / 2 + 4, px + pw / 2 - 6, py + ph / 2 + 16, MUI::BONES);
                _canvas.drawLine(px + pw / 2, py + ph / 2 + 4, px + pw / 2 + 6, py + ph / 2 + 16, MUI::BONES);
            }
        } else {
            MUI::textCentered(_canvas, MUI::title(), "?", px + pw / 2, py + ph / 2 - 7, MUI::BONES);
        }

        const int x = 62, w = SCREEN_W - 4 - x;
        int y = HEADER_H + 3;
        if (!_drawn) {
            MUI::text(_canvas, MUI::title(), "Today's fortune", x, y, MUI::YELLOW, 1);
            _wrap("Shake the box and draw a slip. One per day; the spirits don't do refunds.", x, y + 18, w, 4, MUI::CREAM);
            return;
        }
        const auto& R = _info(_rank);
        int e = MUI::text(_canvas, MUI::title(), R.name, x, y, R.col, 1);
        MUI::text(_canvas, MUI::small(), R.eng, e + 6, y + 3, MUI::DIM);
        y += 18;
        if (_tied) y += 10 * _wrap("Tied to the tree. The bad luck stays here; you walk on.", x, y, w, 3, MUI::CREAM);
        else y += 10 * _wrap(_readingText(_rank, _reading), x, y, w, 3, MUI::CREAM);
        y += 4;
        for (int i = 0; i < 3; i++) { MUI::text(_canvas, MUI::small(), _aspect(i, _tone(_rank), _asp[i]), x, y, MUI::LABEL); y += 11; }
        if (!_seeded) MUI::text(_canvas, MUI::small(), "(no clock: every draw is new)", x, y, MUI::DIM);
    }

    void _drawIChing() {
        const int lx = 10, lw = 40, top = HEADER_H + 10;
        if (!_cast_) {
            for (int i = 0; i < 6; i++) _canvas.drawRect(lx, top + i * 15, lw, 6, MUI::BONES_DIM);
            MUI::text(_canvas, MUI::title(), "Cast the coins", 62, HEADER_H + 3, MUI::YELLOW, 1);
            _wrap("Three coins, six times, bottom line first. Ask your question, then press Enter.",
                  62, HEADER_H + 21, SCREEN_W - 66, 4, MUI::CREAM);
            return;
        }
        uint8_t yang[6], moved[6];
        bool changing = false;
        for (int i = 0; i < 6; i++) {
            yang[i] = _lines[i] == 7 || _lines[i] == 9;
            bool ch = _lines[i] == 6 || _lines[i] == 9;
            moved[i] = ch ? !yang[i] : yang[i];
            changing |= ch;
            int y = top + (5 - i) * 15;                     // line 1 at the bottom
            uint16_t col = ch ? MUI::YELLOW : MUI::CYAN;
            if (yang[i]) _canvas.fillRect(lx, y, lw, 6, col);
            else { _canvas.fillRect(lx, y, lw / 2 - 4, 6, col); _canvas.fillRect(lx + lw / 2 + 4, y, lw / 2 - 4, 6, col); }
            if (ch) _canvas.drawCircle(lx + lw + 7, y + 3, 3, MUI::YELLOW);
        }
        uint8_t n = _hexOf(yang);
        const int x = 62, w = SCREEN_W - 4 - x;
        int y = HEADER_H + 3;
        char buf[40];
        snprintf(buf, sizeof(buf), "%u", n);
        int e = MUI::text(_canvas, MUI::big(), buf, x, y, MUI::YELLOW);
        _canvas.setClipRect(e + 6, y, SCREEN_W - 4 - e - 6, 16);
        MUI::text(_canvas, MUI::title(), _hexName(n), e + 6, y + 1, MUI::WHITE);
        _canvas.clearClipRect();
        y += 19;
        y += 10 * _wrap(_hexGloss(n), x, y, w, 3, MUI::CREAM);
        if (changing) {
            uint8_t m = _hexOf(moved);
            y += 5;
            snprintf(buf, sizeof(buf), "Changing to %u", m);
            MUI::text(_canvas, MUI::small(), buf, x, y, MUI::YELLOW);
            MUI::text(_canvas, MUI::small(), _hexName(m), x, y + 11, MUI::LABEL);
            _wrap(_hexGloss(m), x, y + 22, w, 2, MUI::DIM);
        }
    }

    void _draw() {
        _canvas.fillSprite(MUI::BG);
        MUI::header(_canvas, _mode == M_SLIP ? "OMIKUJI" : "I CHING", MUI::YELLOW);
        if (_mode == M_SLIP) _drawSlipView(); else _drawIChing();
        const char* foot = _mode == M_SLIP
            ? (_drawn && _rank >= R_KYO && !_tied ? "ENT draw  T tie to tree  TAB I Ching" : "ENT draw  TAB I Ching")
            : "ENT cast  TAB omikuji";
        MUI::footer(_canvas, foot, MUI::DIM);
        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }
};
