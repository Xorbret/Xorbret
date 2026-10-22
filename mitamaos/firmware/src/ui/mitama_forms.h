#pragma once
// The Mitama's forms (outline §5a "unlockable forms"): the Soul with
// something added around it. Never a face - every form decorates the ring,
// the aura or the space beside it, and the aura keeps showing the mood.
//
// Drawn in the same 200-unit box as MUI::soul(); `s` is px per unit and
// `f` scales the few fixed pixel sizes from the 92 px home-screen Mitama.
// Unlock rules live in mitama/forms.h.
#include "mitama_ui.h"

namespace MUI {

enum Form : uint8_t {
    F_SOUL, F_HITODAMA, F_NETRUNNER, F_TENGU, F_RYUJIN, F_KAMI,
    F_WANDERER, F_HYAKKI, F_SHIKIGAMI, F_GILDED,
    F_TINFOIL, F_BRAINDANCE, F_KITSUNE, F_ONI, F_OMAMORI,
    F_KABUTO, F_MEKURA, F_SUZU, F_HOSHI,
    F_YUREI, F_KEKKAI, F_CHOCHIN, F_KOTODAMA,
    F_KYUBI,        // the creator's own form: never listed, unlocked by code
    F_COUNT
};
constexpr uint8_t F_PUBLIC = F_KYUBI;   // forms anyone can earn

namespace _FormCol {
    constexpr uint16_t GOLD   = 0xFE87;
    constexpr uint16_t TAN    = 0xDD8C;
    constexpr uint16_t BROWN  = 0x8AC5;
    constexpr uint16_t PALE   = 0xDF3F;
    constexpr uint16_t FOIL   = 0xCE5B;
    constexpr uint16_t CREASE = 0x7BD2;
    constexpr uint16_t GHOST  = 0x6394;
    constexpr uint16_t PINK   = 0xFC10;
}

struct _FormPen {
    LGFX_Sprite& c; float cx, cy, s, f;
    // a point at angle a (deg, screen coords) and radius r (units) from the centre
    float px(float a, float r) const { return cx + r * s * cosf(a * (float)DEG_TO_RAD); }
    float py(float a, float r) const { return cy + r * s * sinf(a * (float)DEG_TO_RAD); }
    void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t col) const {
        c.fillTriangle(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), lroundf(x2), lroundf(y2), col);
    }
    // convex polygon, as a triangle fan
    void poly(const float* p, int n, uint16_t col) const {
        for (int i = 1; i + 1 < n; i++) tri(p[0], p[1], p[i*2], p[i*2+1], p[i*2+2], p[i*2+3], col);
    }
    void disc(float x, float y, float r, uint16_t col) const {
        c.fillCircle(lroundf(x), lroundf(y), max(1, (int)lroundf(r)), col);
    }
    void line(float x0, float y0, float x1, float y1, uint16_t col, float w = 1) const {
        if (w <= 1) c.drawLine(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), col);
        else c.drawWideLine(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), w / 2, col);
    }
    void rect(float x, float y, float w, float h, uint16_t col) const {
        c.fillRect(lroundf(x), lroundf(y), max(1, (int)lroundf(w)), max(1, (int)lroundf(h)), col);
    }
    void mag(float x, float y, float Rm, float tH, float a0, float a1, uint16_t col) const {
        MitamaLogo::_magatama(c, x, y, Rm * f, tH * f, a0, a1, 0.82f, col);
    }
    // a tapered band (a horn): outer/inner edges of `n` samples
    void band(const float* o, const float* in, int n, uint16_t col) const {
        for (int i = 0; i + 1 < n; i++) {
            tri(o[i*2], o[i*2+1], o[i*2+2], o[i*2+3], in[i*2], in[i*2+1], col);
            tri(o[i*2+2], o[i*2+3], in[i*2+2], in[i*2+3], in[i*2], in[i*2+1], col);
        }
    }
};

// Animation clock for forms: everything that moves keys off this, so a
// screen that redraws often (Proxima, ~10 fps) animates and one that
// doesn't just shows a frame.
inline float _formT() { return millis() / 1000.0f; }
inline uint32_t _formHash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }

// Slices of whatever is drawn, shoved sideways, plus stray scanlines.
// Animated: a new pattern every ~90 ms, mostly a light flicker with
// short heavy bursts. `hard` (the creator's form) bursts more often.
inline void _formGlitch(const _FormPen& p, bool hard = false) {
    const float cx = p.cx, cy = p.cy, s = p.s, f = p.f;
    uint32_t ms = millis(), fr = ms / 90;
    uint32_t cycle = hard ? 1700 : 2600;
    bool burst = ms % cycle < (hard ? 420 : 300);
    uint32_t h0 = _formHash(fr);
    int slices = burst ? 4 : (h0 & 3) == 0 ? 0 : 2;     // now and then, a calm frame
    int x0 = lroundf(cx - 96 * s), x1 = lroundf(cx + 98 * s);
    int w = x1 - x0;
    if (w > 0 && w <= 240) {
        uint16_t row[240];
        for (int j = 0; j < slices; j++) {
            uint32_t h = _formHash(fr * 7 + j + 1);
            float dyU = (float)(h % 150) - 75;
            int maxDx = burst ? 12 : 6;
            int dx = lroundf(((int)((h >> 8) % (2 * maxDx + 1)) - maxDx) * f);
            if (!dx) dx = (h & 0x100000) ? 1 : -1;
            int y0 = (int)(cy + dyU * s), th = max(2, (int)lroundf(((h >> 16) % (burst ? 5 : 3) + 1) * f));
            for (int y = y0; y < y0 + th; y++) {
                for (int i = 0; i < w; i++) row[i] = p.c.readPixel(x0 + i, y);
                for (int i = 0; i < w; i++) {
                    int src = i - dx;
                    if (src >= 0 && src < w) p.c.drawPixel(x0 + i, y, row[src]);
                }
            }
        }
    }
    int lines = burst ? 4 : 2;
    for (int j = 0; j < lines; j++) {
        uint32_t h = _formHash(fr * 13 + j + 101);
        float xu = (float)(h % 140) - 70, yu = (float)((h >> 8) % 120) - 60;
        p.rect(cx + xu * s, cy + yu * s, (4 + (h >> 20) % 8) * f, 1, (j & 1) ? MAGENTA : CYAN);
    }
}

inline void _formBehind(const _FormPen& p, Form form) {
    if (form != F_KAMI) return;
    // a torii standing behind the Mitama
    float cx = p.cx, cy = p.cy, s = p.s;
    for (int side = -1; side <= 1; side += 2) p.rect(cx + side * 80 * s - 1.5f * p.f, cy - 80 * s, 3 * p.f, 166 * s, BONES);
    float beam[] = { cx - 94 * s, cy - 94 * s, cx + 94 * s, cy - 94 * s, cx + 86 * s, cy - 84 * s, cx - 86 * s, cy - 84 * s };
    p.poly(beam, 4, BONES);
    p.rect(cx - 86 * s, cy - 72 * s, 172 * s, 3 * p.f, BONES);
}

inline void _formFront(const _FormPen& p, Form form, uint16_t accent) {
    using namespace _FormCol;
    const float cx = p.cx, cy = p.cy, s = p.s, f = p.f;
    const float T = _formT();
    switch (form) {
    case F_HITODAMA: // two spirit flames
        for (float a : { 200.0f, -20.0f }) {
            float ph = a * 0.05f;
            float x = p.px(a, 80), y = p.py(a, 80) + 3 * f * sinf(T * 2.2f + ph), r = 3.4f * f;
            float tip = 2.3f + 0.6f * sinf(T * 9 + ph * 3) + 0.3f * sinf(T * 23 + ph);   // the flame licks
            p.disc(x, y, r, CYAN);
            p.tri(x - r, y, x + r * (0.3f + 0.4f * sinf(T * 6 + ph)), y - r * tip, x + r, y, CYAN);
            p.disc(x, y, 1.4f * f, WHITE);
        }
        break;
    case F_NETRUNNER: // headset and mic
        for (int k = 0; k < 40; k++) {
            float a = -160 + 140 * k / 39.0f;
            p.disc(p.px(a, 64), p.py(a, 64), 1.1f * f, CYAN);
        }
        for (int side = -1; side <= 1; side += 2)
            p.rect(cx + side * 62 * s - (side < 0 ? 3 : 1) * f, cy - 10 * s, 5 * f, 12 * f, CYAN);
        p.line(cx + 60 * s, cy + 12 * s, cx + 30 * s, cy + 40 * s, CYAN);
        if (fmodf(T, 1.2f) < 0.7f) p.disc(cx + 30 * s, cy + 40 * s, 1.6f * f, MAGENTA);   // live mic
        break;
    case F_TENGU: // feathered wings
        for (int side = -1; side <= 1; side += 2) {
            const float LN[] = { 58, 52, 44 }, AN[] = { -35, -15, 5 };
            float flap = 9 * sinf(T * 2.4f);    // a slow wingbeat
            for (int k = 0; k < 3; k++) {
                float bx = cx + side * 54 * s, by = cy + (-14 + k * 14) * s;
                float a = (AN[k] - flap * (1 - k * 0.3f)) * (float)DEG_TO_RAD;
                float tx = bx + side * LN[k] * s * cosf(a), ty = by + LN[k] * s * sinf(a) - 14 * s;
                float nx = -(ty - by), ny = tx - bx, L = sqrtf(nx * nx + ny * ny);
                nx = nx / L * 4 * f; ny = ny / L * 4 * f;
                p.tri(bx + nx, by + ny, tx, ty, bx - nx, by - ny, k == 0 ? WHITE : DIM);
            }
        }
        break;
    case F_RYUJIN: { // a dragon coiled around the ring
        const int N = 120;
        float x0 = 0, y0 = 0, hx = 0, hy = 0;
        for (int k = 0; k < N; k++) {
            float t = (-60 + 300 * k / (float)(N - 1)) * (float)DEG_TO_RAD;
            float r = 68 + 6 * sinf(5 * t - T * 3);   // the coils ripple along
            float x = cx + r * s * cosf(t), y = cy + r * s * sinf(t);
            if (k == 0) { hx = x; hy = y; }
            else p.line(x0, y0, x, y, CYAN, (2.6f * (1 - k / (float)N) + 0.8f) * f);
            x0 = x; y0 = y;
        }
        p.tri(hx - 4 * f, hy - 3 * f, hx + 5 * f, hy - 1 * f, hx - 2 * f, hy + 4 * f, CYAN);
        p.line(hx + 2 * f, hy - 2 * f, hx + 6 * f, hy - 7 * f, GOLD);
        break; }
    case F_WANDERER: { // straw kasa hat
        float hat[] = { cx - 80 * s, cy - 44 * s, cx, cy - 82 * s, cx + 80 * s, cy - 44 * s,
                        cx + 70 * s, cy - 40 * s, cx - 70 * s, cy - 40 * s };
        p.poly(hat, 5, TAN);
        for (float k : { -40.0f, -14.0f, 14.0f, 40.0f }) p.line(cx, cy - 80 * s, cx + k * 1.6f * s, cy - 44 * s, BROWN);
        break; }
    case F_HYAKKI: { // a little night parade of magatama
        const float A[] = { 140, 175, 210 }; const uint16_t C[] = { MAGENTA, CYAN, GREEN };
        float walk = fmodf(T * 12, 360);       // the parade marches round the ring
        for (int k = 0; k < 3; k++) {
            float a = A[k] + walk, r = 82 + 3 * sinf(T * 4 + k * 2);
            p.mag(p.px(a, r), p.py(a, r), 3.0f, 2.3f, -60 + walk, 185 + walk, C[k]);
        }
        break; }
    case F_SHIKIGAMI: { // a paper cut-out familiar, tilted, at the upper right
        float x0 = cx + 70 * s, y0 = cy - 46 * s, k = 1.2f * s;
        y0 += 2 * f * sinf(T * 1.7f);
        float tilt = 14 + 5 * sinf(T * 2.3f);    // paper flutters
        float ct = cosf(tilt * (float)DEG_TO_RAD), st = sinf(tilt * (float)DEG_TO_RAD);
        auto T = [&](const float* in, int n, float* out) {
            for (int i = 0; i < n; i++) {
                float u = in[i*2], v = in[i*2+1];
                out[i*2] = x0 + (u * ct - v * st) * k; out[i*2+1] = y0 + (u * st + v * ct) * k;
            }
        };
        const float torso[] = { -5, -6, 5, -6, 6, 4, -6, 4 };
        const float legL[]  = { -6, 4, 0, 4, 0, 20, -1, 28, -7, 28 };
        const float legR[]  = { 0, 4, 6, 4, 7, 28, 1, 28, 0, 20 };
        const float armR[]  = { 5, -6, 17, 6, 13, 10, 6, 4 };
        const float armL[]  = { -5, -6, -6, 4, -13, 10, -17, 6 };
        const float sash[]  = { -6, 12, 6, 12, 0, -12 };
        float q[10];
        T(torso, 4, q); p.poly(q, 4, WHITE);
        T(legL, 5, q);  p.poly(q, 5, WHITE);
        T(legR, 5, q);  p.poly(q, 5, WHITE);
        T(armR, 4, q);  p.poly(q, 4, WHITE);
        T(armL, 4, q);  p.poly(q, 4, WHITE);
        T(sash, 3, q);
        p.disc(q[4], q[5], 5 * k, WHITE);
        p.line(q[0], q[1], q[2], q[3], BONES);
        break; }
    case F_TINFOIL: { // foil hat
        float lx = p.px(-150, 58), ly = p.py(-150, 58), rx = p.px(-30, 58), ry = p.py(-30, 58);
        p.tri(lx, ly, cx + 6 * s, cy - 104 * s, rx, ry, FOIL);
        p.line(cx - 8 * s, cy - 60 * s, cx + 2 * s, cy - 92 * s, CREASE);
        p.line(cx + 18 * s, cy - 56 * s, cx + 4 * s, cy - 84 * s, CREASE);
        float zx = lx, zy = ly - 1 * s;
        for (int k = 1; k <= 8; k++) {
            float x = lx + (rx - lx) * k / 8, y = ly + (ry - ly) * k / 8 + (k % 2 ? 3 : -1) * s;
            p.line(zx, zy, x, y, CREASE);
            zx = x; zy = y;
        }
        break; }
    case F_BRAINDANCE: // slices of the soul shoved sideways
        _formGlitch(p, false);
        break;
    case F_KYUBI: { // the creator: hollow neon ears, green foxfire, a doubled crest, glitching
        // the colour split jitters, and pulls wide during a glitch burst
        uint32_t fr = millis() / 90;
        bool burst = millis() % 1700 < 420;
        float split = burst ? 3.0f + (_formHash(fr) % 4) : 1.5f + 0.5f * sinf(T * 5);
        float jy = burst ? (float)((int)(_formHash(fr + 9) % 5) - 2) : 0;
        MitamaLogo::_magatama(p.c, cx - split * f, cy + 6 * s + jy, 19 * s, 14 * s, -60, 185, 0.82f, MAGENTA);
        MitamaLogo::_magatama(p.c, cx + split * 0.6f * f, cy + 6 * s - jy, 19 * s, 14 * s, -60, 185, 0.82f, CYAN);
        for (int side = -1; side <= 1; side += 2) {
            p.tri(p.px(-90 + side * 18, 57), p.py(-90 + side * 18, 57), cx + side * 40 * s, cy - 100 * s,
                  p.px(-90 + side * 56, 57), p.py(-90 + side * 56, 57), MAGENTA);
            p.tri(p.px(-90 + side * 31, 62), p.py(-90 + side * 31, 62), cx + side * 38 * s, cy - 80 * s,
                  p.px(-90 + side * 44, 62), p.py(-90 + side * 44, 62), PANEL);
        }
        {   // foxfire drifts and flickers
            const float A[] = { 205, -25, 155 }, R[] = { 78, 80, 84 };
            for (int k = 0; k < 3; k++) {
                float a = A[k] + 6 * sinf(T * 1.3f + k * 2.1f), r = R[k] + 4 * sinf(T * 2.1f + k);
                if (burst && (_formHash(fr * 3 + k) & 3) == 0) continue;     // blinks out mid-glitch
                p.mag(p.px(a, r), p.py(a, r), 3.2f, 2.4f, -120, 125, GREEN);
            }
        }
        _formGlitch(p, true);
        break; }
    case F_KITSUNE: // fox ears and foxfire
        for (int side = -1; side <= 1; side += 2) {
            p.tri(p.px(-90 + side * 18, 57), p.py(-90 + side * 18, 57), cx + side * 40 * s, cy - 100 * s,
                  p.px(-90 + side * 56, 57), p.py(-90 + side * 56, 57), accent);
            p.tri(p.px(-90 + side * 31, 62), p.py(-90 + side * 31, 62), cx + side * 38 * s, cy - 80 * s,
                  p.px(-90 + side * 44, 62), p.py(-90 + side * 44, 62), PANEL);
        }
        {
            const float A[] = { 205, -25, 155 }, R[] = { 78, 80, 84 };
            for (int k = 0; k < 3; k++) {
                float a = A[k] + 6 * sinf(T * 1.3f + k * 2.1f), r = R[k] + 4 * sinf(T * 2.1f + k);
                p.mag(p.px(a, r), p.py(a, r), 3.2f, 2.4f, -120, 125, CYAN);
            }
        }
        break;
    case F_ONI: // two soft horns
        for (int side = -1; side <= 1; side += 2) {
            const int N = 25;
            float o[N * 2], in[N * 2];
            float bx = p.px(-90 + side * 38, 56), by = p.py(-90 + side * 38, 56);
            for (int k = 0; k < N; k++) {
                float u = k / (float)(N - 1), w = 19 * powf(1 - u, 0.8f);
                float x = bx + side * (12 * u + 14 * u * u) * s, y = by - (42 * u - 8 * u * u) * s;
                float ang = atan2f(-(34 - 12 * u), side * (10 + 24 * u));
                float nx = -sinf(ang), ny = cosf(ang);
                o[k*2] = x + nx * w * s / 2; o[k*2+1] = y + ny * w * s / 2;
                in[k*2] = x - nx * w * s / 2; in[k*2+1] = y - ny * w * s / 2;
            }
            p.band(o, in, N, BONES);
        }
        break;
    case F_OMAMORI: { // a charm hanging on a cord
        float ax = p.px(40, 58), ay = p.py(40, 58), x = cx + (62 + 5 * sinf(T * 1.8f)) * s, y = cy + 62 * s;   // swings on its cord
        p.line(ax, ay, x, y - 12 * s, accent);
        p.disc(x, y - 12 * s, 2.2f * f, accent);
        float bag[] = { x - 12 * s, y - 8 * s, x + 12 * s, y - 8 * s, x + 14 * s, y + 28 * s, x - 14 * s, y + 28 * s };
        p.poly(bag, 4, MAGENTA);
        p.rect(x - 12 * s, y, 24 * s + 1, 2 * f, accent);
        break; }
    case F_KABUTO: // a gold crescent helmet crest
        for (int k = 0; k < 60; k++) {
            float t = (200 + 140 * k / 59.0f) * (float)DEG_TO_RAD;
            float x = cx + 46 * s * cosf(t), y = cy - 40 * s + 46 * 0.55f * s * sinf(t);
            float w = 3.4f * sinf((float)M_PI * k / 59) + 0.6f;
            p.disc(x, y, (w / 2 + 0.3f) * f, GOLD);
        }
        p.disc(cx, cy - 60 * s, 2.2f * f, GOLD);
        break;
    case F_MEKURA: { // a blindfold, sealed against the cameras
        float y = cy - 18 * s;
        p.rect(cx - 58 * s, y, 116 * s, 9 * s, BONES);
        float t1[] = { cx + 50 * s, y, cx + 78 * s, y + 18 * s, cx + 72 * s, y + 22 * s, cx + 48 * s, y + 8 * s };
        float t2[] = { cx + 50 * s, y + 4 * f, cx + 70 * s, y + 32 * s, cx + 64 * s, y + 34 * s, cx + 46 * s, y + 8 * s };
        p.poly(t1, 4, BONES); p.poly(t2, 4, BONES);
        break; }
    case F_SUZU: { // a bell hanging below
        float sw = 6 * sinf(T * 3.2f);          // the bell swings
        p.line(cx, cy + 58 * s, cx + sw * 0.6f * s, cy + 72 * s, MAGENTA);
        float bx = cx + sw * s, by = cy + 80 * s;
        p.disc(bx, by, 6 * f, GOLD);
        p.rect(bx - 6 * f, by, 13 * f, 1, BROWN);
        p.rect(bx - 1 * f, by + 1, 2 * f, 4 * f, BROWN);
        p.disc(bx, by - 1 * f, 1 * f, WHITE);
        break; }
    case F_HOSHI: { // a glowing pearl floating beside the Mitama
        float x = p.px(-48, 84), y = p.py(-48, 84) + 3 * f * sinf(T * 1.6f), r = 4 * f;
        p.c.drawCircle(lroundf(x), lroundf(y), lroundf(r + 2.2f * f), CYAN);
        p.disc(x, y, r, WHITE);
        p.disc(x - r * 0.35f, y - r * 0.35f, max(1.0f, r * 0.35f), 0xBF7F);   // highlight
        if (fmodf(T, 1.4f) < 0.9f) p.disc(x - 9 * f, y + 6 * f, 1 * f, CYAN);     // twinkles, out of step
        if (fmodf(T + 0.6f, 1.1f) < 0.6f) p.disc(x + 7 * f, y - 8 * f, 1 * f, WHITE);
        break; }
    case F_YUREI: // the white forehead triangle of a ghost
        p.tri(cx - 10 * s, cy - 56 * s, cx + 10 * s, cy - 56 * s, cx, cy - 74 * s, WHITE);
        break;
    case F_KEKKAI: { // a barrier hexagon
        float hx[6], hy[6];
        float rot = fmodf(T * 8, 60);            // the barrier turns slowly
        for (int k = 0; k < 6; k++) { hx[k] = p.px(30 + 60 * k + rot, 80); hy[k] = p.py(30 + 60 * k + rot, 80); }
        for (int k = 0; k < 6; k++) p.line(hx[k], hy[k], hx[(k + 1) % 6], hy[(k + 1) % 6], CYAN);
        int lit = (int)(T * 4) % 6;               // and a light runs round its corners
        for (int k = 0; k < 6; k++) p.disc(hx[k], hy[k], (k == lit ? 2.2f : 1.4f) * f, k == lit ? WHITE : CYAN);
        break; }
    case F_CHOCHIN: { // a paper lantern on a pole
        p.line(cx - 70 * s, cy + 70 * s, cx - 70 * s, cy - 60 * s, BROWN, 2 * f);
        p.line(cx - 70 * s, cy - 60 * s, cx - 46 * s, cy - 82 * s, BROWN, 2 * f);
        float lx = cx - 46 * s + 2 * f * sinf(T * 1.5f), ly = cy - 60 * s;
        p.line(lx, ly - 22 * s, lx, ly - 14 * s, BROWN);
        float lan[] = { lx - 8 * s, ly - 14 * s, lx + 8 * s, ly - 14 * s, lx + 12 * s, ly,
                        lx + 8 * s, ly + 14 * s, lx - 8 * s, ly + 14 * s, lx - 12 * s, ly };
        p.poly(lan, 6, BONES);
        p.rect(lx - 8 * s, ly - 15 * s, 16 * s, 2 * f, GOLD);
        p.rect(lx - 8 * s, ly + 13 * s, 16 * s, 2 * f, GOLD);
        if (sinf(T * 11) + sinf(T * 17) > -1.2f) p.rect(lx - 11 * s, ly, 22 * s, 1, PINK);   // candle flicker
        break; }
    case F_KOTODAMA: { // words floating off
        float x = cx + 56 * s, y = cy - 62 * s;
        const float W[] = { 26, 18, 22 };
        for (int k = 0; k < 3; k++) {             // words rise and fade, one after another
            float u = fmodf(T * 0.35f + k / 3.0f, 1.0f);
            float wy = y + (24 - 30 * u) * s;
            p.rect(x + 3 * sinf(T + k) * s, wy, W[k] * s * (u < 0.8f ? 1 : (1 - u) * 5), 2 * f, u < 0.85f ? MAGENTA : PURPLE);
        }
        p.disc(x - 6 * s, y + 20 * s + 1, 1.2f * f, MAGENTA);
        p.disc(x - 12 * s, y + 28 * s, 0.9f * f, MAGENTA);
        break; }
    case F_GILDED: // sparkles around the gold Mitama
        for (int k = 0; k < 3; k++) {
            const float A[] = { -60, 150, 20 }, R[] = { 80, 84, 90 };
            float x = p.px(A[k], R[k]), y = p.py(A[k], R[k]);
            float tw = 0.5f + 0.5f * sinf(T * 3 + k * 2.1f);     // each sparkle swells and shrinks
            int arm = lroundf((1 + 2 * tw) * f);
            p.rect(x - arm, y, 2 * arm + 1, 1, tw > 0.7f ? WHITE : GOLD);
            p.rect(x, y - arm, 1, 2 * arm + 1, tw > 0.7f ? WHITE : GOLD);
        }
        break;
    default: break;
    }
}

// The Mitama in a form: the Soul (aura still shows the mood) plus the
// form's decorations. Gilded and Yurei recolour the Soul itself.
inline void soulForm(LGFX_Sprite& c, int cx, int cy, int size, uint16_t accent, uint16_t aura,
                     float spin, Form form) {
    uint16_t bones = BONES;
    if (form == F_GILDED) { accent = _FormCol::GOLD; bones = _FormCol::GOLD; }
    if (form == F_YUREI)  { accent = _FormCol::PALE; bones = _FormCol::GHOST;
                            cy += max(1, size / 30) + lroundf(size / 46.0f * sinf(_formT() * 1.4f)); }   // drifts up and down
    if (form == F_KYUBI)  { accent = CYAN; bones = MAGENTA; }
    _FormPen p{ c, (float)cx, (float)cy, size / 200.0f, size / 92.0f };
    _formBehind(p, form);
    soul(c, cx, cy, size, accent, aura, spin, bones);
    _formFront(p, form, accent);
}

} // namespace MUI
