#pragma once
// Which Mitama forms are unlocked, and which one it wears (drawings are
// in ui/mitama_forms.h). Most rules read the Compendium, so progress made
// before forms existed counts. The rest are one-time flags set by the
// brain. Everything lives in NVS ("forms", "mitama/form") and is mirrored
// to settings.conf, so updates keep it.
#include <Arduino.h>
#include <Preferences.h>
#include "../detect/compendium.h"
#include "../system/sd_settings.h"
#include "../ui/mitama_forms.h"
#include <mbedtls/md.h>

class Forms {
public:
    static Forms& get() { static Forms f; return f; }

    // one-time achievements that the Compendium can't show
    enum Flag : uint32_t { FL_NIGHT = 1, FL_SCRY = 2, FL_KOTO = 4, FL_CREATOR = 8 };

    static const char* name(uint8_t f) {
        static const char* const N[MUI::F_COUNT] = {
            "Soul", "Hitodama", "Netrunner", "Tengu", "Ryujin", "Kami",
            "Wanderer", "Hyakki", "Shikigami", "Gilded",
            "Tinfoil", "Braindance", "Kitsune", "Oni", "Omamori",
            "Kabuto", "Mekura", "Suzu", "Hoshi",
            "Yurei", "Kekkai", "Chochin", "Kotodama",
            "Kyubi",
        };
        return f < MUI::F_COUNT ? N[f] : "?";
    }
    // How to unlock it, shown in the Compendium.
    static const char* hint(uint8_t f) {
        static const char* const H[MUI::F_COUNT] = {
            "Where every Mitama starts.",
            "Meet 10 demons.", "Meet 50 demons.", "Meet 150 demons.", "Meet 500 demons.", "Meet 1000 demons.",
            "Meet 5 different species.", "Meet a Vile, a Night, a Fairy and a Fiend.",
            "Meet 10 different species.", "Fill the whole Compendium.",
            "Meet every Vile.", "Meet every Night.", "Meet every Fairy.", "Meet every Fiend.",
            "Meet the three rare demons.",
            "Meet 10 Axon devices.", "Meet 25 Flock cameras.", "Meet 20 Ring doorbells.", "Meet 20 AirTags.",
            "See a demon between 1 and 5 AM.", "Meet a demon at -35 dBm or closer.",
            "Log 100 networks on one Scry drive.", "Send a Kotodama message.",
            "The creator's own form.",
        };
        return f < MUI::F_COUNT ? H[f] : "";
    }

    void begin() {
        Preferences p; p.begin("forms", true);
        _flags = (uint32_t)p.getInt("flags", 0);
        _seen = (uint32_t)p.getInt("seen", 0);
        p.end();
        p.begin("mitama", true);
        _cur = p.getUChar("form", 0);
        p.end();
        if (_cur >= MUI::F_COUNT) _cur = 0;
        _seen |= 1;   // Soul is never news; anything already earned gets announced, one at a time

        _began = true;
    }

    void setFlag(uint32_t f) {
        if (!_began || (_flags & f)) return;
        _flags |= f;
        Preferences p; p.begin("forms", false); p.putInt("flags", (int32_t)_flags); p.end();
        SdSettings::changed();
        _maskAt = 0;
    }

    // Bit per form; recomputed at most every 2 s.
    uint32_t unlocked() {
        uint32_t now = millis();
        if (_maskAt && now - _maskAt < 2000) return _mask;
        _maskAt = now | 1;
        auto& comp = Compendium::get();
        uint32_t enc = 0; bool close = false;
        for (uint8_t t = 0; t < Sig::T_COUNT; t++) {
            if (!Compendium::listed(t)) continue;
            auto e = comp.entry(t);
            enc += e.count;
            if (e.met && e.best >= -35 && e.best < 0) close = true;
        }
        int species = comp.metCount();
        auto full = [&](uint8_t r) { return comp.raceMet(r) == Compendium::raceSize(r); };
        auto any  = [&](uint8_t r) { return comp.raceMet(r) > 0; };
        auto cnt  = [&](uint8_t t) { return comp.entry(t).count; };
        bool on[MUI::F_COUNT] = {
            true, enc >= 10, enc >= 50, enc >= 150, enc >= 500, enc >= 1000,
            species >= 5,
            any(Sig::R_VILE) && any(Sig::R_NIGHT) && any(Sig::R_FAIRY) && any(Sig::R_FIEND),
            species >= 10, species >= Compendium::total(),
            full(Sig::R_VILE), full(Sig::R_NIGHT), full(Sig::R_FAIRY), full(Sig::R_FIEND),
            any(Sig::R_AVIAN) && any(Sig::R_FOUL) && any(Sig::R_JAKI),
            cnt(Sig::T_AXON) >= 10, cnt(Sig::T_FLOCK) >= 25, cnt(Sig::T_RING) >= 20, cnt(Sig::T_AIRTAG) >= 20,
            (_flags & FL_NIGHT) != 0, close, (_flags & FL_SCRY) != 0, (_flags & FL_KOTO) != 0,
            (_flags & FL_CREATOR) != 0,
        };
        uint32_t m = 0;
        for (int i = 0; i < MUI::F_COUNT; i++) if (on[i]) m |= 1UL << i;
        _mask = m;
        return m;
    }
    bool isUnlocked(uint8_t f) { return f < MUI::F_COUNT && (unlocked() >> f & 1); }
    // Earnable forms unlocked (the creator's form isn't counted).
    int unlockedCount() { return __builtin_popcount(unlocked() & ((1UL << MUI::F_PUBLIC) - 1)); }
    // Forms the Compendium lists: the creator's form only once it's yours.
    int listed() { return isUnlocked(MUI::F_KYUBI) ? MUI::F_COUNT : MUI::F_PUBLIC; }

    // The creator's code. Only a salted SHA-256 of it is in the firmware.
    bool tryCode(const String& typed) {
        String code = typed; code.trim(); code.toLowerCase(); code.replace(" ", ""); code.replace("-", "");
        String salted = "MitamaOS creator:" + code;
        static const uint8_t WANT[32] = { 0x75, 0x66, 0xfa, 0xae, 0x0d, 0x2d, 0x34, 0x82, 0xee, 0x39, 0x01, 0xcb, 0x12, 0x6b, 0x57, 0x0b, 0xb2, 0x7a, 0x7f, 0xe6, 0xb5, 0xe4, 0x71, 0x4b, 0x86, 0x43, 0x05, 0x91, 0xf9, 0x8c, 0x35, 0xe1 };
        uint8_t got[32];
        mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t*)salted.c_str(), salted.length(), got);
        uint8_t diff = 0;
        for (int i = 0; i < 32; i++) diff |= got[i] ^ WANT[i];
        if (diff) return false;
        setFlag(FL_CREATOR);
        _maskAt = 0;
        wear(MUI::F_KYUBI);
        return true;
    }

    MUI::Form current() { return isUnlocked(_cur) ? (MUI::Form)_cur : MUI::F_SOUL; }
    void wear(uint8_t f) {
        if (!isUnlocked(f) || f == _cur) return;
        _cur = f;
        Preferences p; p.begin("mitama", false); p.putUChar("form", _cur); p.end();
        SdSettings::changed();
    }
    // The next unlocked form after the current one (wraps to Soul).
    MUI::Form next() {
        uint32_t m = unlocked();
        uint8_t f = current();
        for (int k = 1; k <= MUI::F_COUNT; k++) {
            uint8_t g = (f + k) % MUI::F_COUNT;
            if (m >> g & 1) { wear(g); return (MUI::Form)g; }
        }
        return MUI::F_SOUL;
    }

    // A form unlocked since the last call, lowest first. Marks it announced.
    bool takeNew(uint8_t& out) {
        if (!_began) return false;
        uint32_t fresh = unlocked() & ~_seen;
        if (!fresh) return false;
        out = __builtin_ctz(fresh);
        _seen |= 1UL << out;
        Preferences p; p.begin("forms", false); p.putInt("seen", (int32_t)_seen); p.end();
        SdSettings::changed();
        return true;
    }

private:
    bool     _began = false;
    uint32_t _flags = 0, _seen = 0, _mask = 0, _maskAt = 0;
    uint8_t  _cur = 0;
};
