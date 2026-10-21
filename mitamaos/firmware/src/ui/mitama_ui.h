#pragma once
#include <M5Cardputer.h>
#include <WiFi.h>
#include <time.h>
#include "../config.h"
#include "../system/mitama_logo.h"
#include "fonts/rajdhani_sb11.h"
#include "fonts/rajdhani_b12.h"
#include "fonts/rajdhani_b14.h"
#include "fonts/rajdhani_b20.h"

// MitamaOS visual layer (outline §15): Rajdhani type, the Night City surface
// over red bones, the shared top bar / status bar, the Mitama "soul" sprite
// and the esoteric app sigils. Everything is drawn from shapes and baked
// fonts - no bitmaps.
//
// The OS canvas is an 8-bit (RGB332) sprite to save heap, so every colour
// here is picked to survive that quantisation: the locked palette's subtle
// violets would all collapse to black, so panels use the darkest violet
// RGB332 can actually show.
namespace MUI {

    // ---- palette (RGB565 values; see note above) -----------------------
    constexpr uint16_t BG         = 0x0000;  // background
    constexpr uint16_t PANEL      = 0x200A;  // tiles / cards - darkest violet in RGB332
    constexpr uint16_t BONES      = 0xE0A5;  // red bones - structural rules
    constexpr uint16_t BONES_DIM  = 0x7843;  // dim red - quiet borders
    constexpr uint16_t YELLOW     = 0xFF61;  // cyber yellow - titles, selection, calm
    constexpr uint16_t YELLOW_DIM = 0x6B60;  // selection glow
    constexpr uint16_t CYAN       = 0x06DD;  // data, links, curious
    constexpr uint16_t MAGENTA    = 0xF94D;
    constexpr uint16_t GREEN      = 0x3FE2;  // all clear / smug
    constexpr uint16_t PURPLE     = 0xB13F;
    constexpr uint16_t DIM        = 0x8BF5;  // secondary text
    constexpr uint16_t CREAM      = 0xEF36;  // Mitama's voice
    constexpr uint16_t LABEL      = 0xCE1C;  // tile labels
    constexpr uint16_t WHITE      = 0xFFFF;
    constexpr uint16_t RED        = 0xF800;  // alarm - Fiend sightings

    // ---- fonts ----------------------------------------------------------
    // Baked Rajdhani (SIL OFL) as anti-aliased VLW in flash. Each font is
    // parsed once into its own VLWfont object and selected with setFont(),
    // so switching fonts costs nothing per frame. Callers must leave the
    // canvas on the built-in font afterwards: the inherited apps' layouts
    // are pixel-tuned to it. text()/width() below do that for you.
    struct _Font { lgfx::VLWfont font; lgfx::PointerWrapper data; bool tried = false, ok = false; };
    inline const lgfx::IFont* _load(_Font& s, const uint8_t* d, size_t n) {
        if (!s.tried) { s.tried = true; s.data.set(d, n); s.ok = s.font.loadFont(&s.data); }
        return s.ok ? (const lgfx::IFont*)&s.font : (const lgfx::IFont*)&lgfx::fonts::Font0;
    }
    inline const lgfx::IFont* small()   { static _Font s; return _load(s, rajdhani_sb11, sizeof(rajdhani_sb11)); } // labels, body, status bar
    inline const lgfx::IFont* title()   { static _Font s; return _load(s, rajdhani_b12,  sizeof(rajdhani_b12)); }  // top bar, card titles
    inline const lgfx::IFont* big()     { static _Font s; return _load(s, rajdhani_b14,  sizeof(rajdhani_b14)); }  // numerals
    inline const lgfx::IFont* display() { static _Font s; return _load(s, rajdhani_b20,  sizeof(rajdhani_b20)); }  // boot wordmark

    // Width in px of s in font f, with `track` px added between letters.
    inline int width(LGFX_Sprite& c, const lgfx::IFont* f, const char* s, int track = 0) {
        c.setFont(f); c.setTextSize(1);
        int w = c.textWidth(s);
        int n = strlen(s);
        if (track && n > 1) w += track * (n - 1);
        c.setFont(&lgfx::fonts::Font0);
        return w;
    }

    // Draws s with its top-left at (x,y), transparent background (anti-
    // aliased against whatever is already there). Returns the x after it.
    inline int text(LGFX_Sprite& c, const lgfx::IFont* f, const char* s, int x, int y,
                    uint16_t col, int track = 0) {
        c.setFont(f); c.setTextSize(1);
        c.setTextDatum(top_left);
        c.setTextColor(col);
        if (!track) {
            c.drawString(s, x, y);
            x += c.textWidth(s);
        } else {
            char b[2] = {0, 0};
            for (const char* p = s; *p; p++) {
                b[0] = *p;
                c.drawString(b, x, y);
                x += c.textWidth(b) + track;
            }
            x -= track;
        }
        c.setFont(&lgfx::fonts::Font0);
        return x;
    }

    inline void textCentered(LGFX_Sprite& c, const lgfx::IFont* f, const char* s, int cx, int y,
                             uint16_t col, int track = 0) {
        text(c, f, s, cx - width(c, f, s, track) / 2, y, col, track);
    }

    // ---- clock ----------------------------------------------------------
    // Non-blocking: getLocalTime() waits up to its timeout when the clock
    // was never set, which would stall every top-bar redraw.
    inline bool clockSet() { return time(nullptr) > 1600000000; }
    inline bool localNow(struct tm& t) {
        if (!clockSet()) return false;
        time_t now = time(nullptr);
        localtime_r(&now, &t);
        return true;
    }

    // ---- the Mitama -----------------------------------------------------
    // Small magatama glyph filling a size x size box at (x,y): the Mitama's
    // presence in the top bar and status bar, tinted by mood.
    inline void mini(LGFX_Sprite& c, int x, int y, int size, uint16_t mood) {
        float Rm = size * 0.22f, tH = size * 0.17f;
        MitamaLogo::_magatama(c, x + size / 2.0f, y + size / 2.0f + tH * 0.3f, Rm, tH,
                              -60, 185, 0.82f, mood);
    }

    // Dark shades of an aura colour for the soul's glow rings (RGB332-safe).
    inline void _auraShades(uint16_t aura, uint16_t out[3]) {
        switch (aura) {
            case GREEN:   out[0] = 0x0360; out[1] = 0x0240; out[2] = 0x0120; break;
            case CYAN:    out[0] = 0x0349; out[1] = 0x0226; out[2] = 0x0123; break;
            case MAGENTA: out[0] = 0x6809; out[1] = 0x4806; out[2] = 0x2003; break;
            case BONES:   out[0] = 0x6800; out[1] = 0x4800; out[2] = 0x2000; break;
            default:      out[0] = 0x6B60; out[1] = 0x4A40; out[2] = 0x2120; break; // yellow
        }
    }

    // The full Mitama: a "soul" core (dark disc, accent ring, dashed bones
    // ring, the magatama sigil) inside an aura glow. `size` is the 200-unit
    // design box from docs/ui.html's soul(); (cx,cy) is the centre. `spin`
    // rotates the dashed bones ring (degrees) for the slow idle turn.
    inline void soul(LGFX_Sprite& c, int cx, int cy, int size, uint16_t accent, uint16_t aura,
                     float spin = 0, uint16_t bones = BONES) {
        float s = size / 200.0f;
        uint16_t sh[3]; _auraShades(aura, sh);
        int ay = cy + (int)(4 * s);
        c.fillCircle(cx, ay, (int)(86 * s), sh[2]);
        c.fillCircle(cx, ay, (int)(76 * s), sh[1]);
        c.fillCircle(cx, ay, (int)(67 * s), sh[0]);
        c.fillCircle(cx, cy, (int)(58 * s), PANEL);
        int r = (int)(58 * s), w = max(1, (int)(3 * s + 0.5f));
        c.fillArc(cx, cy, r - w + 1, r, 0, 360, accent);
        MitamaLogo::_dashRing(c, cx, cy, 50 * s, 1, 3, 6, bones, spin);
        MitamaLogo::_magatama(c, cx, cy + 6 * s, 19 * s, 14 * s, -60, 185, 0.82f, accent);
    }

    // ---- top bar --------------------------------------------------------
    inline void _wifiGlyph(LGFX_Sprite& c, int x, int y) { // (x,y) = bottom centre
        bool on = WiFi.status() == WL_CONNECTED;
        int rssi = on ? WiFi.RSSI() : -100;
        int arcs = !on ? 0 : rssi > -60 ? 2 : rssi > -75 ? 1 : 0;
        uint16_t col = on ? CYAN : 0x4208;
        c.fillRect(x - 1, y - 1, 2, 2, col);
        for (int i = 0; i < 2; i++) {
            int r = 4 + i * 3;
            c.fillArc(x, y, r, r, 225, 315, i < arcs ? col : (uint16_t)0x4208);
        }
    }

    inline void _battery(LGFX_Sprite& c, int x, int y) {
        int bat = constrain(map(M5Cardputer.Power.getBatteryVoltage(), 3300, 4200, 0, 100), 0, 100);
        c.drawRect(x, y, 14, 7, DIM);
        c.fillRect(x + 14, y + 2, 2, 3, DIM);
        int bw = map(bat, 0, 100, 0, 10);
        uint16_t col = bat > 30 ? GREEN : (bat > 15 ? YELLOW : BONES);
        if (bw > 0) c.fillRect(x + 2, y + 2, bw, 3, col);
    }

    // GPS state for the top bar, set by the GPS service: 0 no GPS cap,
    // 1 searching, 2 fix.
    inline volatile uint8_t gpsState = 0;

    // Small satellite-dish glyph: a ring with a centre dot, centred (x,y).
    inline void _gpsGlyph(LGFX_Sprite& c, int x, int y) {
        uint16_t col = gpsState == 2 ? GREEN : DIM;
        c.drawCircle(x, y, 4, col);
        c.fillCircle(x, y, 1, col);
        c.drawFastHLine(x - 6, y, 2, col);
        c.drawFastHLine(x + 5, y, 2, col);
    }

    // Shared top bar (rows 0..HEADER_H-1): mood magatama + title on the
    // left, clock / Wi-Fi / battery on the right, red bones rule under it.
    // `accent` tints the magatama and the rule for state (e.g. bones red
    // while disconnected); YELLOW is the normal calm state.
    inline void header(LGFX_Sprite& c, const char* name, uint16_t accent = YELLOW) {
        c.fillRect(0, 0, SCREEN_W, HEADER_H - 1, BG);
        c.fillRect(0, HEADER_H - 1, SCREEN_W, 1, accent == YELLOW ? BONES : accent);
        mini(c, 2, 1, 13, accent);

        _battery(c, SCREEN_W - 18, 4);
        _wifiGlyph(c, SCREEN_W - 28, 11);

        char tbuf[8] = "--:--";
        struct tm t;
        bool set = localNow(t);
        if (set) strftime(tbuf, sizeof(tbuf), "%H:%M", &t);
        int tw = width(c, small(), tbuf);
        int tx = SCREEN_W - 38 - tw;
        text(c, small(), tbuf, tx, 1, set ? CYAN : DIM);
        if (gpsState) { _gpsGlyph(c, tx - 9, 7); tx -= 16; }   // left of the clock

        c.setClipRect(17, 0, tx - 21, HEADER_H - 1);
        text(c, title(), name ? name : "", 17, 1, YELLOW, 1);
        c.clearClipRect();
    }

    // Shared status bar (bottom FOOTER_H rows): the Mitama's magatama and
    // a line of text - key hints in apps, its own voice on Proxima. An
    // optional right-aligned hint sits at the far edge.
    inline void footer(LGFX_Sprite& c, const char* say, uint16_t sayCol = DIM,
                       const char* rightHint = nullptr, uint16_t mood = YELLOW, int scroll = 0) {
        int y = SCREEN_H - FOOTER_H;
        c.fillRect(0, y, SCREEN_W, FOOTER_H, BG);
        c.fillRect(0, y, SCREEN_W, 1, BONES_DIM);
        mini(c, 2, y + 1, 13, mood);
        int right = SCREEN_W - 3;
        if (rightHint && *rightHint) {
            int hw = width(c, small(), rightHint);
            right -= hw;
            text(c, small(), rightHint, right, y + 1, DIM);
            right -= 6;
        }
        if (say && *say) {
            c.setClipRect(17, y + 1, right - 17, FOOTER_H - 1);
            text(c, small(), say, 17 - scroll, y + 1, sayCol);
            c.clearClipRect();
        }
    }

    // ---- app sigils (outline §15 "Icon system") ------------------------
    // Esoteric line sigils on a 24-unit grid, drawn at `size` px centred on
    // (cx,cy). Locked set plus provisional sigils for the inherited apps
    // the icon study didn't cover (Store, Piano, 3D, Calc, Clock, myAI,
    // System) - same grammar, open to redesign.
    enum Sigil : uint8_t {
        SIG_COMPENDIUM, SIG_PROXIMA, SIG_FILES, SIG_PAINT, SIG_MUSIC, SIG_GAMES,
        SIG_LUA, SIG_GRIMOIRE, SIG_BROWSER, SIG_SETTINGS, SIG_STORE, SIG_PIANO,
        SIG_CUBE, SIG_CALC, SIG_CLOCK, SIG_ORACLE, SIG_SYSTEM, SIG_SCRY, SIG_MOLT, SIG_KOTODAMA, SIG_OMIKUJI, SIG_AETHER, SIG_UNKNOWN
    };

    struct _Pen {
        LGFX_Sprite& c; float cx, cy, s; uint16_t col;
        float X(float v) const { return cx + (v - 12) * s; }
        float Y(float v) const { return cy + (v - 12) * s; }
        void line(float x0, float y0, float x1, float y1) const {
            c.drawWideLine(lroundf(X(x0)), lroundf(Y(y0)), lroundf(X(x1)), lroundf(Y(y1)), 0.75f, col);
        }
        void poly(const float* p, int n, bool closed = true) const {
            for (int i = 0; i + 1 < n; i++) line(p[i*2], p[i*2+1], p[i*2+2], p[i*2+3]);
            if (closed) line(p[n*2-2], p[n*2-1], p[0], p[1]);
        }
        void ring(float x, float y, float r) const {
            int rr = (int)(r * s + 0.5f);
            c.fillArc((int)X(x), (int)Y(y), rr - 1, rr, 0, 360, col);
        }
        void arc(float x, float y, float r, float a0, float a1) const {
            int rr = (int)(r * s + 0.5f);
            c.fillArc((int)X(x), (int)Y(y), rr - 1, rr, a0, a1, col);
        }
        void dashed(float x, float y, float r, float on, float off) const {
            MitamaLogo::_dashRing(c, (int)X(x), (int)Y(y), r * s, 1, on, off, col);
        }
        void dot(float x, float y, float r) const {
            c.fillCircle((int)(X(x) + 0.5f), (int)(Y(y) + 0.5f), max(1, (int)(r * s + 0.5f)), col);
        }
        void rect(float x, float y, float w, float h) const {
            float p[] = { x, y, x + w, y, x + w, y + h, x, y + h };
            poly(p, 4);
        }
    };

    inline void sigil(LGFX_Sprite& c, Sigil id, int cx, int cy, int size, uint16_t col) {
        _Pen p{ c, (float)cx, (float)cy, size / 24.0f, col };
        switch (id) {
        case SIG_COMPENDIUM: { // transmutation circle: the seal that keeps the spirits bound
            p.ring(12, 12, 10.4f); p.ring(12, 12, 8.2f);
            float t[] = { 12, 3.8f, 19.1f, 16.1f, 4.9f, 16.1f };
            p.poly(t, 3);
            for (int i = 0; i < 3; i++) p.ring(t[i * 2], t[i * 2 + 1], 1.2f);
            float q[] = { 13.56f, 15.16f, 10.44f, 15.16f, 10.44f, 12.04f, 13.56f, 12.04f };
            p.poly(q, 4);
            break; }
        case SIG_PROXIMA: { // core + dashed orbit + cardinal ticks
            p.dot(12, 12, 3);
            p.line(12, 4, 12, 6); p.line(12, 18, 12, 20); p.line(4, 12, 6, 12); p.line(18, 12, 20, 12);
            p.dashed(12, 12, 8, 3, 4);
            break; }
        case SIG_FILES: { // warded archive-diamond
            float d[] = { 12, 4.5f, 19.5f, 12, 12, 19.5f, 4.5f, 12 };
            p.poly(d, 4);
            p.line(9.3f, 11, 14.7f, 11); p.line(9.7f, 13.2f, 14.3f, 13.2f);
            p.dot(12, 2.4f, 0.9f); p.dot(21.6f, 12, 0.9f); p.dot(12, 21.6f, 0.9f); p.dot(2.4f, 12, 0.9f);
            break; }
        case SIG_PAINT: { // the squared circle
            p.rect(4.5f, 4.5f, 15, 15);
            p.ring(12, 12, 6.6f);
            float t[] = { 12, 7.2f, 16.7f, 15.8f, 7.3f, 15.8f };
            p.poly(t, 3);
            break; }
        case SIG_MUSIC: // cymatic sound-mandala
            p.dot(12, 12, 1.8f);
            p.arc(12, 12, 5, 135, 225); p.arc(12, 12, 9, 135, 225);
            p.arc(12, 12, 5, -45, 45);  p.arc(12, 12, 9, -45, 45);
            break;
        case SIG_GAMES: { // pentagram + centre point
            float st[10];
            for (int i = 0; i < 5; i++) {
                float a = (-90 + 144 * i) * (float)DEG_TO_RAD;
                st[i*2] = 12 + 9 * cosf(a); st[i*2+1] = 12.6f + 9 * sinf(a);
            }
            p.poly(st, 5);
            p.dot(12, 12.6f, 1.2f);
            break; }
        case SIG_LUA: { // >_ bound in a hexagon
            float h[] = { 12, 2.8f, 20, 7.4f, 20, 16.6f, 12, 21.2f, 4, 16.6f, 4, 7.4f };
            p.poly(h, 6);
            p.line(9.2f, 9.8f, 11.6f, 12); p.line(11.6f, 12, 9.2f, 14.2f); p.line(12.6f, 14.4f, 16, 14.4f);
            break; }
        case SIG_GRIMOIRE: { // spine + clasp + circle-and-triangle seal
            p.rect(6.3f, 3.6f, 11.4f, 16.8f);
            p.line(9.2f, 3.6f, 9.2f, 20.4f);
            p.ring(13.4f, 11.3f, 2.9f);
            float clasp[] = { 17.7f, 9.5f, 20.3f, 9.5f, 20.3f, 13.3f, 17.7f, 13.3f };
            p.poly(clasp, 4, false);
            break; }
        case SIG_BROWSER: // astrolabe
            p.ring(12, 12, 8.4f);
            c.drawEllipse(cx, cy, (int)(3.4f * p.s), (int)(8.4f * p.s), col);
            p.line(3.6f, 12, 20.4f, 12);
            p.line(12, 1.5f, 12, 3.4f); p.line(12, 20.6f, 12, 22.5f);
            p.line(1.5f, 12, 3.4f, 12); p.line(20.6f, 12, 22.5f, 12);
            break;
        case SIG_SETTINGS: // orrery: nested rings + orbiting nodes
            p.ring(12, 12, 8); p.ring(12, 12, 3.3f);
            p.dot(12, 4, 1.4f); p.dot(19, 15.6f, 1.4f); p.dot(5.4f, 16.6f, 1.4f);
            break;
        case SIG_STORE: { // offering chalice in a ring
            p.ring(12, 12, 9);
            float cup[] = { 7.5f, 8, 16.5f, 8, 12, 14.5f };
            p.poly(cup, 3);
            p.line(12, 14.5f, 12, 17); p.line(9.5f, 17, 14.5f, 17);
            break; }
        case SIG_PIANO: // harp strings in a ring
            p.ring(12, 12, 9);
            p.line(8.5f, 7.5f, 8.5f, 16.5f); p.line(12, 5.5f, 12, 18.5f); p.line(15.5f, 7.5f, 15.5f, 16.5f);
            break;
        case SIG_CUBE: { // hexagon + inner Y: the cube
            float h[] = { 12, 3, 19.8f, 7.5f, 19.8f, 16.5f, 12, 21, 4.2f, 16.5f, 4.2f, 7.5f };
            p.poly(h, 6);
            p.line(12, 12, 12, 21); p.line(12, 12, 4.2f, 7.5f); p.line(12, 12, 19.8f, 7.5f);
            break; }
        case SIG_CALC: // eight-pointed star of + and x in a ring
            p.ring(12, 12, 9);
            p.line(12, 6, 12, 18); p.line(6, 12, 18, 12);
            p.line(8, 8, 16, 16);  p.line(16, 8, 8, 16);
            break;
        case SIG_CLOCK: { // hourglass in a ring
            p.ring(12, 12, 9.5f);
            float hg[] = { 8, 6.5f, 16, 6.5f, 8, 17.5f, 16, 17.5f };
            p.poly(hg, 4);
            break; }
        case SIG_ORACLE: // three linked circles
            p.ring(12, 9, 4); p.ring(8.8f, 14.6f, 4); p.ring(15.2f, 14.6f, 4);
            break;
        case SIG_SCRY: { // the spoken seal: a sigil rises from the torii and scatters into the unseen
            float kasagi[] = { 4.6f, 12.6f, 6.2f, 13.5f, 17.8f, 13.5f, 19.4f, 12.6f };
            p.poly(kasagi, 4, false);
            p.line(7, 15.6f, 17, 15.6f);
            p.line(8.4f, 13.5f, 8.4f, 21); p.line(15.6f, 13.5f, 15.6f, 21);
            float seal[] = { 12, 3.6f, 14.6f, 6.6f, 12, 9.6f, 9.4f, 6.6f };
            p.poly(seal, 4);
            p.dot(12, 6.6f, 0.9f);
            p.dot(7.6f, 4.4f, 0.8f); p.dot(16.6f, 3.4f, 0.6f); p.dot(17.6f, 7.4f, 0.9f); p.dot(6.6f, 8.4f, 0.5f);
            break; }
        case SIG_MOLT: { // the spirit rising out of its cracked husk: a hollow magatama arched like an "n"
            // husk centre, radius, half-width; arc runs 180..360 deg (head lower left, tail lower right)
            const float hx = 12, hy = 17.33f, hR = 4.0f, hT = 1.55f;
            auto husk = [&](float u0, float u1, bool head) {
                const int N = 16;
                float pts[(N + 1) * 4 + 4 + 15 * 2]; int n = 0;
                auto put = [&](float x, float y) { pts[n * 2] = x; pts[n * 2 + 1] = y; n++; };
                auto edge = [&](float u, float side) {
                    float th = (180 + 180 * u) * (float)DEG_TO_RAD, t = hT * powf(1 - u, 0.82f);
                    put(hx + (hR + side * t) * cosf(th), hy + (hR + side * t) * sinf(th));
                };
                auto jag = [&](float u, float dir) {   // the broken edge points into the crack
                    float th = (180 + 180 * u + dir * 6.3f) * (float)DEG_TO_RAD;
                    put(hx + hR * cosf(th), hy + hR * sinf(th));
                };
                for (int i = 0; i <= N; i++) edge(u0 + (u1 - u0) * i / N, 1);
                if (u1 < 1) jag(u1, 1);
                for (int i = N; i >= 0; i--) edge(u0 + (u1 - u0) * i / N, -1);
                if (u0 > 0) jag(u0, -1);
                if (head) {                             // round head cap at the lower-left end
                    float hcx = hx - hR, hcy = hy;
                    for (int i = 1; i < 16; i++) {
                        float ph = (float)M_PI * (1 - i / 16.0f);
                        put(hcx - hT * cosf(ph), hcy + hT * sinf(ph));
                    }
                }
                p.poly(pts, n);
            };
            husk(0, 0.36f, true);
            husk(0.64f, 1, false);
            auto shard = [&](float x, float y, float r, float a) {
                const float rr[] = { 1, 0.6f, 0.9f }, da[] = { 0, 130, 230 };
                float t[6];
                for (int k = 0; k < 3; k++) {
                    float th = (a + da[k]) * (float)DEG_TO_RAD;
                    t[k * 2] = x + r * rr[k] * cosf(th); t[k * 2 + 1] = y + r * rr[k] * sinf(th);
                }
                p.poly(t, 3);
            };
            shard(6.4f, 13.53f, 0.56f, 200); shard(17.6f, 12.93f, 0.53f, -20); shard(8.2f, 9.93f, 0.42f, 60);
            MitamaLogo::_magatama(c, p.X(12.8f), p.Y(9.33f), 2.6f * p.s, 1.9f * p.s, -60, 185, 0.82f, col);
            break; }
        case SIG_KOTODAMA: // one spirit, four souls (ichirei shikon): four magatama turned to the centre
            for (int k = 0; k < 4; k++) {
                float phi = -45 + 90 * k, r = phi * (float)DEG_TO_RAD;
                float mx = 12 + (3.6f + 2.4f) * cosf(r), my = 12 + (3.6f + 2.4f) * sinf(r);
                MitamaLogo::_magatama(c, p.X(mx), p.Y(my), 2.4f * p.s, 1.6f * p.s, phi + 180, phi + 415, 0.82f, col);
            }
            p.dot(12, 12, 1.4f);
            break;
        case SIG_OMIKUJI: { // an unrolled fortune slip from the shrine, its rods top and bottom
            p.rect(6, 4, 12, 16);
            p.line(4.6f, 4, 19.4f, 4); p.line(4.6f, 20, 19.4f, 20);
            p.line(8.6f, 4, 8.6f, 20); p.line(15.4f, 4, 15.4f, 20);
            p.line(10.6f, 8, 13.4f, 8); p.line(10.6f, 10.6f, 13.4f, 10.6f);
            p.ring(12, 15, 1.8f);
            break; }
        case SIG_AETHER: // the seeing stone: a vertical eye set in a rimmed round stone
            p.ring(12, 12, 9.6f); p.ring(12, 12, 8.2f);
            p.arc(18.2f, 12, 8.6f, 145, 215); p.arc(5.8f, 12, 8.6f, -35, 35);
            p.ring(12, 12, 2.2f);
            p.dot(12, 12, 0.9f);
            break;
        case SIG_SYSTEM: // sealed chip
            p.rect(7.5f, 7.5f, 9, 9);
            for (int i = 0; i < 3; i++) {
                float v = 9.5f + i * 2.5f;
                p.line(v, 4.5f, v, 7.5f); p.line(v, 16.5f, v, 19.5f);
                p.line(4.5f, v, 7.5f, v); p.line(16.5f, v, 19.5f, v);
            }
            p.dot(12, 12, 1.2f);
            break;
        default:
            p.dashed(12, 12, 8, 2, 4);
            p.dot(12, 12, 2);
            break;
        }
    }
}
