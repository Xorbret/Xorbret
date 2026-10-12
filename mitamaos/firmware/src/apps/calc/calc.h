#pragma once
#include <M5Cardputer.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../config.h"

//  Calc - calculator with a 5-line history
//  M5Cardputer keys: digits 0-9, +-*/., ENT=evaluate,
//  Del=backspace, C=clear, `=home
class Calc : public App {
public:
    Calc(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Calc"; }
    const char* getIcon() const override { return "C"; }
    const char* getDesc() const override { return "Calculator"; }

    void onStart() override {
        _expr  = "";
        _error = false;
        _histCount = 0;
        for (int i = 0; i < HIST_MAX; i++) _hist[i] = "";
        _draw();
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();

        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            _draw();
            _canvas.pushSprite(0, 0);
            if (OsUI::confirmExit(_canvas)) {
                AppManager::get().goHome();
            } else {
                _draw();
            }
            return;
        }

        auto st = M5Cardputer.Keyboard.keysState();
        bool redraw = false;

        // Del - backspace
        if (st.del && millis() - _keyTimer > 120) {
            if (_expr.length() > 0) {
                _expr.remove(_expr.length() - 1);
                _error = false;
                redraw = true;
            }
            _keyTimer = millis();
        }

        // Enter - evaluate
        if (st.enter) {
            OsUI::waitRelease();
            if (_expr.length() > 0) {
                double result = _eval(_expr);
                String histLine;
                if (_error) {
                    histLine = _expr + " = ERR";
                } else {
                    histLine = _expr + " = " + _formatResult(result);
                    _expr = _formatResult(result); // allow continuing the calculation
                }
                _pushHist(histLine);
                if (!_error) _error = false;
            }
            redraw = true;
        }

        // C - full clear
        if (M5Cardputer.Keyboard.isKeyPressed('c') && millis() - _keyTimer > 200) {
            _expr = ""; _error = false; redraw = true;
            _keyTimer = millis();
        }

        // Expression characters
        if (st.word.size() > 0 && millis() - _keyTimer > 130) {
            for (auto ch : st.word) {
                bool valid = (ch >= '0' && ch <= '9') ||
                             ch == '+' || ch == '-' ||
                             ch == '*' || ch == '/' ||
                             ch == '.' || ch == '(' || ch == ')';
                if (valid && _expr.length() < 28) {
                    _expr += ch;
                    _error = false;
                    redraw = true;
                }
            }
            _keyTimer = millis();
        }

        if (redraw) _draw();
    }

private:
    static const int HIST_MAX = 5;

    LGFX_Sprite& _canvas;
    String   _expr  = "";
    bool     _error = false;
    String   _hist[HIST_MAX];
    int      _histCount = 0;
    uint32_t _keyTimer  = 0;

    void _pushHist(const String& line) {
        if (_histCount < HIST_MAX) {
            _hist[_histCount++] = line;
        } else {
            for (int i = 1; i < HIST_MAX; i++) _hist[i-1] = _hist[i];
            _hist[HIST_MAX-1] = line;
        }
    }

    String _formatResult(double v) {
        if (_error) return "ERR";
        // Integer result - drop the decimal part
        if (v == (long long)v && abs(v) < 1e12) {
            char buf[20];
            snprintf(buf, sizeof(buf), "%lld", (long long)v);
            return String(buf);
        }
        char buf[20];
        snprintf(buf, sizeof(buf), "%.6g", v);
        return String(buf);
    }

    // Supports +, -, *, /, parentheses, unary minus, decimal point
    const char* _ptr = nullptr;

    double _eval(const String& expr) {
        _error = false;
        String e = expr;
        e.replace(" ", "");
        _ptr = e.c_str();
        double result = _parseExpr();
        if (*_ptr != '\0') _error = true;
        return result;
    }

    double _parseExpr() {
        double left = _parseTerm();
        while (*_ptr == '+' || *_ptr == '-') {
            char op = *_ptr++;
            double right = _parseTerm();
            left = (op == '+') ? left + right : left - right;
        }
        return left;
    }

    double _parseTerm() {
        double left = _parseFactor();
        while (*_ptr == '*' || *_ptr == '/') {
            char op = *_ptr++;
            double right = _parseFactor();
            if (op == '/') {
                if (right == 0.0) { _error = true; return 0.0; }
                left /= right;
            } else {
                left *= right;
            }
        }
        return left;
    }

    double _parseFactor() {
        // Unary minus
        if (*_ptr == '-') { _ptr++; return -_parseFactor(); }
        if (*_ptr == '+') { _ptr++; return  _parseFactor(); }
        // Parentheses
        if (*_ptr == '(') {
            _ptr++;
            double val = _parseExpr();
            if (*_ptr == ')') _ptr++;
            else _error = true;
            return val;
        }
        // Number
        char* end;
        double val = strtod(_ptr, &end);
        if (end == _ptr) { _error = true; return 0.0; }
        _ptr = end;
        return val;
    }

    void _draw() {
        _canvas.startWrite();
        _canvas.fillSprite(COL_BG);
        OsUI::drawHeader(_canvas, "Calc", COL_HEADER);

        _canvas.setTextSize(1);

        // History - small text at the top
        for (int i = 0; i < _histCount; i++) {
            int y = CONTENT_Y + 4 + i * 14;
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.setCursor(4, y);
            // Truncate the history string
            char hbuf[32];
            strncpy(hbuf, _hist[i].c_str(), 31); hbuf[31] = '\0';
            _canvas.print(hbuf);
        }

        // Separator
        int divY = CONTENT_Y + 4 + HIST_MAX * 14;
        _canvas.drawFastHLine(4, divY, SCREEN_W - 8, COL_SELECTED);

        // Input field - large text
        _canvas.fillRect(2, divY + 3, SCREEN_W - 4, 22, COL_BG2);
        _canvas.drawRect(2, divY + 3, SCREEN_W - 4, 22, _error ? (uint16_t)COL_ERR : (uint16_t)COL_ACCENT);
        _canvas.setTextSize(2);
        _canvas.setTextColor(_error ? (uint16_t)COL_ERR : (uint16_t)WHITE, COL_BG2);
        _canvas.setCursor(6, divY + 7);
        // Show the last 14 chars to fit at x2 size
        String disp = _expr;
        if (disp.length() > 14) disp = disp.substring(disp.length() - 14);
        _canvas.print(disp + "_");

        _canvas.setTextSize(1);

        // Key hints at the bottom of the screen (above the footer)
        _canvas.setTextColor(COL_DIM, COL_BG);
        _canvas.setCursor(4, divY + 30);
        _canvas.print("0-9 +-*/ () .  ENT=calc  C=clr");

        OsUI::drawFooter(_canvas, "Del=backspace  C=clear  `=home");
        _canvas.pushSprite(0, 0);
        _canvas.endWrite();
    }
};
