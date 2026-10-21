#pragma once
#include <M5Cardputer.h>
#include <new>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/disp_lock.h"
#include "../../config.h"
#include "../../ui/mitama_ui.h"
#include "../../net/kotodama.h"
#include "../../mitama/forms.h"

// Kotodama - "spirit of words": LoRa chat with other MitamaOS users (the
// Mitama channel) and the public Meshtastic mesh (LongFast). Needs the
// Cap LoRa-1262. The radio keeps listening after you leave the app.
//
// Keys: type and Enter to send, Tab switch channel, fn+; fn+. scroll,
// fn+R radio on/off, fn+N set your name, fn+G change region, fn+` back.
// fn+C opens Crew: other Mitama heard in range, and your friends
// (; . move, Enter their card - form and Compendium progress -, F add /
// remove a friend, ` back).
class KotodamaApp : public App {
public:
    // crewOnly: the Kizuna tile - this app opened straight on Crew, which is
    // also where ` leaves from.
    KotodamaApp(LGFX_Sprite& canvas, bool crewOnly = false) : _canvas(canvas), _crewOnly(crewOnly) {}

    const char* getName() const override { return _crewOnly ? "Kizuna" : "Kotodama"; }
    const char* getIcon() const override { return _crewOnly ? "Z" : "K"; }
    const char* getDesc() const override { return _crewOnly ? "Your crew and friends" : "LoRa chat"; }

    void onStart() override {
        OsUI::waitRelease();
        _msgs = _crewOnly ? nullptr : new (std::nothrow) Kotodama::Msg[Kotodama::MAX_MSGS];
        _input = ""; _scroll = 0; _naming = false; _note = ""; _closeCrew();
        auto& k = Kotodama::get();
        _picking = k.region() == Mesh::REG_UNSET;
        // Respect fn+R: only start the radio if it was left on (or never set).
        if (!_picking && k.wantOn() && k.state() != Kotodama::ST_ON) k.start();
        if (_crewOnly && !_picking) _openCrew();
        _draw();
    }
    void onStop() override { delete[] _msgs; _msgs = nullptr; _closeCrew(); }

    void update() override {
        M5Cardputer.update();
        auto& kb = M5Cardputer.Keyboard;
        auto& k = Kotodama::get();
        if (kb.isChange() && kb.isPressed()) {
            auto st = kb.keysState();
            if (!(_picking ? _pickKeys(st) : _crew ? _crewKeys(st) : _handleKeys(st))) return;   // left the app
            _draw();
        }
        if (k.serial() != _seenSerial || millis() - _lastDraw > 1000) _draw();
    }

private:
    LGFX_Sprite& _canvas;
    const bool _crewOnly;
    Kotodama::Msg* _msgs = nullptr;
    String   _input;
    uint8_t  _ch = Mesh::CH_MITAMA;
    int      _scroll = 0;                 // lines scrolled up from the newest
    bool     _picking = false, _naming = false;
    uint32_t _seenSerial = 0, _lastDraw = 0;
    bool     _redraw = false;                 // scroll was clamped; draw again
    String   _note;                       // one-line status under the input
    Kotodama::CrewRow* _crew = nullptr;   // the Crew screen's rows, only while it's open
    int      _crewN = 0, _crewSel = 0, _crewTop = 0;
    uint32_t _cardNum = 0;                // whose card is open (0 = the list)
    static constexpr int CREW_MAX = Kotodama::MAX_FRIENDS + Kotodama::MAX_NODES;

    void _openCrew() {
        if (!_crew) _crew = new (std::nothrow) Kotodama::CrewRow[CREW_MAX];
        _crewSel = _crewTop = 0; _note = "";
    }
    void _closeCrew() { delete[] _crew; _crew = nullptr; _crewN = 0; _cardNum = 0; }

    // The open card's row, refreshed with each draw.
    const Kotodama::CrewRow* _cardRow() const {
        for (int i = 0; i < _crewN; i++) if (_crew[i].num == _cardNum) return &_crew[i];
        return nullptr;
    }
    void _toggleFriend(const Kotodama::CrewRow& r) {
        if (!Kotodama::get().setFriend(r.num, !r.isFriend)) _note = "Friend list is full (16).";
        else _note = r.isFriend ? "Removed from friends." : "Added to friends.";
    }

    bool _crewKeys(const Keyboard_Class::KeysState& st) {
        if (_cardNum) {   // a card is open
            for (char c : st.word) {
                if (c == '`') { _cardNum = 0; _note = ""; return true; }
                if ((c == 'f' || c == 'F') && _cardRow()) _toggleFriend(*_cardRow());
            }
            return true;
        }
        for (char c : st.word) {
            if (c == '`' || (st.fn && (c == 'c' || c == 'C'))) {
                _closeCrew(); _note = "";
                if (_crewOnly) { OsUI::waitRelease(); AppManager::get().goHome(); return false; }
                return true;
            }
            if (c == ';') _crewSel = max(0, _crewSel - 1);
            if (c == '.') _crewSel = min(_crewN - 1, _crewSel + 1);
            if ((c == 'f' || c == 'F') && _crewSel < _crewN) _toggleFriend(_crew[_crewSel]);
        }
        if (st.enter && _crewSel < _crewN) { _cardNum = _crew[_crewSel].num; _note = ""; }
        return true;
    }

    void _drawCard() {
        const Kotodama::CrewRow* r = _cardRow();
        if (!r) { _cardNum = 0; _drawCrew(); return; }
        uint8_t form = r->hasCard && r->card.form < MUI::F_COUNT ? r->card.form : MUI::F_SOUL;
        _canvas.setClipRect(0, HEADER_H + 1, 86, SCREEN_H - FOOTER_H - HEADER_H - 1);
        MUI::soulForm(_canvas, 42, 68, 80, r->hasCard ? MUI::YELLOW : MUI::DIM,
                      r->isFriend ? MUI::CYAN : MUI::MAGENTA, 0, (MUI::Form)form);
        _canvas.clearClipRect();

        const int x = 90;
        int y = HEADER_H + 3;
        _canvas.setClipRect(x, y, SCREEN_W - x - 2, 16);
        bool creator = r->hasCard && r->card.form == MUI::F_KYUBI;
        MUI::text(_canvas, MUI::title(), r->name, x, y, creator ? MUI::GREEN : r->isFriend ? MUI::YELLOW : MUI::MAGENTA, 1);
        _canvas.clearClipRect();
        y += 16;
        if (creator) { MUI::text(_canvas, MUI::small(), "Creator of MitamaOS", x, y, MUI::MAGENTA); y += 11; }
        char buf[40], ago[12];
        _ago(ago, sizeof(ago), r->heardMs);
        if (r->heardMs) snprintf(buf, sizeof(buf), "%s  %d dBm  %s", r->isFriend ? "Friend" : "Crew", r->rssi, ago);
        else snprintf(buf, sizeof(buf), "Friend  %s", ago);
        MUI::text(_canvas, MUI::small(), buf, x, y, MUI::DIM);
        y += 15;
        if (!r->hasCard) {
            MUI::text(_canvas, MUI::small(), "No card yet. It comes", x, y, MUI::DIM);
            MUI::text(_canvas, MUI::small(), "with their next check-in.", x, y + 11, MUI::DIM);
        } else {
            const Kotodama::Card& c = r->card;
            auto row = [&](const char* k, const char* v, uint16_t col) {
                MUI::text(_canvas, MUI::small(), k, x, y, MUI::DIM);
                MUI::text(_canvas, MUI::small(), v, x + 44, y, col);
                y += 11;
            };
            row("Form", Forms::name(form), MUI::YELLOW);
            snprintf(buf, sizeof(buf), "%u / %u", c.forms, (unsigned)MUI::F_PUBLIC);
            row("Forms", buf, MUI::WHITE);
            snprintf(buf, sizeof(buf), "%u / %u", c.species, c.speciesTotal);
            row("Pages", buf, MUI::WHITE);
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)c.encounters);
            row("Met", buf, MUI::WHITE);
            buf[0] = 0;
            for (uint8_t rc = 0; rc < Sig::R_COUNT; rc++)
                if (c.races >> rc & 1) { if (buf[0]) strncat(buf, " ", sizeof(buf) - strlen(buf) - 1); strncat(buf, Sig::raceName(rc), sizeof(buf) - strlen(buf) - 1); }
            _canvas.setClipRect(x, y, SCREEN_W - x - 2, 12);
            row("Races", buf[0] ? buf : "none yet", buf[0] ? MUI::GREEN : MUI::DIM);
            _canvas.clearClipRect();
        }
        const char* foot = _note.length() ? _note.c_str() : (r->isFriend ? "F unfriend  ` back" : "F add friend  ` back");
        MUI::footer(_canvas, foot, _note.length() ? MUI::YELLOW : MUI::DIM);
    }

    static void _ago(char* out, size_t n, uint32_t heardMs) {
        if (!heardMs) { snprintf(out, n, "not heard"); return; }
        uint32_t s = (millis() - heardMs) / 1000;
        if (s < 60)        snprintf(out, n, "now");
        else if (s < 3600) snprintf(out, n, "%um", (unsigned)(s / 60));
        else               snprintf(out, n, "%uh", (unsigned)(s / 3600));
    }

    void _drawCrew() {
        auto& k = Kotodama::get();
        if (_crewSel >= _crewN) _crewSel = max(0, _crewN - 1);
        int y = HEADER_H + 2;
        MUI::text(_canvas, MUI::small(), "CREW", 4, y, MUI::YELLOW, 1);
        MUI::text(_canvas, MUI::small(), "Mitama in LoRa range, and friends", 40, y, MUI::DIM);
        const int top = HEADER_H + 16, lh = 12;
        const int vis = (SCREEN_H - FOOTER_H - top) / lh;
        const char* stateMsg = _stateText(k.state());
        if (!_crewN) {
            MUI::text(_canvas, MUI::small(), stateMsg ? stateMsg : "No other Mitama heard yet.", 4, top + 4, stateMsg ? MUI::YELLOW : MUI::DIM);
            if (!stateMsg) MUI::text(_canvas, MUI::small(), "They show up here when they're in range.", 4, top + 17, MUI::DIM);
        }
        if (_crewSel < _crewTop) _crewTop = _crewSel;
        if (_crewSel >= _crewTop + vis) _crewTop = _crewSel - vis + 1;
        for (int i = 0; i < vis && _crewTop + i < _crewN; i++) {
            auto& r = _crew[_crewTop + i];
            int ry = top + i * lh;
            bool sel = _crewTop + i == _crewSel;
            if (sel) {
                _canvas.fillRect(1, ry - 1, SCREEN_W - 2, lh, MUI::PANEL);
                _canvas.drawRect(1, ry - 1, SCREEN_W - 2, lh, MUI::MAGENTA);
            }
            bool creator = r.hasCard && r.card.form == MUI::F_KYUBI;
            if (creator) MitamaLogo::_magatama(_canvas, 8, ry + 6, 2.4f, 1.8f, -60, 185, 0.82f, MUI::GREEN);
            else if (r.isFriend) MUI::text(_canvas, MUI::small(), "*", 5, ry, MUI::YELLOW);
            char right[24], ago[12];
            _ago(ago, sizeof(ago), r.heardMs);
            if (r.heardMs) snprintf(right, sizeof(right), "%d dBm  %s", r.rssi, ago);
            else snprintf(right, sizeof(right), "%s", ago);
            int rw = MUI::width(_canvas, MUI::small(), right);
            _canvas.setClipRect(14, ry, SCREEN_W - 22 - rw, lh);
            MUI::text(_canvas, MUI::small(), r.name, 14, ry,
                      creator ? MUI::MAGENTA : r.heardMs ? (sel ? MUI::WHITE : MUI::LABEL) : MUI::DIM);
            _canvas.clearClipRect();
            MUI::text(_canvas, MUI::small(), right, SCREEN_W - 5 - rw, ry, r.heardMs ? MUI::CYAN : MUI::DIM);
        }
        const char* foot = _note.length() ? _note.c_str() : (_crewN ? (_crewOnly ? "ENT card  F friend  ` back" : "ENT card  F friend  ` chat")
                                   : (_crewOnly ? "` back" : "` back to the chat"));
        MUI::footer(_canvas, foot, _note.length() ? MUI::YELLOW : MUI::DIM);
    }

    bool _pickKeys(const Keyboard_Class::KeysState& st) {
        for (char c : st.word) {
            if (c == '1' || c == '2') {
                auto& k = Kotodama::get();
                bool first = k.region() == Mesh::REG_UNSET;
                k.setRegion(c == '1' ? Mesh::REG_US : Mesh::REG_EU868);
                _picking = false;
                if (first) k.setOn(true);    // first setup turns it on; later changes keep fn+R's choice
                if (_crewOnly) _openCrew();
            }
            if (c == '`') { OsUI::waitRelease(); AppManager::get().goHome(); return false; }
        }
        return true;
    }

    // Returns false when the app was closed.
    bool _handleKeys(const Keyboard_Class::KeysState& st) {
        auto& k = Kotodama::get();
        if (st.fn) {
            for (char c : st.word) {
                if (c == '`') { OsUI::waitRelease(); AppManager::get().goHome(); return false; }
                if (c == ';') _scroll++;
                if (c == '.') _scroll = max(0, _scroll - 1);
                if (c == 'r' || c == 'R') { k.setOn(!k.wantOn()); _note = ""; }
                if (c == 'n' || c == 'N') { _naming = true; _input = k.longName(); }
                if (c == 'g' || c == 'G') _picking = true;
                if (c == 'c' || c == 'C') _openCrew();
            }
            return true;
        }
        if (st.tab) { _ch = (_ch + 1) % Mesh::CH_COUNT; _scroll = 0; return true; }
        if (st.del) { if (_input.length()) _input.remove(_input.length() - 1); return true; }
        if (st.enter) { _submit(); return true; }
        for (char c : st.word) {
            if (c == '`' && !_input.length() && !_naming) { OsUI::waitRelease(); AppManager::get().goHome(); return false; }
            size_t cap = _naming ? 24 : Mesh::MAX_TEXT;
            if (_input.length() < cap) _input += c;
        }
        return true;
    }

    void _submit() {
        auto& k = Kotodama::get();
        _input.trim();
        if (_naming) {
            if (_input.length()) {
                k.setName(_input.c_str());
                _note = "Name saved. The mesh hears it next time we announce.";
            } else _note = "";
            _naming = false; _input = "";
            return;
        }
        if (!_input.length()) return;
        if (k.state() != Kotodama::ST_ON) { _note = "Radio is off (fn+R)."; return; }
        if (k.send(_ch, _input.c_str())) { _input = ""; _scroll = 0; _note = "Sending..."; }
        else _note = "Still sending the last one.";
    }

    static const char* _stateText(Kotodama::State s) {
        switch (s) {
            case Kotodama::ST_ON:         return nullptr;
            case Kotodama::ST_NO_CAP:     return "LoRa cap not found. Fit the Cap LoRa-1262.";
            case Kotodama::ST_NO_REGION:  return "Pick a region first (fn+G).";
            case Kotodama::ST_RADIO_FAIL: return "The radio didn't start.";
            default:                      return "Radio off. fn+R turns it on.";
        }
    }

    // Splits s into lines that fit width w. Returns how many.
    int _wrap(const char* s, int w, int* starts, int* lens, int max) {
        int n = 0, i = 0, L = strlen(s);
        char line[80];
        while (i < L && n < max) {
            int len = 0, lastSpace = -1;
            while (i + len < L && len < (int)sizeof(line) - 1) {
                line[len] = s[i + len]; line[len + 1] = 0;
                if (MUI::width(_canvas, MUI::small(), line) > w) break;
                if (s[i + len] == ' ') lastSpace = len;
                len++;
            }
            if (i + len < L && lastSpace > 0) len = lastSpace + 1;
            if (len == 0) len = 1;
            starts[n] = i; lens[n] = len; n++;
            i += len;
        }
        return n;
    }

    void _drawPicker() {
        int y = HEADER_H + 6;
        MUI::text(_canvas, MUI::title(), "Where are you?", 8, y, MUI::YELLOW, 1);
        y += 18;
        MUI::text(_canvas, MUI::small(), "LoRa must use your region's band.", 8, y, MUI::DIM);
        y += 16;
        MUI::text(_canvas, MUI::small(), "1  Americas  (US 915 MHz)", 14, y, MUI::WHITE);
        y += 13;
        MUI::text(_canvas, MUI::small(), "2  Europe  (EU 868 MHz)", 14, y, MUI::WHITE);
        MUI::footer(_canvas, "1 / 2 choose    ` back", MUI::DIM);
    }

    void _drawChat() {
        auto& k = Kotodama::get();
        // channel tabs
        int x = 4, y = HEADER_H + 1;
        for (uint8_t c = 0; c < Mesh::CH_COUNT; c++) {
            char buf[24];
            int u = c == _ch ? 0 : k.unread(c);
            snprintf(buf, sizeof(buf), u ? "%s %d" : "%s", Mesh::channel(c).name, u);
            int w = MUI::width(_canvas, MUI::small(), buf) + 8;
            if (c == _ch) _canvas.fillRect(x, y, w, 12, MUI::PANEL);
            _canvas.drawRect(x, y, w, 12, c == _ch ? MUI::YELLOW : MUI::BONES_DIM);
            MUI::text(_canvas, MUI::small(), buf, x + 4, y, c == _ch ? MUI::YELLOW : (u ? MUI::CYAN : MUI::DIM));
            x += w + 3;
        }
        k.markRead(_ch);
        char right[24];
        if (k.state() == Kotodama::ST_ON)
            snprintf(right, sizeof(right), "%.3f", Mesh::frequency(k.region()));
        else strcpy(right, "OFF");
        int rw = MUI::width(_canvas, MUI::small(), right);
        MUI::text(_canvas, MUI::small(), right, SCREEN_W - 4 - rw, y, k.state() == Kotodama::ST_ON ? MUI::GREEN : MUI::DIM);

        // messages, newest at the bottom
        // Rows are a full font height apart, and the newest sits fully above
        // the input line (lh 10 overlapped rows and clipped the last one).
        const int lh = 12, w = SCREEN_W - 8;
        const int top = HEADER_H + 15;
        const int newest = SCREEN_H - FOOTER_H - 15 - 14;     // input separator, minus one row of glyphs
        const char* stateMsg = _stateText(k.state());
        int n = _msgs ? k.messages(_msgs, Kotodama::MAX_MSGS, _ch) : 0;
        if (stateMsg && !n) {
            MUI::text(_canvas, MUI::small(), stateMsg, 4, top + 4, MUI::YELLOW);
        } else if (!n) {
            MUI::text(_canvas, MUI::small(), _ch == Mesh::CH_MITAMA ? "No words yet. Say something to other Mitama."
                                                                  : "Quiet on LongFast. Say hello to the mesh.", 4, top + 4, MUI::DIM);
        } else {
            static char full[Mesh::MAX_TEXT + 32];
            int starts[24], lens[24];
            int lineY = newest, skip = _scroll, drawn = 0, m = n - 1;
            for (; m >= 0 && lineY >= top; m--) {
                char who[28];
                k.nameOf(_msgs[m].from, who, sizeof(who));
                snprintf(full, sizeof(full), "%s: %s", who, _msgs[m].text);
                int nameLen = strlen(who) + 1;
                int nl = _wrap(full, w, starts, lens, 24);
                for (int l = nl - 1; l >= 0 && lineY >= top; l--) {
                    if (skip > 0) { skip--; continue; }
                    char seg[80];
                    int s0 = starts[l], len = min(lens[l], (int)sizeof(seg) - 1);
                    int xx = 4;
                    if (s0 < nameLen) {     // the speaker's name, coloured
                        int nlen = min(len, nameLen - s0);
                        memcpy(seg, full + s0, nlen); seg[nlen] = 0;
                        xx = MUI::text(_canvas, MUI::small(), seg, xx, lineY,
                                       _msgs[m].mine ? MUI::GREEN : (_ch == Mesh::CH_MITAMA ? MUI::MAGENTA : MUI::CYAN));
                        s0 += nlen; len -= nlen;
                    }
                    memcpy(seg, full + s0, len); seg[len] = 0;
                    MUI::text(_canvas, MUI::small(), seg, xx, lineY, MUI::WHITE);
                    lineY -= lh; drawn++;
                }
            }
            // Scrolled past the oldest line: pull back so the screen stays full.
            if (m < 0 && lineY >= top && _scroll > 0) {
                int total = _scroll - skip + drawn, visible = (newest - top) / lh + 1;
                int best = max(0, total - visible);
                if (best < _scroll) { _scroll = best; _redraw = true; }
            }
            if (_scroll > 0) MUI::text(_canvas, MUI::small(), "^", SCREEN_W - 9, top, MUI::DIM);
        }

        // input line
        int iy = SCREEN_H - FOOTER_H - 14;
        _canvas.drawFastHLine(0, iy - 1, SCREEN_W, MUI::BONES_DIM);
        String shown = (_naming ? "name> " : "> ") + _input + ((millis() / 500) % 2 ? "_" : " ");
        int sw = MUI::width(_canvas, MUI::small(), shown.c_str());
        int sx = sw > SCREEN_W - 8 ? SCREEN_W - 4 - sw : 4;   // keep the end in view
        _canvas.setClipRect(0, iy, SCREEN_W, 13);
        MUI::text(_canvas, MUI::small(), shown.c_str(), sx, iy, _naming ? MUI::YELLOW : MUI::WHITE);
        _canvas.clearClipRect();

        if (k.takeSendFailed()) _note = "That one didn't send. Try again.";
        else if (_note == "Sending..." && !k.sending()) _note = "";
        const char* foot = _note.length() ? _note.c_str() : "ENT send  TAB channel  fn+C crew  fn+R radio";
        MUI::footer(_canvas, foot, _note.length() ? MUI::YELLOW : MUI::DIM);
    }

    void _draw() {
        _lastDraw = millis();
        _seenSerial = Kotodama::get().serial();
        _canvas.fillSprite(MUI::BG);
        MUI::header(_canvas, _crewOnly ? "KIZUNA" : "KOTODAMA", MUI::YELLOW);
        if (_picking) _drawPicker();
        else if (_crew) { _crewN = Kotodama::get().crew(_crew, CREW_MAX); if (_cardNum) _drawCard(); else _drawCrew(); }
        else _drawChat();
        if (_redraw) { _redraw = false; _draw(); return; }
        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }
};
