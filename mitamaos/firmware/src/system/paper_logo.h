#pragma once
#include <M5Cardputer.h>
#include "../config.h"

// PaperOS notebook mark, drawn entirely out of fillRect() calls instead of
// a converted photo. The embedded-raster version of this logo came out
// corrupted (reddish, torn up) on real hardware - a classic symptom of the
// RGB565 byte order not matching what the panel driver expected - so this
// sidesteps that class of bug completely: there's no pixel data to get the
// order wrong on, just shapes.
//
// Meant to sit directly on a dark/navy background (no card behind it) -
// everything it draws is either the white page or navy ink, nothing fills
// a background box first.
namespace PaperLogo {

    // Notebook icon, top-left of its bounding box at (x,y). `scale` sizes
    // it up in whole pixels (2 = every logical pixel becomes a 2x2 block).
    inline void drawMark(LGFX_Sprite& canvas, int x, int y, int scale = 2) {
        auto px = [&](int dx, int dy, int w, int h, uint16_t col) {
            canvas.fillRect(x + dx * scale, y + dy * scale, w * scale, h * scale, col);
        };

        const uint16_t PAGE = 0xFFFF;
        const uint16_t INK  = 0x0010;
        const uint16_t LINE = 0x8410;

        // spiral binding rings down the left edge
        for (int i = 0; i < 7; i++) px(0, 8 + i * 4, 3, 2, INK);

        // the page itself, with a 1px "spine" border
        px(4, 6, 15, 26, PAGE);
        px(4, 6, 15, 1, INK);   // top edge
        px(4, 31, 15, 1, INK);  // bottom edge
        px(18, 6, 1, 26, INK);  // right edge

        // ruled lines on the page
        for (int i = 0; i < 5; i++) px(7, 12 + i * 4, 9, 1, LINE);

        // paperclip hooked over the top edge of the page
        px(13, 1, 2, 10, INK);
        px(13, 1, 6, 2, INK);
        px(17, 1, 2, 8, INK);
        px(13, 7, 6, 2, INK);
    }

    inline int markW(int scale = 2) { return 19 * scale; }
    inline int markH(int scale = 2) { return 34 * scale; }

    // Full lockup: mark + "PaperOS" / "Operating System" wordmark, drawn
    // with the built-in font (so it's code, not an image, all the way
    // down). (x,y) is the top-left of the notebook mark itself.
    inline void drawLockup(LGFX_Sprite& canvas, int x, int y, int scale = 2) {
        drawMark(canvas, x, y, scale);

        int tx = x + markW(scale) + 8;
        int ty = y + markH(scale) / 2 - 14;

        canvas.setTextSize(2);
        canvas.setTextColor(0xFFFF); // transparent bg - sits on whatever's already drawn
        canvas.setCursor(tx, ty);
        canvas.print(OS_NAME);

        canvas.setTextSize(1);
        canvas.setTextColor(0xC618);
        canvas.setCursor(tx, ty + 20);
        canvas.print("Operating System");
    }

    inline int lockupW(int scale = 2) { return markW(scale) + 8 + 17 * 6; } // widest line is the subtitle
}
