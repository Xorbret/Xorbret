#pragma once
#include <M5Cardputer.h>
#include <Preferences.h>
#include <WiFi.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/wifi_manager.h"
#include "../../config.h"
#include "myai_engine.h"

// Screen: 240x135  font 6x8px at textSize=1
// Header: 0..HEADER_H   (standard OS header: clock/wifi/battery)
// Chat:   HEADER_H+1 .. SCREEN_H-FOOTER_H-1
// Input row: bottom FOOTER_H px (standard OS footer area, drawn as input bar)

#define AI_CHAT_Y     (HEADER_H + 2)
#define AI_LINE_H     14
#define AI_LINES      ((SCREEN_H - HEADER_H - FOOTER_H - 2) / AI_LINE_H)
#define AI_INP_Y       (SCREEN_H - FOOTER_H + 3)
#define AI_CHARS      39   // chars per line at 6px
#define AI_MAX_INPUT  80

// Message roles
#define MSG_USER  0
#define MSG_AI    1
#define MSG_SYS   2

struct AiLine {
    char     text[AI_CHARS + 1];
    uint8_t  role;   // MSG_*
};

#define MAX_LINES  64   // scrollback

class MyAiApp : public App {
public:
    MyAiApp(LGFX_Sprite& c) : _canvas(c) {}
    const char* getName()  const override { return "myAI"; }
    const char* getIcon()  const override { return "A"; }
    const char* getDesc()  const override { return "On-device AI"; }

    void onStart() override {
        _line_cnt   = 0;
        _scroll     = 0;
        _input      = "";
        _stream_buf = "";
        _streaming  = false;

        // myAI runs fully offline. WiFi's radio + network buffers stay
        // resident in heap the whole time it's connected, which can be
        // enough on its own to leave too little contiguous RAM for the
        // ~83KB model cache allocated right below. Drop it here, bring it
        // back on exit.
        _wifiWasOn = WiFiManager::get().isConnected();
        if (_wifiWasOn) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); delay(50); }

        if (!MyAI::init()) {
            String msg = String("myAI v1.0  [") + MyAI::lastError() + "]";
            _push(msg, MSG_SYS);
        } else {
            _push("myAI v1.0  ready", MSG_SYS);
        }
        _scrollBottom();
        _draw();
    }

    void onStop() override {
        MyAI::deinit();
        if (_wifiWasOn) { WiFi.mode(WIFI_STA); WiFiManager::get().connectFirst(0); }
    }

    void update() override {
        M5Cardputer.update();

        char tok;
        while (MyAI::pollToken(tok)) {
            _streaming = true;
            _stream_buf += tok;
            // rebuild last AI line from stream_buf
            _rebuildStreamLine();
            _scrollBottom();
            _draw();
        }

        if (MyAI::hasDone()) {
            MyAI::getResponse();  // clears _done flag
            _streaming = false;
            _stream_buf = "";
            _draw();
        }

        // Progress tick: during the "reading prompt" (encoding) phase, no
        // tokens stream out (pollToken stays quiet), so without this the
        // footer timer wouldn't move and the screen would look frozen for
        // the whole read.
        if (MyAI::isBusy()) {
            static uint32_t lastTick = 0;
            if (millis() - lastTick > 500) { lastTick = millis(); _draw(); }
        }

        if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;
        Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();

        // Exit
        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            OsUI::waitRelease();
            if (MyAI::isBusy()) { AppManager::get().goHome(); return; }
            if (OsUI::confirmExit(_canvas)) AppManager::get().goHome();
            else _draw();
            return;
        }

        if (MyAI::isBusy()) return;  // ignore all input while generating

        if (st.del) {
            if (_input.length() > 0) { _input.remove(_input.length()-1); _draw(); }
            return;
        }

        if (st.enter) {
            if (_input.length() == 0) return;
            String msg = _input;
            _input = "";
            // push user message
            _pushWrapped(msg, MSG_USER, "You  ");
            // placeholder for AI — will be filled by stream
            _push("", MSG_AI);
            _scrollBottom();
            _stream_buf = "";
            _streaming  = true;
            _draw();
            MyAI::sendPrompt(msg.c_str());
            return;
        }

        for (char c : st.word) {
            if ((int)_input.length() < AI_MAX_INPUT) _input += c;
        }
        if (st.word.size() > 0) _draw();
    }

private:
    LGFX_Sprite& _canvas;

    AiLine   _lines[MAX_LINES];
    int      _line_cnt  = 0;
    int      _scroll    = 0;    // index of first visible line
    String   _input;
    String   _stream_buf;
    bool     _streaming = false;
    bool     _wifiWasOn = false;

    void _push(const char* text, uint8_t role) {
        if (_line_cnt >= MAX_LINES) {
            for (int i = 0; i < MAX_LINES-1; i++) _lines[i] = _lines[i+1];
            _line_cnt = MAX_LINES-1;
        }
        strncpy(_lines[_line_cnt].text, text, AI_CHARS);
        _lines[_line_cnt].text[AI_CHARS] = '\0';
        _lines[_line_cnt].role = role;
        _line_cnt++;
    }

    void _push(const String& s, uint8_t role) { _push(s.c_str(), role); }

    // Word-wrap a message into multiple lines with a prefix on the first
    void _pushWrapped(const String& msg, uint8_t role, const char* prefix) {
        int pw = strlen(prefix);
        int w  = AI_CHARS - pw;
        String s = msg;
        bool first = true;
        while (s.length() > 0) {
            int avail = first ? w : AI_CHARS - 5;
            String pfx = first ? String(prefix) : "     ";
            first = false;
            if ((int)s.length() <= avail) {
                _push(pfx + s, role);
                break;
            }
            int cut = avail;
            int sp  = s.lastIndexOf(' ', avail);
            if (sp > 1) cut = sp;
            _push(pfx + s.substring(0, cut), role);
            s = s.substring(cut); s.trim();
        }
    }

    // Called every new token: rewrites/adds last AI line(s) from _stream_buf
    void _rebuildStreamLine() {
        // Remove existing streaming lines (last run of MSG_AI at end)
        while (_line_cnt > 0 && _lines[_line_cnt-1].role == MSG_AI &&
               strlen(_lines[_line_cnt-1].text) == 0) {
            _line_cnt--;  // remove placeholder
        }
        // Remove previous stream lines
        while (_line_cnt > 0 && _lines[_line_cnt-1].role == MSG_AI) {
            // keep going back while these are stream-built lines
            // stop at user lines or sys lines
            _line_cnt--;
        }
        // Re-wrap the full stream_buf
        if (_stream_buf.length() == 0) {
            _push("", MSG_AI);  // keep placeholder
        } else {
            _pushWrapped(_stream_buf, MSG_AI, "AI   ");
        }
    }

    void _scrollBottom() {
        _scroll = _line_cnt - AI_LINES;
        if (_scroll < 0) _scroll = 0;
    }

    uint32_t _roleColor(uint8_t role) {
        if (role == MSG_USER) return 0x867F;   // grey-blue
        if (role == MSG_AI)   return 0x67E0;   // green
        return COL_DIM;                         // sys
    }

    void _draw() {
        _canvas.fillSprite(COL_BG);

        // Header leads with free heap (this app is the tightest fit on
        // RAM in the whole OS - user wants that number always visible,
        // not just discoverable via SysInfo).
        char status[40];
        const char* state = MyAI::isBusy() ? "gen..." : MyAI::isReady() ? "ready" : "no model";
        snprintf(status, sizeof(status), "%uK free - myAI %s",
                 (unsigned)(ESP.getFreeHeap()/1024), state);
        OsUI::drawHeader(_canvas, status, COL_HEADER);
        _canvas.drawFastHLine(0, HEADER_H, SCREEN_W, COL_ACCENT);

        // Chat lines — clip rect is the real safety net: no matter how long
        // a line is or what the actual font metrics turn out to be, text
        // physically cannot render outside this box (this is what was
        // missing - AI_CHARS=39 assumed 6px/char, but nothing actually
        // enforced that at draw time, so a long system message could run
        // off the right edge of the screen).
        _canvas.setClipRect(0, AI_CHAT_Y, SCREEN_W, AI_LINES * AI_LINE_H);
        for (int i = 0; i < AI_LINES; i++) {
            int li = _scroll + i;
            if (li >= _line_cnt) break;
            int y = AI_CHAT_Y + i * AI_LINE_H;
            _canvas.setTextSize(1);
            uint32_t col = _roleColor(_lines[li].role);
            // streaming last line: draw with cursor char at end
            bool is_last_stream = _streaming && (li == _line_cnt - 1) && _lines[li].role == MSG_AI;
            _canvas.setTextColor(col, COL_BG);
            String txt = String(_lines[li].text);
            if (is_last_stream) txt += "_";
            _canvas.drawString(txt, 2, y);
        }
        _canvas.clearClipRect();

        // Standard OS footer, used here as the input bar
        _canvas.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, COL_FOOTER);
        _canvas.drawFastHLine(0, SCREEN_H - FOOTER_H, SCREEN_W, COL_ACCENT);
        _canvas.setTextSize(1);
        _canvas.setTextColor(COL_ACCENT, COL_FOOTER);
        _canvas.setCursor(2, AI_INP_Y);
        _canvas.print(">");
        if (MyAI::isBusy()) {
            char buf[40];
            uint32_t elapsed = (millis() - MyAI::busySince()) / 1000;
            const char* phase = MyAI::curPhase() == 1 ? "reading prompt" : "thinking";
            snprintf(buf, sizeof(buf), "%s %d/%d  %us (`=home)", phase, MyAI::curPos(), KV_MAX_POS-1, elapsed);
            _canvas.setTextColor(COL_DIM, COL_FOOTER);
            _canvas.setCursor(12, AI_INP_Y);
            _canvas.print(buf);
        } else if (_input.length() == 0) {
            _canvas.setTextColor(COL_DIM, COL_FOOTER);
            _canvas.setCursor(12, AI_INP_Y);
            _canvas.print("type and press Enter");
        } else {
            _canvas.setTextColor(COL_TEXT, COL_FOOTER);
            // show last AI_CHARS-2 chars so end is always visible
            int start = (int)_input.length() - (AI_CHARS - 3);
            if (start < 0) start = 0;
            String vis = _input.substring(start) + "_";
            _canvas.setCursor(12, AI_INP_Y);
            _canvas.print(vis);
        }

        _canvas.pushSprite(0, 0);
    }
};
