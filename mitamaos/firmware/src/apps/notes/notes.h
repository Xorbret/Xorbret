#pragma once
#include <M5Cardputer.h>
#include <Preferences.h>
#include "../../system/sd_settings.h"
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../config.h"

#define NOTES_MAX  5
#define NOTES_COLS 38

class Notes : public App {
public:
    Notes(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Notes"; }
    const char* getIcon() const override { return "N"; }
    const char* getDesc() const override { return "Quick notepad"; }

    void onStart() override {
        _loadAll(); _mode = MODE_LIST; _sel = 0; _draw();
    }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();

        // Esc (` key) - behavior depends on mode
        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            if (_mode == MODE_EDIT) {
                // Esc in editor = save and return to list
                _saveNote(_sel, _editBuf);
                _mode = MODE_LIST;
                OsUI::waitRelease();
                _draw();
            } else {
                _draw(); _canvas.pushSprite(0, 0);
                if (OsUI::confirmExit(_canvas)) AppManager::get().goHome();
                else _draw();
            }
            return;
        }

        if (_mode == MODE_LIST) _updateList();
        else                    _updateEdit();
    }

private:
    enum Mode { MODE_LIST, MODE_EDIT };

    LGFX_Sprite& _canvas;
    Mode    _mode    = MODE_LIST;
    int     _sel     = 0;
    String  _titles[NOTES_MAX];
    String  _bodies[NOTES_MAX];
    String  _editBuf = "";
    uint32_t _keyTimer = 0;

    void _loadAll() {
        Preferences p; p.begin("notes", true);
        for (int i = 0; i < NOTES_MAX; i++) {
            _bodies[i] = p.getString(("n" + String(i)).c_str(), "");
            _titles[i] = _bodies[i].length() > 0
                ? _bodies[i].substring(0, min((int)_bodies[i].length(), 20))
                : "(empty)";
        }
        p.end();
    }

    void _saveNote(int idx, const String& text) {
        _bodies[idx] = text;
        _titles[idx] = text.length() > 0
            ? text.substring(0, min((int)text.length(), 20))
            : "(empty)";
        Preferences p; p.begin("notes", false);
        p.putString(("n" + String(idx)).c_str(), text);
        p.end();
        SdSettings::changed();
    }

    void _updateList() {
        bool redraw = false;
        auto st = M5Cardputer.Keyboard.keysState();

        if (M5Cardputer.Keyboard.isKeyPressed(';')) {
            if (_sel > 0) { _sel--; redraw = true; } delay(KEY_REPEAT_MS);
        }
        if (M5Cardputer.Keyboard.isKeyPressed('.')) {
            if (_sel < NOTES_MAX - 1) { _sel++; redraw = true; } delay(KEY_REPEAT_MS);
        }
        if (st.enter) {
            _editBuf = _bodies[_sel]; _mode = MODE_EDIT; _typing = OsUI::Typing();
            _keyTimer = millis(); OsUI::waitRelease(); redraw = true;
        }
        if (st.del) {
            _draw(); _canvas.pushSprite(0, 0);
            if (OsUI::confirmExit(_canvas, "Clear this note?")) _saveNote(_sel, "");
            redraw = true; delay(150);
        }
        if (redraw) _draw();
    }

    void _updateEdit() {
        String added; bool del, ent;
        _typing.poll(M5Cardputer.Keyboard.keysState(), added, del, ent);
        bool redraw = false;
        if (ent && _editBuf.length() < 200) { _editBuf += '\n'; redraw = true; }   // Enter = new line
        for (size_t i = 0; i < added.length() && _editBuf.length() < 200; i++) { _editBuf += added[i]; redraw = true; }
        if (del && _editBuf.length() > 0) { _editBuf.remove(_editBuf.length() - 1); redraw = true; }
        if (redraw) _draw();
    }
    OsUI::Typing _typing;

    void _draw() {
        _canvas.startWrite();
        _canvas.fillSprite(COL_BG);

        if (_mode == MODE_LIST) {
            OsUI::drawHeader(_canvas, "Notes", COL_HEADER);
            _canvas.setTextSize(1);
            for (int i = 0; i < NOTES_MAX; i++) {
                int y = CONTENT_Y + 4 + i * 18;
                bool sel = (i == _sel);
                if (sel) {
                    _canvas.fillRect(0, y - 2, SCREEN_W, 16, COL_SELECTED);
                    _canvas.setTextColor(COL_BG, COL_SELECTED);
                } else {
                    _canvas.setTextColor(
                        _bodies[i].length() > 0 ? (uint16_t)COL_TEXT : (uint16_t)COL_DIM,
                        COL_BG);
                }
                _canvas.setCursor(6, y);
                char tbuf[30]; strncpy(tbuf, _titles[i].c_str(), 28); tbuf[28] = '\0';
                _canvas.printf("%d. %s", i + 1, tbuf);
            }
            OsUI::drawFooter(_canvas, ";.=nav ENT=edit Del=clr `=home");
        } else {
            // Header with counter
            _canvas.fillRect(0, 0, SCREEN_W, 14, COL_HEADER);
            _canvas.setTextColor(WHITE, COL_HEADER);
            _canvas.setTextSize(1);
            _canvas.setCursor(4, 4);
            _canvas.print("Notes");
            // Counter in the top-right of header
            char cntbuf[10]; snprintf(cntbuf, sizeof(cntbuf), "%d/200", (int)_editBuf.length());
            _canvas.setTextColor(COL_DIM, COL_HEADER);
            int cw = strlen(cntbuf) * 6 + 4;
            _canvas.setCursor(SCREEN_W - cw, 4);
            _canvas.print(cntbuf);
            _canvas.drawFastHLine(0, 14, SCREEN_W, COL_ACCENT);

            // Editor area
            _canvas.fillRect(0, 15, SCREEN_W, SCREEN_H - 15 - FOOTER_H, COL_BG2);
            _canvas.setTextColor(COL_TEXT, COL_BG2);
            _canvas.setTextSize(1);

            // Split into NOTES_COLS-char lines and \n
            String txt = _editBuf + "_";
            int line = 0;
            int maxLines = (SCREEN_H - 15 - FOOTER_H) / 12;
            int startLine = 0;
            // Count total lines to show the end
            std::vector<String> linesBuf;
            int start = 0;
            while (start <= (int)txt.length()) {
                int nl = txt.indexOf('\n', start);
                if (nl < 0) nl = txt.length();
                String chunk = txt.substring(start, nl);
                while ((int)chunk.length() > NOTES_COLS) {
                    linesBuf.push_back(chunk.substring(0, NOTES_COLS));
                    chunk = chunk.substring(NOTES_COLS);
                }
                linesBuf.push_back(chunk);
                start = nl + 1;
            }
            // Auto-scroll: show the end
            int total = (int)linesBuf.size();
            startLine = max(0, total - maxLines);
            for (int i = startLine; i < total && (i - startLine) < maxLines; i++) {
                _canvas.setCursor(4, 16 + (i - startLine) * 12);
                _canvas.print(linesBuf[i]);
            }

            OsUI::drawFooter(_canvas, "ENT=newline  `=save&back");
        }

        _canvas.pushSprite(0, 0);
        _canvas.endWrite();
    }
};
