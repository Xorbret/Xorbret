#pragma once
#include <M5Cardputer.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../config.h"

//  Piano - synth on the M5Cardputer keyboard
//
//  Key layout (two octaves):
//  Bottom row: a s d f g h j k l ;   = white keys C4..B4
//  Top row:    q w e r t y u i o p   = white keys C5..B5
//  Sharps/flats on z x c v b n m , . /
//
//  TAB - change octave (+/-1), ` = home
class Piano : public App {
public:
    Piano(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Piano"; }
    const char* getIcon() const override { return "P"; }
    const char* getDesc() const override { return "Keyboard synthesizer"; }

    void onStart() override {
        _octave    = 4;
        _vol       = 180;
        _lastNote  = 0;
        _noteLabel[0] = '\0';
        M5Cardputer.Speaker.begin();
        M5Cardputer.Speaker.setVolume(_vol);
        _draw();
    }

    void onStop() override {
        M5Cardputer.Speaker.stop();
        M5Cardputer.Speaker.end();
    }

    void update() override {
        M5Cardputer.update();
        auto ks = M5Cardputer.Keyboard.keysState();

        // Exit home
        if (M5Cardputer.Keyboard.isKeyPressed('`')) {
            OsUI::waitRelease();
            M5Cardputer.Speaker.stop();
            AppManager::get().launchByIndex(0);
            return;
        }

        // Octave TAB
        if (ks.tab) {
            _octave++;
            if (_octave > 7) _octave = 2;
            OsUI::waitRelease();
            _draw();
        }

        // Volume
        if (M5Cardputer.Keyboard.isKeyPressed('=')) {
            _vol = min(255, _vol + 20);
            M5Cardputer.Speaker.setVolume(_vol);
            _draw(); delay(80);
        }
        if (M5Cardputer.Keyboard.isKeyPressed('-')) {
            _vol = max(0, _vol - 20);
            M5Cardputer.Speaker.setVolume(_vol);
            _draw(); delay(80);
        }

        // Bottom-octave white keys (oct)
        struct { char k; int semitone; const char* lbl; } lower[] = {
            {'a', 0,  "C"},  {'s', 2,  "D"},  {'d', 4,  "E"},
            {'f', 5,  "F"},  {'g', 7,  "G"},  {'h', 9,  "A"},
            {'j', 11, "B"},  {'k', 12, "C+"},
        };
        // Upper-octave white keys (oct+1)
        struct { char k; int semitone; const char* lbl; } upper[] = {
            {'q', 0,  "C"},  {'w', 2,  "D"},  {'e', 4,  "E"},
            {'r', 5,  "F"},  {'t', 7,  "G"},  {'y', 9,  "A"},
            {'u', 11, "B"},  {'i', 12, "C+"},
        };
        // Sharps/flats
        struct { char k; int semitone; const char* lbl; } sharps[] = {
            {'z', 1, "C#"}, {'x', 3, "D#"}, {'c', 6, "F#"},
            {'v', 8, "G#"}, {'b', 10,"A#"}, {'n', 13,"C#+"},
        };

        uint32_t freq = 0;
        const char* label = nullptr;

        for (auto& n : lower) {
            if (M5Cardputer.Keyboard.isKeyPressed(n.k)) {
                freq  = _noteFreq(_octave, n.semitone);
                label = n.lbl;
                break;
            }
        }
        if (!freq) for (auto& n : upper) {
            if (M5Cardputer.Keyboard.isKeyPressed(n.k)) {
                freq  = _noteFreq(_octave + 1, n.semitone);
                label = n.lbl;
                break;
            }
        }
        if (!freq) for (auto& n : sharps) {
            if (M5Cardputer.Keyboard.isKeyPressed(n.k)) {
                freq  = _noteFreq(_octave, n.semitone);
                label = n.lbl;
                break;
            }
        }

        if (freq && freq != _lastNote) {
            _lastNote = freq;
            M5Cardputer.Speaker.tone(freq, 300); // 300ms note
            if (label) strncpy(_noteLabel, label, 7);
            _draw();
        } else if (!freq && _lastNote) {
            _lastNote = 0;
            _noteLabel[0] = '\0';
            _draw();
        }
    }

private:
    LGFX_Sprite& _canvas;
    int      _octave;
    int      _vol;
    uint32_t _lastNote;
    char     _noteLabel[8];

    // Note frequency: base C4 = 261.63 Hz, semitone step = 2^(1/12)
    uint32_t _noteFreq(int octave, int semitone) {
        // C4=261.63, doubles every 12 semitones
        float base = 261.63f * pow(2.0f, (float)(octave - 4));
        return (uint32_t)(base * pow(1.05946f, semitone));
    }

    void _draw() {
        _canvas.fillSprite(COL_BG);
        OsUI::drawHeader(_canvas, "Piano");
        OsUI::drawFooter(_canvas, "TAB=oct+  +/-=vol  `=home");

        // Octave
        char ob[20];
        snprintf(ob, sizeof(ob), "Octave: %d", _octave);
        _canvas.setTextColor(COL_ACCENT, COL_BG);
        _canvas.setCursor(6, CONTENT_Y + 4);
        _canvas.print(ob);

        // Current note - large display
        _canvas.setTextSize(3);
        uint16_t notecol = _lastNote ? COL_OK : COL_DIM;
        _canvas.setTextColor(notecol, COL_BG);
        _canvas.drawCenterString(_lastNote ? _noteLabel : "--", 120, CONTENT_Y + 20);
        _canvas.setTextSize(1);

        // Frequency
        if (_lastNote) {
            char fb[20];
            snprintf(fb, sizeof(fb), "%u Hz", _lastNote);
            _canvas.setTextColor(COL_DIM, COL_BG);
            _canvas.drawCenterString(fb, 120, CONTENT_Y + 50);
        }

        // Draw the keyboard (schematic)
        // 8 white keys in the bottom row
        const char* wKeys = "a s d f g h j k";
        const char* notes = "C D E F G A B C";
        int kx = 10, ky = CONTENT_Y + 62, kw = 26, kh = 30;
        for (int i = 0; i < 8; i++) {
            bool pressed = (i < 7) ? M5Cardputer.Keyboard.isKeyPressed("asdfghjk"[i])
                                    : false;
            uint16_t col = pressed ? COL_ACCENT : WHITE;
            _canvas.fillRect(kx + i * (kw + 1), ky, kw, kh, col);
            _canvas.drawRect(kx + i * (kw + 1), ky, kw, kh, COL_TEXT);
            // Note letter
            char nbuf[3] = {notes[i * 2], '\0'};
            _canvas.setTextColor(pressed ? WHITE : BLACK, col);
            _canvas.setCursor(kx + i * (kw + 1) + 8, ky + kh - 10);
            _canvas.print(nbuf);
        }

        // Black keys (C# D# F# G# A#) - positions 0,1,3,4,5
        int sharp_pos[] = {0, 1, 3, 4, 5};
        const char* sharpKeys = "zxcvb";
        for (int i = 0; i < 5; i++) {
            int p = sharp_pos[i];
            bool pressed = M5Cardputer.Keyboard.isKeyPressed(sharpKeys[i]);
            uint16_t col = pressed ? COL_ACCENT : COL_BG2;
            int sx = kx + p * (kw + 1) + (kw / 2) + 2;
            _canvas.fillRect(sx, ky, 14, 18, col);
            _canvas.drawRect(sx, ky, 14, 18, COL_DIM);
        }

        // Volume
        int vbw = map(_vol, 0, 255, 0, 70);
        _canvas.setTextColor(COL_DIM, COL_BG);
        _canvas.setCursor(SCREEN_W - 100, CONTENT_Y + 4);
        _canvas.print("VOL");
        _canvas.drawRect(SCREEN_W - 76, CONTENT_Y + 4, 72, 7, COL_DIM);
        _canvas.fillRect(SCREEN_W - 75, CONTENT_Y + 5, vbw, 5, COL_ACCENT);

        _canvas.pushSprite(0, 0);
    }
};
