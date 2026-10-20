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

// Kotodama - "spirit of words": LoRa chat with other MitamaOS users (the
// Mitama channel) and the public Meshtastic mesh (LongFast). Needs the
// Cap LoRa-1262. The radio keeps listening after you leave the app.
//
// Keys: type and Enter to send, Tab switch channel, fn+; fn+. scroll,
// fn+R radio on/off, fn+N set your name, fn+G change region, fn+` back.
class KotodamaApp : public App {
public:
    KotodamaApp(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Kotodama"; }
    const char* getIcon() const override { return "K"; }
    const char* getDesc() const override { return "LoRa chat"; }

    void onStart() override {
        OsUI::waitRelease();
        _msgs = new (std::nothrow) Kotodama::Msg[Kotodama::MAX_MSGS];
        _input = ""; _scroll = 0; _naming = false; _note = "";
        auto& k = Kotodama::get();
        _picking = k.region() == Mesh::REG_UNSET;
        // Respect fn+R: only start the radio if it was left on (or never set).
        if (!_picking && k.wantOn() && k.state() != Kotodama::ST_ON) k.start();
        _draw();
    }
    void onStop() override { delete[] _msgs; _msgs = nullptr; }

    void update() override {
        M5Cardputer.update();
        auto& kb = M5Cardputer.Keyboard;
        auto& k = Kotodama::get();
        if (kb.isChange() && kb.isPressed()) {
            auto st = kb.keysState();
            if (!(_picking ? _pickKeys(st) : _handleKeys(st))) return;   // left the app
            _draw();
        }
        if (k.serial() != _seenSerial || millis() - _lastDraw > 1000) _draw();
    }

private:
    LGFX_Sprite& _canvas;
    Kotodama::Msg* _msgs = nullptr;
    String   _input;
    uint8_t  _ch = Mesh::CH_MITAMA;
    int      _scroll = 0;                 // lines scrolled up from the newest
    bool     _picking = false, _naming = false;
    uint32_t _seenSerial = 0, _lastDraw = 0;
    bool     _redraw = false;                 // scroll was clamped; draw again
    String   _note;                       // one-line status under the input

    bool _pickKeys(const Keyboard_Class::KeysState& st) {
        for (char c : st.word) {
            if (c == '1' || c == '2') {
                auto& k = Kotodama::get();
                bool first = k.region() == Mesh::REG_UNSET;
                k.setRegion(c == '1' ? Mesh::REG_US : Mesh::REG_EU868);
                _picking = false;
                if (first) k.setOn(true);    // first setup turns it on; later changes keep fn+R's choice
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
        const int top = HEADER_H + 15, bottom = SCREEN_H - FOOTER_H - 15, lh = 10, w = SCREEN_W - 8;
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
            int lineY = bottom - lh, skip = _scroll, drawn = 0, m = n - 1;
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
                int total = _scroll - skip + drawn, visible = (bottom - top) / lh + 1;
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
        const char* foot = _note.length() ? _note.c_str() : "ENT send  TAB channel  fn+R radio  fn+N name";
        MUI::footer(_canvas, foot, _note.length() ? MUI::YELLOW : MUI::DIM);
    }

    void _draw() {
        _lastDraw = millis();
        _seenSerial = Kotodama::get().serial();
        _canvas.fillSprite(MUI::BG);
        MUI::header(_canvas, "KOTODAMA", MUI::YELLOW);
        if (_picking) _drawPicker(); else _drawChat();
        if (_redraw) { _redraw = false; _draw(); return; }
        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }
};
