#pragma once
#include <M5Cardputer.h>
#include <time.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/disp_lock.h"
#include "../../config.h"
#include "../../ui/mitama_ui.h"
#include "../../ui/species_sigils.h"
#include "../../detect/compendium.h"

// Compendium - the bestiary of every demon (device) the Mitama has met
// (outline §5a, §15). Split view: a race-grouped list on the left, the
// highlighted entry's card on the right. Unmet species show as "???".
// Fiend entries are drawn wrong on purpose: a jittering sigil with the red
// bones showing through and a name that won't hold still.
//
// Keys: ; . move, , / jump a race, ` back.
class CompendiumApp : public App {
public:
    CompendiumApp(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Compendium"; }
    const char* getIcon() const override { return "C"; }
    const char* getDesc() const override { return "Every demon you've met"; }

    void onStart() override {
        OsUI::waitRelease();
        _buildRows();
        _scrollTo();
        _draw();
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();
        auto& kb = M5Cardputer.Keyboard;
        if (kb.isKeyPressed('`')) { OsUI::waitRelease(); AppManager::get().goHome(); return; }
        bool moved = false;
        if (kb.isKeyPressed(';')) { _step(-1); moved = true; }
        if (kb.isKeyPressed('.')) { _step(+1); moved = true; }
        if (kb.isKeyPressed(',')) { _race(-1); moved = true; }
        if (kb.isKeyPressed('/')) { _race(+1); moved = true; }
        uint32_t now = millis();
        // Redraw on a move, once a second for new meetings and the clock,
        // and fast while a Fiend's card is glitching.
        bool glitch = Sig::raceOf(_sel) == Sig::R_FIEND && Compendium::get().entry(_sel).met;
        if (moved || now - _lastDraw > 1000 || (glitch && now - _lastDraw > 90)) _draw();
        if (moved) delay(KEY_REPEAT_MS);
    }

private:
    LGFX_Sprite& _canvas;

    // List rows: race headers (type = 0x80 | race) and species.
    static constexpr int MAX_ROWS = Sig::T_COUNT + Sig::R_COUNT;
    static constexpr int LIST_W = 96, ROW_H = 12, LIST_Y = HEADER_H + 2;
    static constexpr int VIS = (SCREEN_H - FOOTER_H - LIST_Y) / ROW_H;
    uint8_t  _rows[MAX_ROWS];
    int      _nRows = 0, _scroll = 0;
    uint8_t  _sel = 0;                   // selected species
    uint32_t _lastDraw = 0;

    static uint16_t _raceCol(uint8_t r) {
        switch (r) {
            case Sig::R_VILE:  return MUI::MAGENTA;
            case Sig::R_NIGHT: return MUI::CYAN;
            case Sig::R_FAIRY: return MUI::GREEN;
            case Sig::R_FIEND: return MUI::RED;
            default:           return MUI::YELLOW;    // the rare singletons
        }
    }

    static const char* _devName(uint8_t t) {
        static const char* const N[Sig::T_COUNT] = {
            "Flock", "Axon", "ALPR", "Camera", "Ring", "Meta glasses",
            "AirTag", "Tile", "SmartTag", "Google tag", "Drone", "iBeacon",
            "Skimmer", "Raven", "Deauth", "Evil twin", "Hacker",
        };
        return t < Sig::T_COUNT ? N[t] : "?";
    }

    // What the device is, in plain words, with the Mitama's edge.
    static const char* _lore(uint8_t t) {
        static const char* const L[Sig::T_COUNT] = {
            "Flock Safety plate camera. Photographs every car that passes, forever.",
            "Police body camera and taser maker. If it's here, so is an officer.",
            "License plate reader. Your car's diary, kept by strangers.",
            "A Wi-Fi security camera. Someone wanted to watch this spot.",
            "Ring doorbell. Amazon's porch lantern, and it talks to police.",
            "Meta smart glasses. The wearer might be filming you right now.",
            "Apple AirTag. If it keeps following you, it isn't lost.",
            "Tile tracker. Keys, wallets, or someone's plans for you.",
            "Samsung SmartTag. Fox-fire only Galaxy phones can see.",
            "Google Find My Device tag. Android's demon-fire.",
            "A drone broadcasting Remote ID. Something is watching from above.",
            "iBeacon. Shops use them to know which aisle you're in.",
            "Bluetooth module common in card skimmers. Check the card slot.",
            "Gunshot detector. It listens to the whole street, all the time.",
            "Someone is knocking devices off Wi-Fi. That's an attack.",
            "A fake copy of a real network. Don't log in to it.",
            "A known attack tool nearby. Not wildlife. Stay sharp.",
        };
        return t < Sig::T_COUNT ? L[t] : "";
    }

    void _buildRows() {
        _nRows = 0;
        for (uint8_t r = 0; r < Sig::R_COUNT; r++) {
            if (!Compendium::raceSize(r)) continue;     // Herald: iBeacons aren't listed
            _rows[_nRows++] = 0x80 | r;
            for (uint8_t t = 0; t < Sig::T_COUNT; t++)
                if (Compendium::listed(t) && Sig::raceOf(t) == r) _rows[_nRows++] = t;
        }
    }

    int _rowOf(uint8_t t) const {
        for (int i = 0; i < _nRows; i++) if (_rows[i] == t) return i;
        return 0;
    }

    void _step(int d) {
        int i = _rowOf(_sel);
        do { i = (i + d + _nRows) % _nRows; } while (_rows[i] & 0x80);
        _sel = _rows[i];
        _scrollTo();
    }

    void _race(int d) {
        uint8_t r = Sig::raceOf(_sel);
        do { r = (r + d + Sig::R_COUNT) % Sig::R_COUNT; } while (!Compendium::raceSize(r));
        for (uint8_t t = 0; t < Sig::T_COUNT; t++)
            if (Compendium::listed(t) && Sig::raceOf(t) == r) { _sel = t; break; }
        _scrollTo();
    }

    // Keep the selection visible, with its race header when it fits.
    void _scrollTo() {
        int i = _rowOf(_sel), head = i;
        while (head > 0 && !(_rows[head] & 0x80)) head--;
        if (head < _scroll && i - head < VIS) _scroll = head;
        if (i < _scroll) _scroll = i;
        if (i >= _scroll + VIS) _scroll = i - VIS + 1;
    }

    void _drawList() {
        auto& comp = Compendium::get();
        for (int k = 0; k < VIS && _scroll + k < _nRows; k++) {
            uint8_t row = _rows[_scroll + k];
            int y = LIST_Y + k * ROW_H;
            if (row & 0x80) {
                uint8_t r = row & 0x7F;
                int met = comp.raceMet(r), size = Compendium::raceSize(r);
                char buf[24];
                snprintf(buf, sizeof(buf), "%s %d/%d", Sig::raceName(r), met, size);
                for (char* p = buf; *p; p++) *p = toupper(*p);
                int e = MUI::text(_canvas, MUI::small(), buf, 3, y, _raceCol(r), 1);
                if (met == size) MUI::text(_canvas, MUI::small(), " *", e, y, _raceCol(r));
                continue;
            }
            auto en = comp.entry(row);
            uint16_t col = _raceCol(Sig::raceOf(row));
            if (row == _sel) {
                _canvas.fillRect(1, y - 1, LIST_W - 3, ROW_H, MUI::PANEL);
                _canvas.drawRect(1, y - 1, LIST_W - 3, ROW_H, col);
            }
            if (en.met) MUI::speciesSigil(_canvas, row, 11, y + 5, 10, col);
            else MUI::text(_canvas, MUI::small(), "?", 9, y, MUI::DIM);
            MUI::text(_canvas, MUI::small(), en.met ? _devName(row) : "???", 20, y,
                      en.met ? (row == _sel ? MUI::WHITE : MUI::LABEL) : MUI::DIM);
        }
        _canvas.drawFastVLine(LIST_W, HEADER_H + 1, SCREEN_H - FOOTER_H - HEADER_H - 2, MUI::BONES_DIM);
    }

    // Word-wraps s into width w from (x,y); returns lines drawn.
    int _wrap(const char* s, int x, int y, int w, int maxLines, uint16_t col) {
        char line[64];
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

    void _drawCard() {
        const int x0 = LIST_W + 4, w = SCREEN_W - 3 - x0;
        auto en = Compendium::get().entry(_sel);
        uint8_t race = Sig::raceOf(_sel);
        uint16_t col = _raceCol(race);
        bool fiend = race == Sig::R_FIEND;
        const int sx = x0 + 15, sy = HEADER_H + 17;

        if (!en.met) {
            _canvas.drawCircle(sx, sy, 12, MUI::DIM);
            MUI::textCentered(_canvas, MUI::title(), "?", sx, sy - 6, MUI::DIM);
            MUI::text(_canvas, MUI::title(), "???", x0 + 34, HEADER_H + 3, MUI::DIM, 1);
            MUI::text(_canvas, MUI::small(), Sig::raceName(race), x0 + 34, HEADER_H + 17, col);
            _wrap("Not met yet. It's out there somewhere.", x0, HEADER_H + 40, w, 3, MUI::DIM);
            return;
        }

        // sigil: Fiends jitter, with a red ghost slipping out from under it
        if (fiend) {
            int dx = (int)random(-2, 3), dy = (int)random(-1, 2);
            MUI::speciesSigil(_canvas, _sel, sx + dx + 1, sy + dy, 28, MUI::BONES_DIM);
            MUI::speciesSigil(_canvas, _sel, sx + (random(5) == 0 ? dx : 0), sy, 28, MUI::RED);
            if (random(3) == 0) {   // a torn scanline
                int ty = sy - 12 + (int)random(24);
                _canvas.fillRect(sx - 14, ty, 28, 1 + (int)random(2), MUI::BG);
            }
        } else {
            MUI::speciesSigil(_canvas, _sel, sx, sy, 28, col);
        }

        char name[24];
        strncpy(name, MUI::yokaiName(_sel), sizeof(name) - 1); name[sizeof(name) - 1] = 0;
        if (fiend && random(2) == 0) {
            static const char G[] = "#%&$@!?/<>";
            for (int k = 0; k < 2; k++) {
                int i = random(strlen(name));
                if (name[i] != ' ') name[i] = G[random(sizeof(G) - 1)];
            }
        }
        const int nx = x0 + 34, nw = SCREEN_W - 3 - nx;
        auto* f = MUI::width(_canvas, MUI::title(), name, 1) <= nw ? MUI::title() : MUI::small();
        MUI::text(_canvas, f, name, nx, HEADER_H + 3, col, f == MUI::title() ? 1 : 0);
        char buf[40];
        snprintf(buf, sizeof(buf), "%s  %s", _devName(_sel), Sig::raceName(race));
        MUI::text(_canvas, MUI::small(), buf, nx, HEADER_H + 17, MUI::DIM);

        int y = HEADER_H + 36;
        MUI::text(_canvas, MUI::small(), "First", x0, y, MUI::DIM);
        if (en.first) {
            time_t t = en.first; struct tm tm; localtime_r(&t, &tm);
            strftime(buf, sizeof(buf), "%b %d %Y  %H:%M", &tm);
        } else strcpy(buf, "clock wasn't set");
        MUI::text(_canvas, MUI::small(), buf, x0 + 30, y, MUI::WHITE);
        y += 11;
        MUI::text(_canvas, MUI::small(), "Seen", x0, y, MUI::DIM);
        snprintf(buf, sizeof(buf), en.count == 1 ? "once" : "%lu times", (unsigned long)en.count);
        MUI::text(_canvas, MUI::small(), buf, x0 + 30, y, MUI::WHITE);
        y += 11;
        MUI::text(_canvas, MUI::small(), "Best", x0, y, MUI::DIM);
        snprintf(buf, sizeof(buf), "%d dBm", en.best);
        MUI::text(_canvas, MUI::small(), buf, x0 + 30, y, MUI::WHITE);
        y += 14;
        _wrap(_lore(_sel), x0, y, w, 3, MUI::CREAM);
    }

    void _draw() {
        _lastDraw = millis();
        _canvas.fillSprite(MUI::BG);
        MUI::header(_canvas, "COMPENDIUM", MUI::YELLOW);
        _drawList();
        _drawCard();
        auto& comp = Compendium::get();
        char count[12];
        snprintf(count, sizeof(count), "%d/%d", comp.metCount(), Compendium::total());
        if (comp.persistent()) MUI::footer(_canvas, "; . move   , / race", MUI::DIM, count);
        else MUI::footer(_canvas, "No SD card: forgets on reboot", MUI::YELLOW, count);
        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }
};
