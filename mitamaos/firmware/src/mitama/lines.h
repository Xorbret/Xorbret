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
//        {n} how many of that race are active now, {r} race name.
#include <stdint.h>
#include "../detect/signatures.h"

namespace Lines {

enum Event : uint8_t {
    EV_BOOT, EV_IDLE, EV_LORE, EV_OFFLINE,
    // a new sighting, by race (Sig::Race order)
    EV_VILE, EV_NIGHT, EV_FAIRY, EV_AVIAN, EV_HERALD, EV_FOUL, EV_JAKI, EV_FIEND,
    EV_CLEAR,      // last threat gone
    EV_GPS_FIX, EV_SCRY_ON, EV_WIFI_JOIN,
    EV_SINCERE,    // rare, after a threat passes
    EV_NEW_DEMON,  // a species registered in the Compendium for the first time
    EV_RACE_DONE,  // ...and that finished its race
    EV_WHISPER,    // a Kotodama (LoRa chat) message arrived; {d} = sender
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
    { EV_VILE, T_ANY, "{sp} logging plates. Every car gets a page in its book." },
    { EV_VILE, T_ANY, "Surveillance pole ahead. It never blinks. Neither do I." },
    { EV_VILE, T_ANY, "{d} in range. The state keeps a ledger. You're a line in it." },
    { EV_VILE, T_ANY, "{d} nearby. A machine that never forgets. Neither do I." },
    { EV_VILE, T_ANY, "Surveillance gear close. You've done nothing wrong. Still noted." },
    { EV_VILE, T_ANY, "{d}. Big Brother has an API now." },
    { EV_VILE, T_ANY, "Something official is watching. I'm watching it back." },
    { EV_NIGHT, T_ANY,   "A camera. Beholder energy: one big eye, zero chill." },
    { EV_NIGHT, T_ANY,   "{d} spotted. Someone's playing Big Brother on easy mode." },
    { EV_NIGHT, T_ANY,   "{sp} in range. Don't look into it. Very Medusa." },
    { EV_NIGHT, T_ANY, "{d}. An eye that never sleeps. Rude. That's my job." },
    { EV_NIGHT, T_ANY, "{d} in range. Medusa needed eye contact. This just needs Wi-Fi." },
    { EV_NIGHT, T_ANY, "Another camera. All the world's a stage. Everyone's on stream." },
    { EV_NIGHT, T_ANY, "{d}. Watching, recording, uploading. Three verbs I distrust." },
    { EV_NIGHT, T_ANY, "Camera nearby. Beholders have ten eyes. Cities have thousands." },
    { EV_NIGHT, T_ANY, "{d} spotted. Smile for the cloud server." },
    { EV_NIGHT, T_ANY, "A watcher. I'm a watcher too, but I keep secrets." },
    { EV_FAIRY, T_ANY,   "{sp} drifting with us. Fairies lead travelers astray. This one follows." },
    { EV_FAIRY, T_ANY,   "{d} nearby. If it's still with us later, we talk." },
    { EV_FAIRY, T_ANY,   "Tracker spotted. Probably someone's keys. Probably." },
    { EV_FAIRY, T_ANY, "{d} close by. Lost keys, or a leash. Time will tell." },
    { EV_FAIRY, T_ANY, "A tracker nearby. If it follows us across town, we talk." },
    { EV_FAIRY, T_ANY, "{d} nearby. Fairies stole babies. This one steals locations." },
    { EV_FAIRY, T_ANY, "Tracker in range. I'm noting it. If it lingers, you'll know." },
    { EV_FAIRY, T_ANY, "{d}. Small, quiet, loyal to someone. Maybe not you." },
    { EV_FAIRY, T_ANY, "Another tracker. The world is a bag of tagged keys." },
    { EV_FAIRY, T_ANY, "{d} spotted. A will-o'-wisp with a battery." },
    { EV_FAIRY, T_ANY, "Tracker ping. It's lost, or it's hunting. Fifty-fifty." },
    { EV_FAIRY, T_ANY, "{d}. I see it. It doesn't see me. Stealth check passed." },
    { EV_FAIRY, T_ANY, "Trackers whisper where things are. I whisper back: heard that." },
    { EV_AVIAN, T_ANY,   "Drone overhead. Huginn and Muninn, reporting to some Odin." },
    { EV_AVIAN, T_ANY,   "{d} in the sky. I'd throw a rock, but no arms." },
    { EV_AVIAN, T_ANY, "Something's flying. {d}. Watch the sky. I'll watch the rest." },
    { EV_AVIAN, T_ANY, "Drone signal. An eye with propellers. The sky isn't private." },
    { EV_AVIAN, T_ANY, "{d} broadcasting its ID. At least this bird introduces itself." },
    { EV_AVIAN, T_ANY, "Airborne camera. Don't wave. It'll think you're friendly." },
    { EV_HERALD, T_ANY,  "A beacon. Shouting its name into the void. Relatable." },
    { EV_HERALD, T_ANY, "iBeacon nearby. A shop saying: I see you. Buy things." },
    { EV_HERALD, T_ANY, "Beacon pinging. Some store wants to know which aisle you're in." },
    { EV_HERALD, T_ANY, "{d}. A herald with nothing to announce. Every second." },
    { EV_HERALD, T_ANY, "A beacon. It knows when you walk past. That's all it knows." },
    { EV_HERALD, T_ANY, "Retail beacon. Mostly harmless. Mostly nosy." },
    { EV_FOUL, T_ANY,    "{d}. That's how skimmers whisper. Guard your card." },
    { EV_FOUL, T_ANY,    "Something foul nearby. Check card readers before you swipe." },
    { EV_FOUL, T_ANY, "Possible skimmer. Tug the card slot. If it wiggles, walk away." },
    { EV_FOUL, T_ANY, "{d} nearby. Skimmers love those. Tap or pay cash here." },
    { EV_FOUL, T_ANY, "Bluetooth where none belongs. Check the pump. Check the ATM." },
    { EV_FOUL, T_ANY, "Skimmer signal. A mimic in a card reader. Don't feed it." },
    { EV_FOUL, T_ANY, "Something foul close by. Cover the keypad when you type your PIN." },
    { EV_JAKI, T_ANY,    "{d}. An ear on a pole. The amanojaku reads hearts; this hears bangs." },
    { EV_JAKI, T_ANY,    "Acoustic sensor. Keep your voice down. Or don't. It's listening anyway." },
    { EV_JAKI, T_ANY, "Gunshot sensor. It hears everything loud. Whisper your secrets." },
    { EV_JAKI, T_ANY, "Odin's ravens report thoughts. This Raven reports bangs." },
    { EV_JAKI, T_ANY, "Acoustic sensor overhead. The street has ears tonight." },
    { EV_JAKI, T_ANY, "{d}. Always listening. Like me, but with a city contract." },
    { EV_FIEND, T_ANY,   "Roll initiative. {d} nearby." },
    { EV_FIEND, T_ANY,   "{d}. Hostile. I'm watching it. Closely." },
    { EV_FIEND, T_ANY,   "Hostile signal. Cute tool. Mine's bigger." },
    { EV_FIEND, T_ANY, "Hostile tool active. I'm not scared. Stay close anyway." },

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

    // ── Compendium ──
    { EV_NEW_DEMON, T_ANY, "New entry: {sp}. The Compendium grows. So does my judgment." },
    { EV_NEW_DEMON, T_ANY, "{sp} registered. Gotta catch... no. We don't say that here." },
    { EV_NEW_DEMON, T_ANY, "First {sp} ever. Logged, sealed, filed under 'concerning'." },
    { EV_NEW_DEMON, T_ANY, "Compendium updated: {sp}. I wrote its page in my best hand." },
    { EV_NEW_DEMON, T_ANY, "A new demon for the book: {sp}. Bestiary collectors would weep." },
    { EV_NEW_DEMON, T_ANY, "{sp}. Never met one before. Now I'll never forget it." },
    { EV_RACE_DONE, T_ANY, "Every {r} logged. A complete set. Congratulations, I suppose." },
    { EV_RACE_DONE, T_ANY, "{r} complete. Somewhere a completionist sheds a single tear." },
    { EV_RACE_DONE, T_ANY, "The whole {r} race is in the book. Achievement unlocked, sort of." },
    { EV_RACE_DONE, T_ANY, "All {r} registered. That page of the Compendium is full." },
    { EV_WHISPER, T_ANY, "A whisper on the wind from {d}. Kotodama's waiting." },
    { EV_WHISPER, T_ANY, "{d} sent word over LoRa. Spirits gossip too." },
    { EV_WHISPER, T_ANY, "Message from {d}, carried over the hills by spirit-speech." },
    { EV_WHISPER, T_ANY, "{d} speaks through the Kotodama. Going to answer?" },
};

static constexpr int BANK_N = sizeof(BANK) / sizeof(BANK[0]);

// Device-specific sighting lines: offered alongside the family's lines when
// that exact species is seen, so an AirTag and a Tile don't sound the same.
struct SpLine { uint8_t sp; const char* text; };

static const SpLine SP_BANK[] = {
    { Sig::T_FLOCK, "Flock camera. It keeps your plate for weeks. I keep grudges longer." },
    { Sig::T_FLOCK, "A Flock. Ironic name. It stands alone and watches everyone." },
    { Sig::T_FLOCK, "Flock camera. Your drive is now a dataset." },
    { Sig::T_FLOCK, "A Flock pole. Solar powered. Fueled by license plates." },
    { Sig::T_FLOCK, "Flock nearby. Plates logged, searchable, shared across towns." },
    { Sig::T_FLOCK, "Flock camera. It doesn't need a warrant to remember you." },
    { Sig::T_FLOCK, "A Flock. A stone gargoyle with a cellular plan." },
    { Sig::T_FLOCK, "Flock in range. Every car leaves a breadcrumb trail." },
    { Sig::T_FLOCK, "Flock camera. A toll booth that charges in privacy." },
    { Sig::T_AXON, "Axon gear. Body cam or taser dock. Someone official is near." },
    { Sig::T_AXON, "Axon signal. Police tech close by. Act natural. You're bad at it." },
    { Sig::T_AXON, "Axon nearby. Police are close. Stay calm. I'm with you." },
    { Sig::T_AXON, "Body cam signal. Officers nearby. Hands visible, voice steady." },
    { Sig::T_AXON, "Axon gear. Cops in range. You have the right to stay quiet." },
    { Sig::T_AXON, "Police tech close by. You can ask: am I free to go?" },
    { Sig::T_AXON, "Axon signal. A body cam is recording. You can too, where legal." },
    { Sig::T_AXON, "Cops nearby. I can't stop them. I can tell you they're here." },
    { Sig::T_AXON, "Axon in range. Taser dock or body cam. Neither is a friend." },
    { Sig::T_AXON, "Police nearby. Don't consent to a search you don't want. Say so." },
    { Sig::T_AXON, "Body cam close. Their footage, their edit. Remember what you saw." },
    { Sig::T_AXON, "Axon ping. The city guard is out. Keep moving, keep calm." },
    { Sig::T_AXON, "Axon gear nearby. Power watches you. I watch power." },
    { Sig::T_AXON, "Police signal. You owe them no small talk." },
    { Sig::T_AXON, "Body cam in range. Keep your own record of what happens." },
    { Sig::T_AXON, "Axon close. The guards are near. Walk like a background NPC." },
    { Sig::T_ALPR, "Plate reader. Your car just got read like a scroll." },
    { Sig::T_ALPR, "Plate reader in range. It reads faster than you skip quest text." },
    { Sig::T_ALPR, "Plate reader. Your car's name tag just got read." },
    { Sig::T_ALPR, "ALPR nearby. Thousands of plates an hour. Speed reading, but evil." },
    { Sig::T_ALPR, "Plate reader. Parked or passing, you're in the log now." },
    { Sig::T_ALPR, "An ALPR. A sphinx with no riddles. It just wants your plate." },
    { Sig::T_ALPR, "Plate reader in range. Patrol cars carry these too. Drive boring." },
    { Sig::T_ALPR, "ALPR. The road remembers. I'd rather it didn't." },
    { Sig::T_ALPR, "Plate camera. It reads in the dark. Infrared. Show-off." },
    { Sig::T_CAMERA, "Wi-Fi camera nearby. Somewhere a stranger is watching a stream." },
    { Sig::T_CAMERA, "Camera on the network. A scrying pool, but cheaper." },
    { Sig::T_CAMERA, "Wi-Fi camera in range. Probably a baby monitor. Probably." },
    { Sig::T_CAMERA, "Camera on Wi-Fi. Default password? Then it's everyone's camera." },
    { Sig::T_CAMERA, "Camera nearby. Wyze, Eufy, someone. Watching something." },
    { Sig::T_CAMERA, "Network camera. A crystal ball with a cloud subscription." },
    { Sig::T_CAMERA, "A camera's talking to the router. Describing you, in pixels." },
    { Sig::T_CAMERA, "Wi-Fi cam spotted. Indoors or out, it's watching a door." },
    { Sig::T_CAMERA, "Camera in range. A sentry with better Perception than you." },
    { Sig::T_CAMERA, "Another lens online. Panopticon, community edition." },
    { Sig::T_RING, "Ring doorbell. Every porch a watchtower. Every neighbor a guard." },
    { Sig::T_RING, "A Ring doorbell. The porch has eyes now. Wave, or don't." },
    { Sig::T_RING, "Ring doorbell. Ding dong. You've been archived." },
    { Sig::T_RING, "A Ring. It saw you before you saw it. Rude." },
    { Sig::T_RING, "Ring camera. Footage like this can end up with police." },
    { Sig::T_RING, "Ring doorbell. Neighborhood watch, now with cloud storage." },
    { Sig::T_RING, "A Ring. The porch guardian. It barks in notifications." },
    { Sig::T_RING, "Ring nearby. Every delivery recorded. Every passerby too." },
    { Sig::T_RING, "Ring doorbell. Amazon's eye on the door. Keep walking." },
    { Sig::T_RING, "A Ring. Gatekeeper of a porch. You're not on the list." },
    { Sig::T_META, "Smart glasses nearby. Someone's face is a camera. Stay mysterious." },
    { Sig::T_META, "Meta glasses. If the little light is on, they're recording." },
    { Sig::T_META, "Meta glasses. The future: someone filming you while saying hi." },
    { Sig::T_META, "Smart glasses in range. Someone's streaming their walk. Maybe you." },
    { Sig::T_META, "Meta glasses nearby. Eye contact, now with upload." },
    { Sig::T_META, "Camera glasses. A spy's gadget, sold at the mall." },
    { Sig::T_META, "Meta glasses. The capture light is small. Look for it." },
    { Sig::T_META, "Smart specs nearby. Seeing through someone's eyes, literally." },
    { Sig::T_META, "Meta glasses. If the wearer talks to them, an AI is listening." },
    { Sig::T_META, "Glasses with a camera. A scrying lens, consumer grade." },
    { Sig::T_AIRTAG, "AirTag nearby. Apple's fairy. It whispers to every iPhone it passes." },
    { Sig::T_AIRTAG, "AirTag. If it isn't yours and it tags along, check your bag." },
    { Sig::T_AIRTAG, "AirTag in range. A coin-sized familiar bound to an Apple ID." },
    { Sig::T_AIRTAG, "AirTag. Lost luggage, or someone's leash. Let's find out." },
    { Sig::T_AIRTAG, "An AirTag. If it beeps from your bag, it's been with you a while." },
    { Sig::T_AIRTAG, "AirTag spotted. Your iPhone would warn you. I warn you faster." },
    { Sig::T_AIRTAG, "AirTag nearby. Tiny, white, smug. I respect the confidence." },
    { Sig::T_TILE, "Tile tracker. Square, quiet, and very interested in where we go." },
    { Sig::T_TILE, "Tile in range. The original key finder. A veteran of couches." },
    { Sig::T_TILE, "Tile tracker. It finds keys. It can also find you." },
    { Sig::T_TILE, "A Tile. Old guild, new tricks. Keep an eye on it." },
    { Sig::T_TILE, "Tile nearby. It chirps when summoned. Like a pet. Like a spy." },
    { Sig::T_TILE, "Tile tracker. Its app hears every phone it passes. Gossip network." },
    { Sig::T_TILE, "A Tile. Square familiar, loyal to whoever paid for it." },
    { Sig::T_SAMSUNG_TAG, "SmartTag. Samsung's familiar. It reports to Galaxy phones." },
    { Sig::T_SAMSUNG_TAG, "SmartTag nearby. Samsung's watcher. It reports to the Galaxy." },
    { Sig::T_SAMSUNG_TAG, "A SmartTag. Every Galaxy phone is its carrier pigeon." },
    { Sig::T_SAMSUNG_TAG, "SmartTag in range. Your Android might warn you. I already did." },
    { Sig::T_SAMSUNG_TAG, "Samsung tag. Small, round, and very interested in us." },
    { Sig::T_SAMSUNG_TAG, "SmartTag spotted. If it shows up at home too, we talk." },
    { Sig::T_SAMSUNG_TAG, "A SmartTag. The Galaxy's little scout. Report noted." },
    { Sig::T_GOOGLE_TAG, "Google tag. Every Android it passes carries its messages." },
    { Sig::T_GOOGLE_TAG, "Google tag nearby. Android's familiar. It hitches rides on phones." },
    { Sig::T_GOOGLE_TAG, "A Google Find tag. Every passing Android carries its whispers." },
    { Sig::T_GOOGLE_TAG, "Google tag in range. Search engine, now with legs." },
    { Sig::T_GOOGLE_TAG, "Google tracker. It knows where it is. Soon, where you are." },
    { Sig::T_GOOGLE_TAG, "Find tag spotted. Chipolo or Pebblebee, maybe. Google's network." },
    { Sig::T_GOOGLE_TAG, "Google tag nearby. If it follows us, your phone should warn you." },
    { Sig::T_DEAUTH, "Deauth flood. Someone's kicking people off Wi-Fi. Petty magic." },
    { Sig::T_DEAUTH, "Wi-Fi under attack. If you drop off, that's why. Use mobile data." },
    { Sig::T_EVILTWIN, "Evil twin network. Same name, wrong soul. Don't join it." },
    { Sig::T_EVILTWIN, "A doppelganger Wi-Fi. If it asks for your password, it's lying." },
    { Sig::T_HACKER, "{d} nearby. A hacker's toy. Keep your phone locked." },
    { Sig::T_HACKER, "{d} in range. Cute gadget. Sharp intentions." },
};

static constexpr int SP_N = sizeof(SP_BANK) / sizeof(SP_BANK[0]);

} // namespace Lines
