#pragma once
#include <M5Cardputer.h>
#include <SD.h>
#include <math.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../config.h"

// Pixel art / image editor. Two UI modes: Drawing and Tool Panel.
// Discrete actions (tool select, menu confirm, Tab) use edge-triggered
// "just pressed" checks so a single physical key press can't register
// twice or leak into the wrong mode. Cursor movement (;/./,//) stays
// level-triggered on purpose so holding a direction repeats.
// Save/Open is a real folder browser: any folder, any filename.

enum PaintTool {
    PT_PEN = 0, PT_LINE, PT_RECT, PT_FILLRECT, PT_ELLIPSE, PT_CIRCLE,
    PT_TRIANGLE, PT_FILL, PT_ERASER, PT_EYEDROP, PT_OUTLINE, PT_TEXT,
    PT_COUNT
};
static const char* TOOL_NAMES[] = {
    "Pen", "Line", "Rect", "FillRect", "Ellipse", "Circle",
    "Triangle", "Fill", "Eraser", "EyeDrop", "Outline", "Text"
};
// p l r h e o t g z y k w — 'c' is intentionally free: direct color-cycle
// hotkey in drawing mode (matches the "c = change color" behavior the user
// already expected from before), not a tool shortcut.
static const char TOOL_KEYS[] = { 'p','l','r','h','e','o','t','g','z','y','k','w' };

#define PAL_SIZE      24
#define PAL_CUSTOM    24   // 25th slot: whatever the eyedropper last picked
#define PAL_TOTAL     25
#define PANEL_W       26
#define PAL_STRIP_H   10
#define CANVAS_W      240
#define CANVAS_H      135

// Real visible drawing viewport — must match the math in _redraw()
// (canY = CONTENT_Y+2, canH = SCREEN_H - canY - FOOTER_H - PAL_STRIP_H - 2).
// CANVAS_W/CANVAS_H above are the _dc sprite's backing-buffer size (upper
// bound for storage); these two are the actual on-screen limit and are
// what every "how big can the image legally be" decision uses instead.
#define VIEW_CAN_Y    (CONTENT_Y + 2)
#define VIEW_W        SCREEN_W
#define VIEW_H        (SCREEN_H - VIEW_CAN_Y - FOOTER_H - PAL_STRIP_H - 2)

enum PaintUiMode { UI_DRAWING = 0, UI_TOOL_PANEL };

class Paint : public App {
public:
    Paint(LGFX_Sprite& canvas) : _canvas(canvas), _dc(&M5Cardputer.Display) {}

    const char* getName() const override { return "Paint"; }
    const char* getIcon() const override { return "A"; }
    const char* getDesc() const override { return "Pixel art editor"; }

    void onStart() override {
        _cx = _cy = 0;
        _colIdx = 1;
        _usingCustom = false;
        _customColor = TFT_WHITE;
        _brush = 1;
        _tool = PT_PEN;
        _dirty = false;
        _uiMode = UI_DRAWING;
        _shapeFilled = false;
        _showGrid = false;
        _p1x = _p1y = -1;
        _p2x = _p2y = -1;
        _zoom = 1;
        _undoBuf = nullptr;
        _toolPanelSel = 0;
        _imgW = _imgH = 0;
        memset(_prevKey, 0, sizeof(_prevKey));

        _dc.setColorDepth(16);
        _dc.createSprite(CANVAS_W, CANVAS_H);
        _dc.fillSprite(TFT_BLACK);

        if (!_startMenu()) {
            _dc.deleteSprite();
            AppManager::get().goHome();
            return;
        }

        _cx = _imgW / 2;
        _cy = _imgH / 2;
        _allocUndo();
        _saveUndo();
        _redraw();
    }

    void onStop() override {
        _dc.deleteSprite();
        _freeUndo();
    }

    void update() override {
        M5Cardputer.update();
        auto ks = M5Cardputer.Keyboard.keysState();

        // ── Edge-detect every discrete key ONCE, before any branching ──
        // ENT is additionally gated by _entArmed: whenever Tab toggles the
        // UI mode, or the panel just consumed an ENT to confirm a tool,
        // ENT is disarmed until ks.enter is actually observed false at
        // least once. Without this, the very press that opens/confirms
        // the panel can still read as "held" for a frame or two afterward
        // (ordinary key-travel time, or Tab/ENT sitting close enough on
        // this keyboard to overlap briefly) and re-trigger as a fresh ENT
        // once back in Drawing mode - which is exactly what made picking a
        // tool also fire that tool once on the canvas (Fill flooding the
        // whole image right after being selected, etc).
        if (!ks.enter) _entArmed = true;
        bool ent  = _edge(ks.enter, _pEnter) && _entArmed;
        bool tab  = _edge(ks.tab,   _pTab);
        bool back = _edge(ks.del,   _pDel);
        bool eP[PT_COUNT];
        for (int i = 0; i < PT_COUNT; i++) eP[i] = _edgeKey(TOOL_KEYS[i]);
        bool eMenu  = _edgeKey('m');
        bool eGrid  = _edgeKey('n');
        bool eUndo  = _edgeKey('u');
        bool eFill  = _edgeKey('i');   // toggle filled/outline for shapes
        bool eBrushUp   = _edgeKey(']');
        bool eBrushDown = _edgeKey('[');
        // Don't use || directly on two _edgeKey() calls: short-circuit
        // evaluation would skip the second call entirely once the first
        // returns true, leaving that key's prev-state stale until next
        // frame. Evaluate both unconditionally, then OR the results.
        bool eZoomPlus1 = _edgeKey('+');
        bool eZoomPlus2 = _edgeKey('=');
        bool eZoomUp    = eZoomPlus1 || eZoomPlus2;
        bool eZoomDown  = _edgeKey('-');
        bool eColorCycle = _edgeKey('c');  // direct "change color" hotkey while drawing

        if (tab) {
            _uiMode = (_uiMode == UI_DRAWING) ? UI_TOOL_PANEL : UI_DRAWING;
            if (_uiMode == UI_TOOL_PANEL) _toolPanelSel = (int)_tool;
            _pEnter = ks.enter;
            _entArmed = false;
            _redraw();
            return;
        }

        if (_uiMode == UI_TOOL_PANEL) {
            if (_updateToolPanel(ent, back)) _entArmed = false;  // this ENT confirmed a tool - it must not also act on the canvas
            return;
        }

        // ── Drawing mode ──
        if (back) {
            if (!_dirty || _confirmDialog("Exit? Unsaved!")) { AppManager::get().goHome(); return; }
            _redraw();
            return;
        }
        if (eMenu) { _quickMenu(); _redraw(); return; }
        if (eGrid) { _showGrid = !_showGrid; }
        if (eUndo) { _restoreUndo(); }
        if (eFill && (_tool==PT_RECT || _tool==PT_ELLIPSE || _tool==PT_TRIANGLE)) _shapeFilled = !_shapeFilled;
        if (eBrushUp)   _brush = min(_brush + 1, 8);
        if (eBrushDown) _brush = max(_brush - 1, 1);
        if (eZoomUp)    _zoom = min(_zoom + 1, 4);
        if (eZoomDown)  _zoom = max(_zoom - 1, 1);
        if (eColorCycle) { _usingCustom = false; _colIdx = (_colIdx + 1) % PAL_SIZE; }
        bool anyToolKey = false;
        for (int i = 0; i < PT_COUNT; i++) if (eP[i]) { _setTool((PaintTool)i); anyToolKey = true; }

        int step = ks.shift ? 4 : 1;
        bool moved = false;
        if (_repeatKey(';', 0)) { _cy = max(0, _cy - step); moved = true; }
        if (_repeatKey('.', 1)) { _cy = min(_imgH - 1, _cy + step); moved = true; }
        if (_repeatKey(',', 2)) { _cx = max(0, _cx - step); moved = true; }
        if (_repeatKey('/', 3)) { _cx = min(_imgW - 1, _cx + step); moved = true; }

        // Pen/Eraser: exactly one dab per ENT press. Movement only ever
        // moves the cursor; it never paints on its own.
        if (ent) {
            if (_tool == PT_PEN || _tool == PT_ERASER) {
                _saveUndo();
                _dc.fillRect(_cx - _brush/2, _cy - _brush/2, _brush, _brush,
                              _tool == PT_ERASER ? (uint16_t)TFT_BLACK : _curColor());
            } else {
                _applyTool();
            }
            _dirty = true;
        }

        if (moved || ent || anyToolKey || tab || eGrid || eUndo || eFill || eBrushUp || eBrushDown || eZoomUp || eZoomDown || eColorCycle)
            _redraw();
        else
            delay(15);
    }

private:
    LGFX_Sprite& _canvas;
    LGFX_Sprite  _dc;

    int _cx, _cy, _imgW, _imgH;
    int _colIdx, _brush, _zoom;
    bool _usingCustom;
    uint16_t _customColor;
    PaintTool _tool;
    bool _dirty, _shapeFilled, _showGrid;
    int _p1x, _p1y, _p2x, _p2y;
    PaintUiMode _uiMode;
    int _toolPanelSel;
    uint16_t* _undoBuf = nullptr;

    bool _prevKey[128];

    // Non-blocking "press once immediately, then auto-repeat after holding"
    // for movement — same shape as AdvanceOS's
    // Key_Press_1_Click_And_After_Few_MS_RepeatClick, just without a
    // blocking delay() (which could stall the whole frame and made other
    // input feel laggy/inconsistent).
    struct RepeatState { bool down = false; uint32_t pressedAt = 0, lastRepeat = 0; };
    RepeatState _rpt[4];  // 0=up 1=down 2=left 3=right
    bool _repeatKey(char c, int slot, uint32_t initialDelay = 260, uint32_t repeatMs = 55) {
        RepeatState& s = _rpt[slot];
        bool now = M5Cardputer.Keyboard.isKeyPressed(c);
        if (!now) { s.down = false; return false; }
        if (!s.down) { s.down = true; s.pressedAt = s.lastRepeat = millis(); return true; }
        uint32_t t = millis();
        if (t - s.pressedAt >= initialDelay && t - s.lastRepeat >= repeatMs) { s.lastRepeat = t; return true; }
        return false;
    }
    bool _pEnter = false, _pTab = false, _pDel = false;
    bool _entArmed = true;  // false right after a Tab/panel-confirm ENT, until ks.enter is seen false again

    static constexpr uint16_t _palette[PAL_SIZE] = {
        0x0000, 0xFFFF, 0x4208, 0x9CF3,
        0x8000, 0xF800, 0xFC10, 0xFD20,
        0xFFE0, 0x0320, 0x07E0, 0xB7E0,
        0x0410, 0x07FF, 0xAFFF, 0x0010,
        0x001F, 0x3D9F, 0x8010, 0xF81F,
        0x8400, 0xFDA0, 0x7BEF, 0x3186,
    };

    // ── Edge detection helpers ─────────────────────────────────────────
    bool _edge(bool now, bool& prev) { bool e = now && !prev; prev = now; return e; }
    bool _edgeKey(char c) {
        bool now = M5Cardputer.Keyboard.isKeyPressed(c);
        bool e = now && !_prevKey[(uint8_t)c];
        _prevKey[(uint8_t)c] = now;
        return e;
    }

    uint16_t _curColor() { return _usingCustom ? _customColor : _palette[_colIdx]; }

    void _setTool(PaintTool t) { _tool = t; _resetShape(); }
    void _resetShape() { _p1x = _p1y = _p2x = _p2y = -1; }

    // ── Undo (single level) ─────────────────────────────────────────────
    void _allocUndo() { _undoBuf = (uint16_t*)malloc(CANVAS_W * CANVAS_H * 2); }
    void _freeUndo()  { if (_undoBuf) { free(_undoBuf); _undoBuf = nullptr; } }
    void _saveUndo() {
        if (!_undoBuf) return;
        for (int y = 0; y < _imgH; y++)
            for (int x = 0; x < _imgW; x++)
                _undoBuf[y * CANVAS_W + x] = _dc.readPixel(x, y);
    }
    void _restoreUndo() {
        if (!_undoBuf) return;
        for (int y = 0; y < _imgH; y++)
            for (int x = 0; x < _imgW; x++)
                _dc.drawPixel(x, y, _undoBuf[y * CANVAS_W + x]);
    }

    // ── Drawing primitives ──────────────────────────────────────────────
    void _applyTool() {
        uint16_t col = _curColor();
        switch (_tool) {
        case PT_PEN:
        case PT_ERASER:
            _dc.fillRect(_cx - _brush / 2, _cy - _brush / 2, _brush, _brush,
                          _tool == PT_ERASER ? (uint16_t)TFT_BLACK : col);
            break;
        case PT_FILL:
            _saveUndo();
            _floodFill(_cx, _cy, _dc.readPixel(_cx, _cy), col);
            break;
        case PT_EYEDROP:
            // Stores whatever color is under the cursor into a dedicated
            // custom slot, so it works on any color, not just palette matches.
            _customColor = _dc.readPixel(_cx, _cy);
            _usingCustom = true;
            break;
        case PT_LINE:
            if (_p1x < 0) { _p1x = _cx; _p1y = _cy; }
            else { _saveUndo(); _drawLine(_p1x, _p1y, _cx, _cy, col, _brush); _resetShape(); }
            break;
        case PT_RECT:
            if (_p1x < 0) { _p1x = _cx; _p1y = _cy; }
            else {
                _saveUndo();
                int x0 = min(_p1x, _cx), y0 = min(_p1y, _cy);
                int w = abs(_cx - _p1x) + 1, h = abs(_cy - _p1y) + 1;
                if (_shapeFilled) _dc.fillRect(x0, y0, w, h, col);
                else _dc.drawRect(x0, y0, w, h, col);
                _resetShape();
            }
            break;
        case PT_FILLRECT:
            if (_p1x < 0) { _p1x = _cx; _p1y = _cy; }
            else {
                _saveUndo();
                _dc.fillRect(min(_p1x, _cx), min(_p1y, _cy),
                             abs(_cx - _p1x) + 1, abs(_cy - _p1y) + 1, col);
                _resetShape();
            }
            break;
        case PT_ELLIPSE:
            if (_p1x < 0) { _p1x = _cx; _p1y = _cy; }
            else {
                _saveUndo();
                int rx = abs(_cx - _p1x), ry = abs(_cy - _p1y);
                if (_shapeFilled) _dc.fillEllipse(_p1x, _p1y, rx, ry, col);
                else _dc.drawEllipse(_p1x, _p1y, rx, ry, col);
                _resetShape();
            }
            break;
        case PT_CIRCLE:
            if (_p1x < 0) { _p1x = _cx; _p1y = _cy; }
            else {
                _saveUndo();
                int r = (int)round(sqrt((double)(_cx-_p1x)*(_cx-_p1x) + (double)(_cy-_p1y)*(_cy-_p1y)));
                if (_shapeFilled) _dc.fillCircle(_p1x, _p1y, r, col);
                else _dc.drawCircle(_p1x, _p1y, r, col);
                _resetShape();
            }
            break;
        case PT_TRIANGLE:
            if (_p1x < 0) { _p1x = _cx; _p1y = _cy; }
            else if (_p2x < 0) { _p2x = _cx; _p2y = _cy; }
            else {
                _saveUndo();
                if (_shapeFilled) _fillTriangle(_p1x, _p1y, _p2x, _p2y, _cx, _cy, col);
                else {
                    _drawLine(_p1x, _p1y, _p2x, _p2y, col, 1);
                    _drawLine(_p2x, _p2y, _cx, _cy, col, 1);
                    _drawLine(_cx, _cy, _p1x, _p1y, col, 1);
                }
                _resetShape();
            }
            break;
        case PT_OUTLINE:
            _saveUndo();
            _autoOutline(col);
            break;
        case PT_TEXT: {
            String ch = OsUI::inputText(_canvas, "Text:");
            if (ch.length() > 0) {
                _saveUndo();
                _dc.setTextColor(col, TFT_BLACK);
                _dc.setTextSize(1);
                _dc.setCursor(_cx, _cy);
                _dc.print(ch);
            }
            break;
        }
        default: break;
        }
    }

    void _drawLine(int x0, int y0, int x1, int y1, uint16_t col, int thick) {
        int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, e2;
        while (true) {
            _dc.fillRect(x0 - thick / 2, y0 - thick / 2, thick, thick, col);
            if (x0 == x1 && y0 == y1) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void _fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t col) {
        if (y0 > y1) { int t; t=x0;x0=x1;x1=t; t=y0;y0=y1;y1=t; }
        if (y0 > y2) { int t; t=x0;x0=x2;x2=t; t=y0;y0=y2;y2=t; }
        if (y1 > y2) { int t; t=x1;x1=x2;x2=t; t=y1;y1=y2;y2=t; }
        int total = y2 - y0 + 1;
        for (int y = y0; y <= y2; y++) {
            int seg, xL, xR;
            if (y < y1) {
                seg = max(1, y1 - y0);
                xL = x0 + (x1 - x0) * (y - y0) / seg;
                xR = x0 + (x2 - x0) * (y - y0) / max(1, y2 - y0);
            } else {
                seg = max(1, y2 - y1);
                xL = x1 + (x2 - x1) * (y - y1) / seg;
                xR = x0 + (x2 - x0) * (y - y0) / max(1, total);
            }
            if (xL > xR) { int t = xL; xL = xR; xR = t; }
            _dc.drawFastHLine(xL, y, xR - xL + 1, col);
        }
    }

    void _floodFill(int x, int y, uint16_t target, uint16_t fill) {
        if (target == fill) return;
        if (_dc.readPixel(x, y) != target) return;

        // Scanline fill: each stack entry is a whole horizontal span, not a
        // single pixel, so the stack stays shallow even on a fill that
        // covers most of the canvas.
        struct Span { int16_t xL, xR, y, dir; };  // dir: -1 up, +1 down, 0 both
        const int STACK_SZ = 512;
        Span* stk = new Span[STACK_SZ];
        if (!stk) return;
        int sp = 0;
        stk[sp++] = {(int16_t)x, (int16_t)x, (int16_t)y, 0};

        while (sp > 0) {
            Span s = stk[--sp];
            int xL = s.xL, xR = s.xR, yy = s.y;
            if (yy < 0 || yy >= _imgH) continue;
            // extend left/right on this row from the seed span
            while (xL > 0 && _dc.readPixel(xL - 1, yy) == target) xL--;
            while (xR < _imgW - 1 && _dc.readPixel(xR + 1, yy) == target) xR++;
            _dc.drawFastHLine(xL, yy, xR - xL + 1, fill);

            // scan the row above and below for new spans to push
            for (int dir = -1; dir <= 1; dir += 2) {
                int ny = yy + dir;
                if (ny < 0 || ny >= _imgH) continue;
                int nx = xL;
                while (nx <= xR) {
                    if (_dc.readPixel(nx, ny) == target) {
                        int spanStart = nx;
                        while (nx <= xR && _dc.readPixel(nx, ny) == target) nx++;
                        if (sp < STACK_SZ) stk[sp++] = {(int16_t)spanStart, (int16_t)(nx - 1), (int16_t)ny, (int16_t)dir};
                    }
                    nx++;
                }
            }
        }
        delete[] stk;
    }

    // Simple 1px outline around any non-black region (whole canvas, one shot).
    void _autoOutline(uint16_t col) {
        for (int y = 0; y < _imgH; y++) {
            for (int x = 0; x < _imgW; x++) {
                if (_dc.readPixel(x, y) != (uint16_t)TFT_BLACK) continue;
                bool edge = false;
                const int dx4[4] = {1,-1,0,0}, dy4[4] = {0,0,1,-1};
                for (int k = 0; k < 4; k++) {
                    int nx = x + dx4[k], ny = y + dy4[k];
                    if (nx < 0 || ny < 0 || nx >= _imgW || ny >= _imgH) continue;
                    if (_dc.readPixel(nx, ny) != (uint16_t)TFT_BLACK) { edge = true; break; }
                }
                if (edge) _dc.drawPixel(x, y, col);
            }
        }
    }

    // ── Start menu: canvas size or Open ─────────────────────────────────
    bool _startMenu() {
        static char fullLabel[32];
        snprintf(fullLabel, sizeof(fullLabel), "Full screen (%dx%d)", VIEW_W, VIEW_H);
        const char* items[] = { "16x16", "32x32", "64x64", fullLabel, "Open...", "Cancel" };
        const int sizes[] = { 16, 32, 64, 0 };
        int sel = 0, n = 6;
        bool pEnt = false;
        while (true) {
            M5Cardputer.update();
            auto ks = M5Cardputer.Keyboard.keysState();
            _canvas.fillSprite(COL_BG);
            OsUI::drawHeader(_canvas, "New / Open Image", COL_HEADER);
            for (int i = 0; i < n; i++) {
                int y = CONTENT_Y + 4 + i * 16;
                bool s = (i == sel);
                _canvas.fillRect(10, y, SCREEN_W - 20, 15, s ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG2);
                _canvas.setTextColor(s ? WHITE : (uint16_t)COL_TEXT, s ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG2);
                _canvas.setCursor(16, y + 3);
                _canvas.print(items[i]);
            }
            OsUI::drawFooter(_canvas, ";.=nav ENT=select");
            _canvas.pushSprite(0, 0);

            if (M5Cardputer.Keyboard.isKeyPressed(';')) { sel = (sel + n - 1) % n; delay(140); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { sel = (sel + 1) % n; delay(140); }
            bool ent = ks.enter && !pEnt; pEnt = ks.enter;
            if (ent) {
                OsUI::waitRelease();
                if (sel == 4) {  // Open...
                    String path = _browse(true, "/");
                    if (path == "") continue;  // cancelled, back to this menu
                    if (!_loadBmp(path)) { _showMsg("Load failed", COL_ERR); delay(900); continue; }
                    return true;
                }
                if (sel == 5) return false;  // Cancel
                _imgW = _imgH = (sizes[sel] == 0) ? 0 : sizes[sel];
                if (sizes[sel] == 0) { _imgW = VIEW_W; _imgH = VIEW_H; }  // matches the actual on-screen viewport, no overflow
                return true;
            }
            delay(10);
        }
    }

    // ── Quick menu (Save/Save As/Open/New/Exit) ─────────────────────────
    void _quickMenu() {
        const char* items[] = { "Save", "Save As...", "Open...", "New", "Exit to " OS_NAME };
        int sel = 0, n = 5;
        bool pEnt = false;
        while (true) {
            M5Cardputer.update();
            auto ks = M5Cardputer.Keyboard.keysState();
            _canvas.fillSprite(COL_BG);
            OsUI::drawHeader(_canvas, "Paint Menu", COL_HEADER);
            for (int i = 0; i < n; i++) {
                int y = CONTENT_Y + 4 + i * 16;
                bool s = (i == sel);
                _canvas.fillRect(10, y, SCREEN_W - 20, 15, s ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG2);
                _canvas.setTextColor(s ? WHITE : (uint16_t)COL_TEXT, s ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG2);
                _canvas.setCursor(16, y + 3);
                _canvas.print(items[i]);
            }
            OsUI::drawFooter(_canvas, ";.=nav ENT=select `=cancel");
            _canvas.pushSprite(0, 0);

            if (M5Cardputer.Keyboard.isKeyPressed(';')) { sel = (sel + n - 1) % n; delay(140); }
            if (M5Cardputer.Keyboard.isKeyPressed('.')) { sel = (sel + 1) % n; delay(140); }
            bool ent = ks.enter && !pEnt; pEnt = ks.enter;
            if (M5Cardputer.Keyboard.isKeyPressed('`') || ks.del) { OsUI::waitRelease(); return; }
            if (ent) {
                OsUI::waitRelease();
                switch (sel) {
                    case 0:  // Save (to last path, or ask if none yet)
                        if (_lastPath == "") { String p = _browse(false, "/"); if (p != "") _saveBmp(p); }
                        else _saveBmp(_lastPath);
                        return;
                    case 1: { String p = _browse(false, "/"); if (p != "") _saveBmp(p); return; }
                    case 2: { String p = _browse(true, "/"); if (p != "") _loadBmp(p); return; }
                    case 3:
                        if (!_dirty || _confirmDialog("Discard and start New?")) {
                            _dc.fillSprite(TFT_BLACK);
                            _dirty = false; _lastPath = ""; _resetShape();
                        }
                        return;
                    case 4:
                        if (!_dirty || _confirmDialog("Exit? Unsaved!")) AppManager::get().goHome();
                        return;
                }
            }
            delay(10);
        }
    }

    // ── Real folder browser for Save/Open — any folder, any filename ───
    // Returns full path (Open: existing file; Save: chosen/typed name), or
    // "" if the user cancelled.
    String _browse(bool forOpen, String path) {
        bool pEnt = false;
        int sel = 0, scroll = 0;
        // Path convention: "/" for root, otherwise NO trailing slash (e.g.
        // "/Pictures"). SD.open() with a trailing slash on anything but
        // root returns nothing on this SD backend, so subfolder paths must
        // never carry one.
        if (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
        while (true) {
            String names[80]; bool isDir[80]; int count = 0;
            File dir = SD.open(path.c_str());
            if (dir) {
                File f = dir.openNextFile();
                while (f && count < 80) {
                    String n = String(f.name());
                    int slash = n.lastIndexOf('/');
                    if (slash >= 0) n = n.substring(slash + 1);
                    bool d = f.isDirectory();
                    if (d || !forOpen || n.endsWith(".bmp") || n.endsWith(".BMP")) {
                        names[count] = n; isDir[count] = d; count++;
                    }
                    f = dir.openNextFile();
                }
                dir.close();
            }
            // sort: dirs first, then alpha (tiny bubble sort, lists are small)
            for (int i = 0; i < count; i++)
                for (int j = i+1; j < count; j++) {
                    bool swap = (!isDir[i] && isDir[j]) ||
                                (isDir[i]==isDir[j] && names[i] > names[j]);
                    if (swap) { String tn=names[i]; names[i]=names[j]; names[j]=tn;
                                bool td=isDir[i]; isDir[i]=isDir[j]; isDir[j]=td; }
                }

            bool root = (path == "/" || path == "");
            int extra = forOpen ? (root ? 0 : 1) : (root ? 1 : 2);
            int total = count + extra;
            if (sel >= total) sel = max(0, total - 1);

            while (true) {
                M5Cardputer.update();
                auto ks = M5Cardputer.Keyboard.keysState();
                _canvas.fillSprite(COL_BG);
                OsUI::drawHeader(_canvas, forOpen ? "Open Image" : "Save Image", COL_HEADER);
                _canvas.setTextColor(COL_DIM, COL_BG);
                _canvas.setCursor(4, CONTENT_Y);
                _canvas.print(path.length() > 38 ? ("..." + path.substring(path.length()-35)) : path);

                const int rowH = 14, visible = 5;
                if (sel < scroll) scroll = sel;
                if (sel >= scroll + visible) scroll = sel - visible + 1;

                for (int row = 0; row < visible; row++) {
                    int idx = scroll + row;
                    if (idx >= total) break;
                    int y = CONTENT_Y + 12 + row * rowH;
                    bool s = (idx == sel);
                    String label;
                    int ei = idx;
                    if (!root && ei == 0) { label = ".. (up)"; ei = -1; }
                    else {
                        if (!root) ei--;
                        if (!forOpen && ei == 0) { label = "[Save here as...]"; ei = -1; }
                        else {
                            if (!forOpen) ei--;
                            if (ei >= 0 && ei < count) label = (isDir[ei] ? "[" + names[ei] + "]" : names[ei]);
                        }
                    }
                    _canvas.fillRect(4, y, SCREEN_W - 8, rowH - 1, s ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG);
                    _canvas.setTextColor(s ? WHITE : (uint16_t)COL_TEXT, s ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG);
                    _canvas.setCursor(8, y + 2);
                    _canvas.print(label);
                }
                OsUI::drawFooter(_canvas, ";.=nav ENT=open/enter `=cancel");
                _canvas.pushSprite(0, 0);

                if (M5Cardputer.Keyboard.isKeyPressed(';')) { sel = (sel + total - 1) % total; delay(120); }
                if (M5Cardputer.Keyboard.isKeyPressed('.')) { sel = (sel + 1) % total; delay(120); }
                bool ent = ks.enter && !pEnt; pEnt = ks.enter;
                if (M5Cardputer.Keyboard.isKeyPressed('`') || ks.del) { OsUI::waitRelease(); return ""; }

                if (ent) {
                    OsUI::waitRelease();
                    int ei = sel;
                    if (!root && ei == 0) {  // ".."
                        int slash = path.lastIndexOf('/');
                        path = (slash <= 0) ? "/" : path.substring(0, slash);
                        sel = 0; scroll = 0;
                        goto relist;
                    }
                    if (!root) ei--;
                    if (!forOpen && ei == 0) {  // "[Save here as...]"
                        String name = OsUI::inputText(_canvas, "File name:");
                        if (name == "") continue;
                        if (!name.endsWith(".bmp") && !name.endsWith(".BMP")) name += ".bmp";
                        String full = path + (path.endsWith("/") ? "" : "/") + name;
                        if (SD.exists(full.c_str()) && !_confirmDialog("Overwrite file?")) continue;
                        return full;
                    }
                    if (!forOpen) ei--;
                    if (ei >= 0 && ei < count) {
                        String full = path + (path.endsWith("/") ? "" : "/") + names[ei];
                        if (isDir[ei]) { path = full; sel = 0; scroll = 0; goto relist; }
                        else if (forOpen) return full;
                        else if (_confirmDialog("Overwrite file?")) return full;
                    }
                }
                delay(10);
            }
            relist:;
        }
    }

    // ── BMP I/O ──────────────────────────────────────────────────────────
    void _saveBmp(const String& path) {
        File f = SD.open(path.c_str(), FILE_WRITE);
        if (!f) { _showMsg("SD write error!", COL_ERR); delay(900); return; }

        int w = _imgW, h = _imgH;
        int rowSize = ((w * 3 + 3) & ~3);
        int pixelDataSize = rowSize * h;
        uint8_t hdr[54] = {};
        hdr[0] = 'B'; hdr[1] = 'M';
        *(int32_t*)(hdr + 2)  = 54 + pixelDataSize;
        *(int32_t*)(hdr + 10) = 54;
        *(int32_t*)(hdr + 14) = 40;
        *(int32_t*)(hdr + 18) = w;
        *(int32_t*)(hdr + 22) = h;
        *(int16_t*)(hdr + 26) = 1;
        *(int16_t*)(hdr + 28) = 24;
        *(int32_t*)(hdr + 34) = pixelDataSize;
        f.write(hdr, 54);

        uint8_t* row = new uint8_t[rowSize];
        if (row) {
            for (int y = h - 1; y >= 0; y--) {
                int pos = 0;
                for (int x = 0; x < w; x++) {
                    uint16_t c = _dc.readPixel(x, y);
                    row[pos++] = (c & 0x1F) << 3;
                    row[pos++] = ((c >> 5) & 0x3F) << 2;
                    row[pos++] = ((c >> 11) & 0x1F) << 3;
                }
                while (pos % 4) row[pos++] = 0;
                f.write(row, rowSize);
            }
            delete[] row;
        }
        f.close();
        _dirty = false;
        _lastPath = path;
        _showMsg(path.c_str(), COL_OK);
        delay(800);
    }

    bool _loadBmp(const String& path) {
        File f = SD.open(path.c_str());
        if (!f || f.size() < 54) return false;
        uint8_t hdr[54];
        f.read(hdr, 54);
        if (hdr[0] != 'B' || hdr[1] != 'M') { f.close(); return false; }
        int32_t w = *(int32_t*)(hdr + 18);
        int32_t h = *(int32_t*)(hdr + 22);
        if (*(uint16_t*)(hdr + 28) != 24) { f.close(); return false; }
        // Clamp to VIEW_W/VIEW_H (the real on-screen viewport), not the
        // bigger CANVAS_W/CANVAS_H backing-buffer size, so opened images
        // always fit on screen and the cursor can't wander off it.
        _imgW = min((int)w, VIEW_W);
        _imgH = min((int)h, VIEW_H);
        _dc.fillSprite(TFT_BLACK);
        f.seek(*(uint32_t*)(hdr + 10));
        int rowSize = ((w * 3 + 3) & ~3);
        uint8_t* row = new uint8_t[rowSize];
        if (row) {
            for (int y = min((int)h - 1, _imgH - 1); y >= 0; y--) {
                f.read(row, rowSize);
                for (int x = 0; x < _imgW; x++) {
                    uint8_t b = row[x*3], g = row[x*3+1], r = row[x*3+2];
                    _dc.drawPixel(x, y, ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
                }
            }
            delete[] row;
        }
        f.close();
        _dirty = false;
        _lastPath = path;
        if (_undoBuf) _saveUndo();
        return true;
    }

    String _lastPath = "";

    // ── Small dialogs ────────────────────────────────────────────────────
    bool _confirmDialog(const char* msg) {
        _canvas.fillRect(20, 45, 200, 44, COL_BG2);
        _canvas.drawRect(20, 45, 200, 44, COL_WARN);
        _canvas.setTextColor(COL_TEXT, COL_BG2);
        _canvas.drawCenterString(msg, 120, 52);
        _canvas.drawCenterString("[Y] Yes  [N] No", 120, 66);
        _canvas.pushSprite(0, 0);
        OsUI::waitRelease();
        while (true) {
            M5Cardputer.update();
            if (M5Cardputer.Keyboard.isKeyPressed('y')) { OsUI::waitRelease(); return true; }
            if (M5Cardputer.Keyboard.isKeyPressed('n')) { OsUI::waitRelease(); return false; }
            delay(20);
        }
    }

    void _showMsg(const char* msg, uint16_t col) {
        _canvas.fillRect(16, 52, 208, 20, COL_BG2);
        _canvas.drawRect(16, 52, 208, 20, col);
        _canvas.setTextColor(col, COL_BG2);
        _canvas.setClipRect(18, 54, 204, 16);
        _canvas.drawCenterString(msg, 120, 57);
        _canvas.clearClipRect();
        _canvas.pushSprite(0, 0);
    }

    // ── Tool Panel: dedicated input, own draw, own edge-triggered ENT ───
    // Returns true if this call's ENT just confirmed a tool selection.
    bool _updateToolPanel(bool ent, bool back) {
        if (_repeatKey(';', 0)) _toolPanelSel = (_toolPanelSel + PT_COUNT - 1) % PT_COUNT;
        if (_repeatKey('.', 1)) _toolPanelSel = (_toolPanelSel + 1) % PT_COUNT;
        int palTotal = _usingCustom ? PAL_TOTAL : PAL_SIZE;
        if (_repeatKey(',', 2)) { _colIdx = (_colIdx + palTotal - 1) % palTotal; _usingCustom = false; }
        if (_repeatKey('/', 3)) { _colIdx = (_colIdx + 1) % palTotal; _usingCustom = false; }
        bool confirmed = false;
        if (ent) { _setTool((PaintTool)_toolPanelSel); _uiMode = UI_DRAWING; confirmed = true; }
        if (back) { _uiMode = UI_DRAWING; }
        // Draw whichever screen matches the mode we're ACTUALLY in now, not
        // unconditionally the panel. Without this check, the very frame
        // that confirms a tool and flips _uiMode back to UI_DRAWING would
        // still redraw and display the Tool Panel screen (this function's
        // own draw call, always run to reach here) - so the screen kept
        // showing the panel for one extra frame after the mode had already
        // switched internally. That looked like "nothing happened", so the
        // next press was read as Drawing-mode input and acted on the
        // canvas immediately - the actual cause of "press ENT twice, the
        // second press paints/fills".
        if (_uiMode == UI_TOOL_PANEL) _drawToolPanelScreen();
        else _redraw();
        return confirmed;
    }

    void _drawToolPanelScreen() {
        _canvas.fillSprite(COL_BG);
        OsUI::drawHeader(_canvas, "Tools & Colors", COL_HEADER);

        static const char* TOOL_DESC[PT_COUNT] = {
            "Draw freehand - move to paint",
            "2 clicks: start point, end point",
            "2 clicks: opposite corners (outline)",
            "2 clicks: opposite corners (filled)",
            "2 clicks: center, then edge",
            "2 clicks: center, then edge (round)",
            "3 clicks: one per corner",
            "Flood-fills area under cursor",
            "Erase freehand - move to erase",
            "Picks the color under cursor",
            "Outlines every shape on canvas",
            "Types text starting at cursor",
        };

        const int rowH = 12, top = CONTENT_Y + 2, cols = 2;
        int perCol = (PT_COUNT + cols - 1) / cols;
        for (int i = 0; i < PT_COUNT; i++) {
            int col = i / perCol, row = i % perCol;
            int x = 6 + col * (SCREEN_W / cols);
            int y = top + row * rowH;
            bool sel = (i == _toolPanelSel);
            uint16_t bg = sel ? (uint16_t)COL_SELECTED : (uint16_t)COL_BG;
            _canvas.fillRect(x, y, SCREEN_W/cols - 8, rowH - 1, bg);
            if (sel) _canvas.drawRect(x - 1, y - 1, SCREEN_W/cols - 6, rowH + 1, COL_ERR);
            _canvas.setTextColor(sel ? WHITE : (uint16_t)COL_TEXT, bg);
            char label[20];
            snprintf(label, sizeof(label), "%c %s", TOOL_KEYS[i], TOOL_NAMES[i]);
            _canvas.setCursor(x + 3, y + 2);
            _canvas.print(label);
        }

        int py = top + perCol * rowH + 2;
        _canvas.setTextColor(COL_ACCENT, COL_BG);
        _canvas.setClipRect(4, py, SCREEN_W - 8, 10);
        _canvas.setCursor(6, py);
        _canvas.print(TOOL_DESC[_toolPanelSel]);
        _canvas.clearClipRect();
        py += 11;
        _canvas.setTextColor(COL_DIM, COL_BG);
        _canvas.setCursor(6, py);
        _canvas.print(",/=color  ;.=tool  ENT=use  `=back");
        py += 11;

        int total = _usingCustom ? PAL_TOTAL : PAL_SIZE;
        int sw = (SCREEN_W - 12) / min(total, 13);
        for (int i = 0; i < total; i++) {
            uint16_t c = (i == PAL_CUSTOM) ? _customColor : _palette[i];
            int x = 6 + (i % 13) * sw;
            int y = py + (i / 13) * (sw + 2);
            _canvas.fillRect(x, y, sw - 1, sw, c);
            if (i == _colIdx) {
                // Double outline (black then white) — a single white ring is
                // invisible when the selected color itself IS white, which
                // is exactly what was happening (white starts as the
                // default selected color).
                _canvas.drawRect(x - 2, y - 2, sw + 3, sw + 4, TFT_BLACK);
                _canvas.drawRect(x - 1, y - 1, sw + 1, sw + 2, WHITE);
            }
        }

        OsUI::drawFooter(_canvas, "Tab=drawing");
        _canvas.pushSprite(0, 0);
    }

    // ── Canvas viewport blit (zoom/pan) ─────────────────────────────────
    void _drawCanvasArea(int ox, int oy, int vw, int vh) {
        if (_zoom == 1) {
            for (int y = 0; y < _imgH && y < vh; y++)
                for (int x = 0; x < _imgW && x < vw; x++)
                    _canvas.drawPixel(ox + x, oy + y, _dc.readPixel(x, y));
        } else {
            int sx = max(0, min(_cx - vw / (2 * _zoom), _imgW - vw / _zoom));
            int sy = max(0, min(_cy - vh / (2 * _zoom), _imgH - vh / _zoom));
            for (int y = 0; y < vh; y++) {
                for (int x = 0; x < vw; x++) {
                    int ix = sx + x / _zoom, iy = sy + y / _zoom;
                    if (ix < _imgW && iy < _imgH)
                        _canvas.drawPixel(ox + x, oy + y, _dc.readPixel(ix, iy));
                }
            }
        }
        if (_showGrid && _zoom == 1) {
            int gw = min(_imgW, vw), gh = min(_imgH, vh);
            for (int x = 0; x <= gw; x += 8) _canvas.drawFastVLine(ox + x, oy, gh, COL_BG2);
            for (int y = 0; y <= gh; y += 8) _canvas.drawFastHLine(ox, oy + y, gw, COL_BG2);
        }
        int bw = min(_imgW, vw), bh = min(_imgH, vh);
        if (bw > 0 && bh > 0) _canvas.drawRect(ox - 1, oy - 1, bw + 2, bh + 2, COL_DIM);
    }

    void _redraw() {
        _canvas.fillSprite(COL_BG);

        String hdr = String("Paint ") + TOOL_NAMES[_tool];
        if (_shapeFilled) hdr += " fill";
        if (_dirty) hdr += " *";
        OsUI::drawHeader(_canvas, hdr.c_str(), COL_HEADER);

        int canX = 0, canY = CONTENT_Y + 2;
        int canW = SCREEN_W, canH = SCREEN_H - canY - FOOTER_H - PAL_STRIP_H - 2;
        _drawCanvasArea(canX, canY, canW, canH);

        int cx_s = _cx, cy_s = _cy + canY;
        if (_zoom > 1) {
            int vw = canW / _zoom, vh = canH / _zoom;
            int sx = max(0, min(_cx - vw / 2, _imgW - vw));
            int sy = max(0, min(_cy - vh / 2, _imgH - vh));
            cx_s = (_cx - sx) * _zoom;
            cy_s = (_cy - sy) * _zoom + canY;
        }
        // Cursor: tall caret for Text (start-of-line marker), small box otherwise.
        if (_tool == PT_TEXT) _canvas.drawFastVLine(cx_s, cy_s - 4, 9, COL_ERR);
        else _canvas.drawRect(cx_s - 2, cy_s - 2, 5, 5, COL_ERR);

        if (_p1x >= 0) {
            int py = _p1y + canY;
            _canvas.drawCircle(_p1x, py, 2, COL_WARN);
            if (_tool == PT_LINE) _canvas.drawLine(_p1x, py, cx_s, cy_s, COL_WARN);
            else if (_tool == PT_RECT || _tool == PT_FILLRECT)
                _canvas.drawRect(min(_p1x, cx_s), min(py, cy_s),
                                 abs(cx_s - _p1x) + 1, abs(cy_s - py) + 1, COL_WARN);
            else if (_tool == PT_CIRCLE) {
                int r = (int)round(sqrt((double)(_cx-_p1x)*(_cx-_p1x) + (double)(_cy-_p1y)*(_cy-_p1y)));
                _canvas.drawCircle(_p1x, py, r, COL_WARN);
            }
        }

        int py = SCREEN_H - FOOTER_H - PAL_STRIP_H;
        int sw = SCREEN_W / PAL_SIZE;
        for (int i = 0; i < PAL_SIZE; i++) {
            int px = i * sw;
            _canvas.fillRect(px + 1, py, sw - 2, PAL_STRIP_H - 1, _palette[i]);
            if (!_usingCustom && i == _colIdx) {
                _canvas.drawRect(px - 1, py - 2, sw + 2, PAL_STRIP_H + 3, TFT_BLACK);
                _canvas.drawRect(px, py - 1, sw, PAL_STRIP_H + 1, TFT_WHITE);
            }
        }

        char info[40];
        snprintf(info, sizeof(info), "%dx%d z%d b%d Tab=tools m=menu", _imgW, _imgH, _zoom, _brush);
        OsUI::drawFooter(_canvas, info);
        _canvas.pushSprite(0, 0);
    }
};
