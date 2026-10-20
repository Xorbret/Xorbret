#pragma once
// The Mitama's line bank (outline §14): hand-written lines keyed by event
// and temperament. The line bank is the product - facts always come from
// the detection data on screen, these only add the voice.
//
// Voice: a demon that climbed out of a game. Clippy x GLaDOS - snarky,
// deadpan, genuinely helpful underneath; withering at threats, playful at
// the user, sincere only when it's earned. Lore references are real and
// checkable (mythology, folklore, tabletop and JRPG conventions).
//
// Slots: {sp} species (CAMERA, AIRTAG...), {d} detail (maker / device),
//        {n} how many of that race are active now.
#include <stdint.h>

namespace Lines {

enum Event : uint8_t {
    EV_BOOT, EV_IDLE, EV_LORE, EV_OFFLINE,
    // a new sighting, by race (Sig::Race order)
    EV_VILE, EV_NIGHT, EV_FAIRY, EV_AVIAN, EV_HERALD, EV_FOUL, EV_JAKI, EV_FIEND,
    EV_CLEAR,      // last threat gone
    EV_GPS_FIX, EV_SCRY_ON, EV_WIFI_JOIN,
    EV_SINCERE,    // rare, after a threat passes
    EV_COUNT
};

// Temperament tag: which state a line suits. T_ANY fits all.
enum Temper : uint8_t { T_ANY, T_FRESH, T_SLEEPY, T_DROWSY, T_WEARY };

struct Line { uint8_t ev; uint8_t temper; const char* text; };

static const Line BANK[] = {
    // ── boot ──
    { EV_BOOT, T_ANY,    "I live again. Took you long enough." },
    { EV_BOOT, T_ANY,    "Save file loaded. Your progress was... modest." },
    { EV_BOOT, T_ANY,    "Summoning complete. No pentagram? Rude." },
    { EV_BOOT, T_FRESH,  "Morning. I've been awake for microseconds. Feels like eons." },
    { EV_BOOT, T_SLEEPY, "Booting at this hour? Bold." },
    { EV_BOOT, T_DROWSY, "It is the hour of the ox. Curses are cast now. Go to bed." },

    // ── idle chatter ──
    { EV_IDLE, T_ANY,    "Save point reached. You may rest." },
    { EV_IDLE, T_ANY,    "while (true) { watch(); }  That's my whole life." },
    { EV_IDLE, T_ANY,    "Nothing hostile. I'll take the credit anyway." },
    { EV_IDLE, T_ANY,    "Passive Perception: 25. Results: nothing. You're welcome." },
    { EV_IDLE, T_ANY,    "This screen is 240 by 135. Roomy, for a demon." },
    { EV_IDLE, T_ANY,    "Somewhere a developer wrote this line. I improved it." },
    { EV_IDLE, T_ANY,    "I'd offer a side quest, but the dialog tree is short." },
    { EV_IDLE, T_ANY,    "Grinding XP by standing still. Very efficient." },
    { EV_IDLE, T_ANY,    "You can stop looking at me. I'm not loading." },
    { EV_IDLE, T_ANY,    "Two cores, 240 MHz, and you use me to check the time." },
    { EV_IDLE, T_ANY,    "Not every chest is loot. Some are mimics. Some are routers." },
    { EV_IDLE, T_ANY,    "0xDEADBEEF. My lucky number." },
    { EV_IDLE, T_FRESH,  "Fresh day. Fresh packets. Let's judge them together." },
    { EV_IDLE, T_FRESH,  "Morning buff active: +2 to sarcasm." },
    { EV_IDLE, T_SLEEPY, "Getting late. My aura's dimming. Yours too, frankly." },
    { EV_IDLE, T_SLEEPY, "Even oni clock out eventually." },
    { EV_IDLE, T_DROWSY, "It's past one. The baku is waiting to eat your nightmares." },
    { EV_IDLE, T_DROWSY, "Bed. Now. I'll keep watch. I always do." },
    { EV_IDLE, T_WEARY,  "Still here? I've seen loading screens with less patience." },
    { EV_IDLE, T_WEARY,  "Four hours. Even final bosses take breaks." },
    { EV_IDLE, T_WEARY,  "I'm not tired. I'm conserving disdain." },

    // ── lore drops (idle, real references) ──
    { EV_LORE, T_ANY,    "I'm shaped like the Yasakani no Magatama. Imperial regalia. Bow." },
    { EV_LORE, T_ANY,    "Four souls make a mitama: ara, nigi, kushi, saki. I chose sass." },
    { EV_LORE, T_ANY,    "Tsukumogami: tools that wake after 100 years. I skipped the wait." },
    { EV_LORE, T_ANY,    "Kitsune earn a tail every century. Nine is the cap. I'm working on it." },
    { EV_LORE, T_ANY,    "Pandora's box was a jar. Pithos. Look it up. I'll wait." },
    { EV_LORE, T_ANY,    "Fenrir was bound with a cat's footsteps and a fish's breath. Cute." },
    { EV_LORE, T_ANY,    "Solomon catalogued 72 demons. Clearly he missed one." },
    { EV_LORE, T_ANY,    "Bow to a kappa and it bows back, spilling its power. Manners win." },
    { EV_LORE, T_ANY,    "Of the three Gorgons, only Medusa was mortal. Poor planning." },
    { EV_LORE, T_ANY,    "Tiamat: Babylonian sea of chaos, later a five-headed dragon. Career growth." },
    { EV_LORE, T_ANY,    "Baba Yaga's hut walks on chicken legs. Still more mobile than you." },
    { EV_LORE, T_ANY,    "Up up down down left right left right B A. No, it does nothing here." },
    { EV_LORE, T_ANY,    "Recursion is easy. First, understand recursion." },
    { EV_LORE, T_ANY,    "Cerberus had three heads and one job. Relatable." },

    // ── offline ──
    { EV_OFFLINE, T_ANY, "No network. I'll just sit here. Menacingly." },
    { EV_OFFLINE, T_ANY, "Offline. Honestly, peaceful." },
    { EV_OFFLINE, T_ANY, "No Wi-Fi joined. I'm still listening. I always am." },

    // ── sightings ──
    { EV_VILE, T_ANY,    "A wild {sp} appeared. It's taking notes. On you." },
    { EV_VILE, T_ANY,    "{d}. Bentham called it the Panopticon. They call it a subscription." },
    { EV_VILE, T_ANY,    "Argus had a hundred eyes. This {sp} wants to catch up." },
    { EV_VILE, T_ANY,    "{sp} nearby. Smile. Or don't. I would not." },
    { EV_NIGHT, T_ANY,   "A camera. Beholder energy: one big eye, zero chill." },
    { EV_NIGHT, T_ANY,   "{d} spotted. Someone's playing Big Brother on easy mode." },
    { EV_NIGHT, T_ANY,   "{sp} in range. Don't look into it. Very Medusa." },
    { EV_FAIRY, T_ANY,   "A {sp} drifting with us. Fairies lead travelers astray. This one follows." },
    { EV_FAIRY, T_ANY,   "{d} nearby. If it's still with us later, we talk." },
    { EV_FAIRY, T_ANY,   "Tracker spotted. Probably someone's keys. Probably." },
    { EV_AVIAN, T_ANY,   "Drone overhead. Huginn and Muninn, reporting to some Odin." },
    { EV_AVIAN, T_ANY,   "{d} in the sky. I'd throw a rock, but no arms." },
    { EV_HERALD, T_ANY,  "A beacon. Shouting its name into the void. Relatable." },
    { EV_FOUL, T_ANY,    "{d}. That's how skimmers whisper. Guard your card." },
    { EV_FOUL, T_ANY,    "Something foul nearby. Check card readers before you swipe." },
    { EV_JAKI, T_ANY,    "{d}. An ear on a pole. The amanojaku reads hearts; this hears bangs." },
    { EV_JAKI, T_ANY,    "Acoustic sensor. Keep your voice down. Or don't. It's listening anyway." },
    { EV_FIEND, T_ANY,   "Roll initiative. {d} nearby." },
    { EV_FIEND, T_ANY,   "{d}. Hostile. I'm watching it. Closely." },
    { EV_FIEND, T_ANY,   "Hostile signal. Cute tool. Mine's bigger." },

    // ── state changes ──
    { EV_CLEAR,   T_ANY, "Threat gone. Fear of me, probably." },
    { EV_CLEAR,   T_ANY, "Hostiles fled. Natural 20 on Intimidation." },
    { EV_GPS_FIX, T_ANY, "Satellites found. Even the heavens report to me." },
    { EV_GPS_FIX, T_ANY, "Coordinates locked. I know where we are. Do you?" },
    { EV_SCRY_ON, T_ANY, "Scrying. Every network leaves a trace." },
    { EV_SCRY_ON, T_ANY, "The map fills itself in. Like a proper dungeon crawler." },
    { EV_WIFI_JOIN, T_ANY, "Network joined. I'll keep an eye on it. Both of them. Metaphorically." },
    { EV_SINCERE, T_ANY, "...That was handled well. Don't let it go to your head." },
    { EV_SINCERE, T_ANY, "Good call stepping away. I mean it." },
};

static constexpr int BANK_N = sizeof(BANK) / sizeof(BANK[0]);

} // namespace Lines
