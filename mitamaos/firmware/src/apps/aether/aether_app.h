#pragma once
#include <climits>
#include <M5Cardputer.h>
#include <WiFi.h>
#include "../../os/app.h"
#include "../../os/app_manager.h"
#include "../../os/os_ui.h"
#include "../../os/disp_lock.h"
#include "../../config.h"
#include "../../ui/mitama_ui.h"
#include "../../detect/detection.h"
#include "../../detect/aether.h"

// Aether - how crowded each 2.4 GHz Wi-Fi channel is, from the beacons
// routers already broadcast (heard by the detection watcher as it hops;
// nothing is sent). One bar per channel: height = networks heard there in
// the last minute, colour = how strong the loudest one is. Suggests the
// quietest of channels 1 / 6 / 11, the ones that don't overlap.
//
// Keys: ` back.
class AetherApp : public App {
public:
    AetherApp(LGFX_Sprite& canvas) : _canvas(canvas) {}

    const char* getName() const override { return "Aether"; }
    const char* getIcon() const override { return "A"; }
    const char* getDesc() const override { return "Wi-Fi channel crowding"; }

    void onStart() override { OsUI::waitRelease(); _draw(); }
    void onStop() override {}

    void update() override {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isKeyPressed('`')) { OsUI::waitRelease(); AppManager::get().goHome(); return; }
        if (millis() - _lastDraw > 1000) _draw();
    }

private:
    LGFX_Sprite& _canvas;
    uint32_t _lastDraw = 0;

    static uint16_t _strength(int8_t rssi) {
        return rssi > -55 ? MUI::RED : rssi > -70 ? MUI::YELLOW : MUI::CYAN;
    }

    void _draw() {
        _lastDraw = millis();
        _canvas.fillSprite(MUI::BG);
        MUI::header(_canvas, "AETHER", MUI::CYAN);

        AetherTally::Channel ch[AetherTally::CHANNELS];
        int total = AetherTally::get().snapshot(ch);
        int maxAps = 1;
        for (auto& c : ch) maxAps = max<int>(maxAps, c.aps);

        const int left = 8, base = SCREEN_H - FOOTER_H - 14, top = HEADER_H + 26;
        const int slot = (SCREEN_W - 2 * left) / AetherTally::CHANNELS, bw = slot - 5;
        _canvas.drawFastHLine(left - 2, base + 1, SCREEN_W - 2 * left + 4, MUI::BONES_DIM);
        for (int i = 0; i < AetherTally::CHANNELS; i++) {
            int x = left + i * slot;
            bool clean = i == 0 || i == 5 || i == 10;         // 1, 6, 11 don't overlap
            if (ch[i].aps) {
                int h = max(2, (base - top) * ch[i].aps / maxAps);
                _canvas.fillRect(x, base - h + 1, bw, h, _strength(ch[i].best));
                char n[4]; snprintf(n, sizeof(n), "%u", ch[i].aps);
                MUI::textCentered(_canvas, MUI::small(), n, x + bw / 2, base - h - 11, MUI::WHITE);
            }
            char lbl[4]; snprintf(lbl, sizeof(lbl), "%d", i + 1);
            MUI::textCentered(_canvas, MUI::small(), lbl, x + bw / 2, base + 2, clean ? MUI::YELLOW : MUI::DIM);
        }

        // Quietest of 1 / 6 / 11. A network spills about 4 channels each
        // side, so neighbours count too (less the further away they are);
        // a tie goes to the one whose loudest neighbour is weakest.
        static const int CLEAN[3] = { 0, 5, 10 };
        int best = 0, bestCost = INT_MAX, bestLoud = 127;
        for (int k = 0; k < 3; k++) {
            int cost = 0, loud = -127;
            for (int d = -4; d <= 4; d++) {
                int j = CLEAN[k] + d;
                if (j < 0 || j >= AetherTally::CHANNELS || !ch[j].aps) continue;
                cost += (5 - abs(d)) * ch[j].aps;
                loud = max<int>(loud, ch[j].best);
            }
            if (cost < bestCost || (cost == bestCost && loud < bestLoud)) { best = k; bestCost = cost; bestLoud = loud; }
        }
        char line[64];
        DetectionEngine::Stats st = DetectionEngine::get().stats();
        if (!st.wifiOn) strcpy(line, "Wi-Fi listening is paused.");
        else if (WiFi.status() == WL_CONNECTED)
            snprintf(line, sizeof(line), "Joined Wi-Fi: only hearing channel %d.", WiFi.channel());
        else if (!total) strcpy(line, "Listening... give it a few seconds.");
        else snprintf(line, sizeof(line), "%d networks. Quietest clear channel: %d", total, CLEAN[best] + 1);
        MUI::text(_canvas, MUI::small(), line, 6, HEADER_H + 2, MUI::LABEL);

        MUI::footer(_canvas, "Bars: networks per channel, colour = loudest", MUI::DIM);
        { DispLock _lk; _canvas.pushSprite(0, 0); }
    }
};
