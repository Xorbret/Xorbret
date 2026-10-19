#pragma once
#include <M5Cardputer.h>
#include <math.h>
#include "../config.h"

// MitamaOS sealed crest (locked logo, outline §15 "Logo lockup"): a solid
// open-comma magatama inside a dashed cyber-yellow seal ring, with a dim
// bones-red ring behind it. Geometry is ported 1:1 from docs/logo.html's
// magatama()/crest() in a 200x200 design space, then scaled - all shapes,
// no bitmap, so there's no RGB565 byte-order trap.
namespace MitamaLogo {

    static const uint16_t BONES = 0xE0A5; // bones red

    // Dashed ring: dashes of `on` units then `off` units along the circle.
    inline void _dashRing(LGFX_Sprite& c, int cx, int cy, float r, float w,
                          float on, float off, uint16_t col, float startDeg = 0) {
        float circ = 2.0f * M_PI * r;
        int n = (int)(circ / (on + off) + 0.5f);
        if (n < 1) n = 1;
        float step = 360.0f / n, dash = step * on / (on + off);
        int r0 = (int)(r - w / 2), r1 = (int)(r + w / 2 + 0.5f);
        if (r1 <= r0) r1 = r0 + 1;
        for (int i = 0; i < n; i++)
            c.fillArc(cx, cy, r0, r1, startDeg + i * step, startDeg + i * step + dash, col);
    }

    // Tapered comma: a band around (cx,cy) of mid-radius Rm from angle a0
    // to a1 (degrees, screen coords), half-thickness tH shrinking by
    // (1-u)^tp, plus a round head of radius tH at a0.
    inline void _magatama(LGFX_Sprite& c, float cx, float cy, float Rm, float tH,
                          float a0, float a1, float tp, uint16_t col) {
        const int N = 48;
        float px0 = 0, py0 = 0, qx0 = 0, qy0 = 0;
        for (int i = 0; i <= N; i++) {
            float u = (float)i / N;
            float th = (a0 + (a1 - a0) * u) * (float)DEG_TO_RAD;
            float t = tH * powf(1.0f - u, tp);
            float ct = cosf(th), st = sinf(th);
            float px = cx + (Rm + t) * ct, py = cy + (Rm + t) * st;
            float qx = cx + (Rm - t) * ct, qy = cy + (Rm - t) * st;
            if (i > 0) {
                c.fillTriangle(px0, py0, px, py, qx, qy, col);
                c.fillTriangle(px0, py0, qx, qy, qx0, qy0, col);
            }
            px0 = px; py0 = py; qx0 = qx; qy0 = qy;
        }
        float hx = cx + Rm * cosf(a0 * (float)DEG_TO_RAD);
        float hy = cy + Rm * sinf(a0 * (float)DEG_TO_RAD);
        c.fillCircle(hx, hy, tH, col);
    }

    // Sealed crest centred on (cx,cy), `size` px across the 200-unit box.
    inline void drawCrest(LGFX_Sprite& c, int cx, int cy, int size) {
        float s = size / 200.0f;
        auto X = [&](float v) { return cx + (v - 100) * s; };
        auto Y = [&](float v) { return cy + (v - 100) * s; };
        _dashRing(c, cx, cy, 86 * s, max(2.0f, 5 * s), 7, 11, COL_WARN);
        _dashRing(c, cx, cy, 74 * s, max(1.0f, 2.5f * s), 4, 8, BONES);
        _magatama(c, X(100), Y(106), 34 * s, 24 * s, -60, 185, 0.82f, COL_WARN);
    }

}
