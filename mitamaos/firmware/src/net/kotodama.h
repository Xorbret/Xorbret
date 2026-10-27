#pragma once
// Kotodama - LoRa text chat over the Cap LoRa-1262 (SX1262), compatible
// with Meshtastic's LongFast channel (see mesh_proto.h).
//
// Hardware: the SX1262 shares the SD card's SPI bus (SCK 40, MISO 39,
// MOSI 14) with its own chip select on G5, plus IRQ G4, reset G3, busy
// G6. Its RF switch is pin P0 of the cap's PI4IOE5V6408 I/O expander
// (0x43 on the internal I2C bus); finding the expander is how we know the
// cap is fitted.
//
// A radio task (core 0) listens continuously while Kotodama is on, so
// messages arrive whichever app is open, and sends what the chat screen
// queues. It's a quiet node: it never relays other people's packets.
// Messages are kept in RAM (the newest MAX_MSGS per channel) and appended
// to /MitamaOS/kotodama/log.txt by the main loop (tick()). Apps that need
// all the RAM (see appChanged()) make it stand down until they close.
#include <Arduino.h>
#include <new>
#include <M5Cardputer.h>
#include <RadioLib.h>
#include <SD.h>
#include <Preferences.h>
#include <time.h>
#include "mesh_proto.h"
#include "../system/sd_bus.h"
#include "../system/sd_settings.h"
#include "../system/crash_log.h"

class Kotodama {
public:
    static Kotodama& get() { static Kotodama k; return k; }

    struct Msg {
        uint32_t from = 0, unix = 0;
        uint8_t  ch = 0;
        bool     mine = false, logged = false;
        int16_t  rssi = 0;
        int8_t   snr = 0;
        char     text[Mesh::MAX_TEXT + 1] = "";
    };
    // A Mitama's card, shared with the crew: which form it wears and how far
    // its Compendium has come. Sent after our name on the Mitama channel.
    struct Card {
        uint8_t  form = 0, forms = 0;            // worn form, forms unlocked
        uint8_t  species = 0, speciesTotal = 0;  // Compendium pages filled / total
        uint8_t  races = 0;                      // bit per race (Sig::Race) completed
        uint32_t encounters = 0;
        uint8_t  level = 0;                      // ki level (0 = sent by a build without ki)
    };
    struct Node {
        uint32_t num = 0; char shortName[5] = ""; char longName[25] = "";
        bool     hasCard = false;
        Card     card;
        uint32_t heardMs = 0;    // millis() of the last packet from it (0 = only named)
        int16_t  rssi = 0;
        bool     crew = false;   // heard on the Mitama channel: another MitamaOS user
    };
    // One row of the Crew list: friends first (heard or not), then other
    // Mitama heard this session, most recent first.
    struct CrewRow {
        uint32_t num = 0; char name[25] = ""; uint32_t heardMs = 0; int16_t rssi = 0; bool isFriend = false;
        bool hasCard = false; Card card;
    };
    // Someone from the crew came into range (first time, or back after 30 min).
    struct Arrival { uint32_t num = 0; bool isFriend = false; uint32_t ms = 0; };

    static constexpr int MAX_MSGS = 16, MAX_NODES = 24;     // MAX_MSGS per channel
    static constexpr int MAX_FRIENDS = 16;

    enum State : uint8_t { ST_OFF, ST_NO_REGION, ST_NO_CAP, ST_RADIO_FAIL, ST_ON };

    // ── settings (NVS "kotodama", mirrored to settings.conf) ──
    uint8_t region() { _loadPrefs(); return _region; }
    const char* longName() { _loadPrefs(); return _long; }
    const char* shortName() { _loadPrefs(); return _short; }
    bool wantOn() { _loadPrefs(); return _on; }

    void setRegion(uint8_t r) {
        if (r >= Mesh::REG_COUNT) r = Mesh::REG_UNSET;
        _loadPrefs(); _region = r;
        Preferences p; p.begin("kotodama", false); p.putUChar("region", r); p.end();
        SdSettings::changed();
        if (_state == ST_ON) { stop(); start(); }
        else if (_on) start();              // was waiting for a region
    }
    void setName(const char* name) {
        _loadPrefs();
        strncpy(_long, name, sizeof(_long) - 1); _long[sizeof(_long) - 1] = 0;
        Preferences p; p.begin("kotodama", false); p.putString("name", _long); p.end();
        SdSettings::changed();
        _announceAt = millis();             // tell the mesh our new name
    }
    void setOn(bool on) {
        _loadPrefs(); _on = on;
        Preferences p; p.begin("kotodama", false); p.putUChar("on", on); p.end();
        SdSettings::changed();
        if (on && !_heavy) start(); else stop();
    }

    // Called from the main loop when the foreground app changes. The apps
    // that need every byte (as for the detection watcher) make the radio
    // stand down and free its buffers; it comes back when they close.
    void appChanged(const char* name) {
        // Molt too: its download needs the RAM for TLS and the SD card to itself.
        static const char* HEAVY[] = { "MiTalk", "PaperWeb", "Store", "Games", "Lua", "Molt" };
        bool heavy = false;
        for (auto h : HEAVY) if (strcmp(name, h) == 0) heavy = true;
        if (heavy == _heavy) return;
        _heavy = heavy;
        if (heavy) {
            if (_state == ST_ON) { stop(); _resume = true; }
            tick();                          // log what we have before freeing it
            portENTER_CRITICAL(&_mux);
            Msg* m = _msgs; _msgs = nullptr;
            for (auto& r : _ring) r.head = r.count = 0;
            portEXIT_CRITICAL(&_mux);
            delete[] m;
        } else if (_resume) { _resume = false; if (wantOn()) start(); }
    }

    // At boot: starts the radio if it was left on.
    void boot() { if (wantOn()) start(); }

    void start() {
        if (_state == ST_ON) return;
        _loadPrefs();
        _Lock lk(this);
        if (_region == Mesh::REG_UNSET) { _state = ST_NO_REGION; return; }
        if (!_capFitted()) { _state = ST_NO_CAP; return; }
        if (!_msgs) {
            Msg* m = new (std::nothrow) Msg[MAX_MSGS * Mesh::CH_COUNT];
            portENTER_CRITICAL(&_mux); _msgs = m; portEXIT_CRITICAL(&_mux);
        }
        if (!_msgs || !SdBus::spi) { _state = ST_RADIO_FAIL; return; }
        _rfSwitch(true);
        if (!_radio) _radio = new SX1262(new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY, *SdBus::spi,
                                                    SPISettings(2000000, MSBFIRST, SPI_MODE0)));
        const auto& R = Mesh::region(_region);
        int st = _radio->begin(Mesh::frequency(_region), Mesh::BW_KHZ, Mesh::SF, Mesh::CR, Mesh::SYNC,
                               R.power, Mesh::PREAMBLE, 3.0f, true);
        if (st != RADIOLIB_ERR_NONE) { _err = st; _state = ST_RADIO_FAIL; _rfSwitch(false); return; }
        _radio->setCurrentLimit(140);
        _radio->setDio1Action(_isr);
        _rxFlag = false;
        _radio->startReceive();
        _state = ST_ON;
        _announceAt = millis() + 3000;      // say who we are shortly after starting
        _crewAt = millis() + CREW_MS;
        // The task ends itself when the radio stops (frees its stack); the
        // lock makes "is it still running?" and its exit one step.
        if (!_task) xTaskCreatePinnedToCore(_taskFn, "kotodama", 8192, this, 1, &_task, 0);
    }

    // Waits for the radio task to finish whatever it's sending (a few
    // seconds at most), so the two never drive the chip at once.
    void stop() {
        _stopping = true;
        _Lock lk(this);
        if (_state == ST_ON) {
            _radio->sleep();
            _rfSwitch(false);
        }
        if (_txPending) _txFailed = true;  // tell the chat it never went out
        _txPending = false;              // don't send a stale message next time
        _state = ST_OFF;
        _stopping = false;
    }

    State state() const { return _state; }
    int lastError() const { return _err; }
    uint32_t nodeNum() {
        if (!_node) {
            uint64_t mac = ESP.getEfuseMac();      // bytes LSB first: mac[0] is the low byte
            uint8_t m[6]; for (int i = 0; i < 6; i++) m[i] = (mac >> (8 * i)) & 0xFF;
            _node = (uint32_t)m[2] << 24 | (uint32_t)m[3] << 16 | (uint32_t)m[4] << 8 | m[5];
        }
        return _node;
    }

    // Queues a text for the radio task. False if one is already waiting.
    bool send(uint8_t ch, const char* text) {
        if (_state != ST_ON || _txPending) return false;
        strncpy(_txText, text, Mesh::MAX_TEXT); _txText[Mesh::MAX_TEXT] = 0;
        _txCh = ch;
        _txPending = true;
        _sentN++;
        return true;
    }
    bool sending() const { return _txPending; }
    uint32_t sentCount() const { return _sentN; }   // messages queued since boot
    uint32_t crewNewCount() const { return _crewNewN; }   // crew first heard since boot
    int friendCount() const { return _friendsLoaded ? _nFriends : -1; }   // -1 until friends.txt is read
    // Set when the last queued message failed to go out; read once.
    bool takeSendFailed() { bool f = _txFailed; _txFailed = false; return f; }

    // One channel's messages, oldest first (up to MAX_MSGS).
    int messages(Msg* out, int max, uint8_t ch) {
        int n = 0;
        if (ch >= Mesh::CH_COUNT) return 0;
        portENTER_CRITICAL(&_mux);
        if (_msgs) {
            const _Ring& r = _ring[ch];
            for (int k = 0; k < r.count && n < max; k++) out[n++] = *_at(ch, k);
        }
        portEXIT_CRITICAL(&_mux);
        return n;
    }
    uint32_t serial() const { return _serial; }       // changes with every new message
    int unread(uint8_t ch) const { return _unread[ch]; }
    void markRead(uint8_t ch) { _unread[ch] = 0; }

    // Display name for a node: its long name if we've heard it, else !abcd.
    void nameOf(uint32_t num, char* out, size_t cap) {
        if (num == nodeNum()) { strncpy(out, longName(), cap - 1); out[cap - 1] = 0; return; }
        portENTER_CRITICAL(&_mux);
        for (auto& n : _nodes)
            if (n.num == num && n.longName[0]) {
                strncpy(out, n.longName, cap - 1); out[cap - 1] = 0;
                portEXIT_CRITICAL(&_mux);
                return;
            }
        portEXIT_CRITICAL(&_mux);
        snprintf(out, cap, "!%04x", (unsigned)(num & 0xFFFF));
    }

    // ── Crew: other Mitama in radio range, and your friends ──
    int crew(CrewRow* out, int cap) {
        int n = 0;
        portENTER_CRITICAL(&_mux);
        for (int i = 0; i < _nFriends && n < cap; i++) {
            CrewRow& r = out[n++];
            r = CrewRow();
            r.num = _friends[i].num; r.isFriend = true;
            strncpy(r.name, _friends[i].name, sizeof(r.name) - 1);
            const Node* nd = _find(r.num);
            if (nd) {
                r.heardMs = nd->heardMs; r.rssi = nd->rssi; r.hasCard = nd->hasCard; r.card = nd->card;
                if (nd->longName[0]) strncpy(r.name, nd->longName, sizeof(r.name) - 1);
            }
        }
        int firstOther = n;
        for (auto& nd : _nodes) {
            if (n >= cap) break;
            if (!nd.num || !nd.crew || _friendIdx(nd.num) >= 0) continue;
            CrewRow r; r.num = nd.num; r.heardMs = nd.heardMs; r.rssi = nd.rssi; r.hasCard = nd.hasCard; r.card = nd.card;
            if (nd.longName[0]) strncpy(r.name, nd.longName, sizeof(r.name) - 1);
            else snprintf(r.name, sizeof(r.name), "!%04x", (unsigned)(nd.num & 0xFFFF));
            int k = n++;   // insertion sort, most recent first
            while (k > firstOther && (int32_t)(out[k - 1].heardMs - r.heardMs) < 0) { out[k] = out[k - 1]; k--; }
            out[k] = r;
        }
        portEXIT_CRITICAL(&_mux);
        return n;
    }
    bool cardOf(uint32_t num, Card& out) {
        portENTER_CRITICAL(&_mux);
        const Node* n = _find(num);
        bool ok = n && n->hasCard;
        if (ok) out = n->card;
        portEXIT_CRITICAL(&_mux);
        return ok;
    }
    // Our own card, kept fresh by the brain; sent with our name to the crew.
    void setMyCard(const Card& c) {
        portENTER_CRITICAL(&_mux);
        _myCard = c; _hasMyCard = true;
        portEXIT_CRITICAL(&_mux);
    }
    bool isFriend(uint32_t num) {
        portENTER_CRITICAL(&_mux);
        bool f = _friendIdx(num) >= 0;
        portEXIT_CRITICAL(&_mux);
        return f;
    }
    // Adds or removes a friend. False when the list is full.
    bool setFriend(uint32_t num, bool on) {
        char name[25];
        nameOf(num, name, sizeof(name));
        bool ok = true;
        portENTER_CRITICAL(&_mux);
        int i = _friendIdx(num);
        if (on && i < 0) {
            if (_nFriends < MAX_FRIENDS) {
                _friends[_nFriends].num = num;
                strncpy(_friends[_nFriends].name, name, sizeof(_friends[0].name) - 1);
                _friends[_nFriends].name[sizeof(_friends[0].name) - 1] = 0;
                _nFriends++;
                _friendsDirty = true;
            } else ok = false;
        } else if (!on && i >= 0) {
            memmove(&_friends[i], &_friends[i + 1], sizeof(_friends[0]) * (_nFriends - i - 1));
            _nFriends--;
            _friendsDirty = true;
        }
        portEXIT_CRITICAL(&_mux);
        return ok;
    }
    // Handed out a few seconds late, so the card that follows a name has
    // usually arrived too (see cardOf).
    bool takeArrival(Arrival& out) {
        portENTER_CRITICAL(&_mux);
        // Arrivals nobody announced in time (chatter off) are old news.
        while (_nArrivals > 0 && millis() - _arrivals[0].ms > 120000)
            memmove(_arrivals, _arrivals + 1, sizeof(Arrival) * --_nArrivals);
        bool had = _nArrivals > 0 && millis() - _arrivals[0].ms > 4000;
        if (had) { out = _arrivals[0]; memmove(_arrivals, _arrivals + 1, sizeof(Arrival) * --_nArrivals); }
        portEXIT_CRITICAL(&_mux);
        return had;
    }

    // Newest incoming text, for the Mitama to mention (once).
    bool takeIncoming(Msg& out) {
        portENTER_CRITICAL(&_mux);
        bool had = _incoming;
        if (had) { out = _lastIn; _incoming = false; }
        portEXIT_CRITICAL(&_mux);
        return had;
    }

    // Main loop: load / save the friend list, append new messages to the SD log.
    void tick() {
        _friendsTick();
        if (!_msgs || _serial == _loggedSerial) return;
        _loggedSerial = _serial;
        _Crumb cr("kotodama log");
        if (SD.cardType() == CARD_NONE) return;
        if (!SD.exists("/MitamaOS/kotodama")) SD.mkdir("/MitamaOS/kotodama");
        File f = SD.open("/MitamaOS/kotodama/log.txt", FILE_APPEND);
        if (!f) return;
        Msg m;
        for (int k = 0; k < MAX_MSGS * Mesh::CH_COUNT; k++) {
            bool found = false;
            portENTER_CRITICAL(&_mux);
            for (uint8_t c = 0; _msgs && c < Mesh::CH_COUNT && !found; c++)
                for (int j = 0; j < _ring[c].count; j++) {
                    Msg* x = _at(c, j);
                    if (!x->logged) { x->logged = true; m = *x; found = true; break; }
                }
            portEXIT_CRITICAL(&_mux);
            if (!found) break;
            char who[28], when[24] = "?";
            nameOf(m.from, who, sizeof(who));
            if (m.unix) { time_t t = m.unix; struct tm tm; localtime_r(&t, &tm); strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &tm); }
            f.printf("%s [%s] %s: %s\n", when, Mesh::channel(m.ch).name, who, m.text);
        }
        f.close();
    }

private:
    static constexpr int LORA_CS = 5, LORA_IRQ = 4, LORA_RST = 3, LORA_BUSY = 6;
    static constexpr uint8_t IOE_ADDR = 0x43;
    static constexpr uint32_t ANNOUNCE_MS = 3UL * 3600 * 1000;   // Meshtastic's own default
    // A short "still here" on the Mitama channel only, so crew in range can
    // see each other (LongFast keeps Meshtastic's 3 h).
    static constexpr uint32_t CREW_MS = 15UL * 60 * 1000;
    static constexpr uint32_t CREW_AWAY_MS = 30UL * 60 * 1000;   // gone this long = "arrives" again
    static constexpr const char* FRIENDS = "/MitamaOS/kotodama/friends.txt";
    static constexpr const char* FRIENDS_TMP = "/MitamaOS/kotodama/friends.tmp";

    // Marks what the radio is doing for the crash log, until scope end.
    struct _Crumb { _Crumb(const char* w) { CrashLog::crumb(w); } ~_Crumb() { CrashLog::done(); } };

    SX1262*      _radio = nullptr;
    TaskHandle_t _task = nullptr;
    volatile State _state = ST_OFF;
    int          _err = 0;
    uint32_t     _node = 0;
    static inline volatile bool _rxFlag = false;
    volatile bool _stopping = false, _txFailed = false;
    bool         _heavy = false, _resume = false;   // stood down for a heavy app
    SemaphoreHandle_t _radioLock = nullptr;
    struct _Lock {
        SemaphoreHandle_t m;
        _Lock(Kotodama* k) {
            if (!k->_radioLock) k->_radioLock = xSemaphoreCreateMutex();
            m = k->_radioLock; xSemaphoreTake(m, portMAX_DELAY);
        }
        ~_Lock() { if (m) xSemaphoreGive(m); }
    };
    uint32_t _replyAt = 0, _lastReply = 0;       // answering a Mitama's "who's there?"

    bool    _prefsLoaded = false, _on = false;
    uint8_t _region = Mesh::REG_UNSET;
    char    _long[25] = "", _short[5] = "";

    volatile bool _txPending = false;
    uint32_t _sentN = 0;
    char     _txText[Mesh::MAX_TEXT + 1];
    uint8_t  _txCh = 0;
    uint32_t _announceAt = 0;

    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    Msg*     _msgs = nullptr;                    // MAX_MSGS per channel, back to back
    struct _Ring { int head = 0, count = 0; } _ring[Mesh::CH_COUNT];
    volatile uint32_t _serial = 0;
    uint32_t _loggedSerial = 0;
    volatile int _unread[Mesh::CH_COUNT] = {0, 0};
    Node     _nodes[MAX_NODES];
    struct Friend { uint32_t num; char name[25]; } _friends[MAX_FRIENDS] = {};
    int      _nFriends = 0;
    bool     _friendsLoaded = false, _friendsDirty = false;
    uint32_t _crewNewN = 0;
    Arrival  _arrivals[4];
    int      _nArrivals = 0;
    uint32_t _crewAt = 0;
    Card     _myCard;
    bool     _hasMyCard = false;
    // Version 1 is 10 bytes; byte 10 (the ki level) was added on the end,
    // so older builds read the first 10 and ignore it.
    static constexpr size_t CARD_LEN = 11, CARD_MIN = 10;
    static void _packCard(const Card& c, uint8_t* b) {
        b[0] = 1;   // card version
        b[1] = c.form; b[2] = c.forms; b[3] = c.species; b[4] = c.speciesTotal; b[5] = c.races;
        for (int i = 0; i < 4; i++) b[6 + i] = (c.encounters >> (8 * i)) & 0xFF;
        b[10] = c.level;
    }
    static bool _unpackCard(const uint8_t* b, size_t n, Card& c) {
        if (n < CARD_MIN || b[0] != 1) return false;    // newer versions keep these bytes first
        c.form = b[1]; c.forms = b[2]; c.species = b[3]; c.speciesTotal = b[4]; c.races = b[5];
        c.encounters = 0;
        for (int i = 0; i < 4; i++) c.encounters |= (uint32_t)b[6 + i] << (8 * i);
        c.level = n > 10 ? b[10] : 0;
        return true;
    }
    struct Seen { uint32_t from, id; } _seen[32] = {};
    int      _seenNext = 0;
    Msg      _lastIn;
    bool     _incoming = false;

    Kotodama() = default;

    void _loadPrefs() {
        if (_prefsLoaded) return;
        _prefsLoaded = true;
        Preferences p; p.begin("kotodama", true);
        _region = p.getUChar("region", Mesh::REG_UNSET);
        if (_region >= Mesh::REG_COUNT) _region = Mesh::REG_UNSET;   // hand-edited settings.conf
        _on = p.getUChar("on", 0);
        String n = p.getString("name", "");
        p.end();
        char hex[5];
        snprintf(hex, sizeof(hex), "%04x", (unsigned)(nodeNum() & 0xFFFF));
        if (n.length()) strncpy(_long, n.c_str(), sizeof(_long) - 1);
        else snprintf(_long, sizeof(_long), "Mitama %s", hex);
        memcpy(_short, hex, 5);
    }

    // The cap's I/O expander answers on the internal I2C bus.
    static bool _capFitted() { return m5::In_I2C.scanID(IOE_ADDR, 400000); }

    // PI4IOE5V6408: 0x03 direction (1 = output), 0x05 output, 0x07 high-Z.
    static void _rfSwitch(bool on) {
        auto& i2c = m5::In_I2C;
        uint8_t dir = i2c.readRegister8(IOE_ADDR, 0x03, 400000);
        uint8_t hz  = i2c.readRegister8(IOE_ADDR, 0x07, 400000);
        uint8_t out = i2c.readRegister8(IOE_ADDR, 0x05, 400000);
        i2c.writeRegister8(IOE_ADDR, 0x03, dir | 0x01, 400000);
        i2c.writeRegister8(IOE_ADDR, 0x07, hz & ~0x01, 400000);
        i2c.writeRegister8(IOE_ADDR, 0x05, on ? (out | 0x01) : (out & ~0x01), 400000);
    }

    // Not IRAM_ATTR: an inline function placed in IRAM fails to link here,
    // and Arduino's GPIO ISR service doesn't require IRAM handlers.
    static void _isr() { _rxFlag = true; }

    static void _taskFn(void* arg) { ((Kotodama*)arg)->_run(); }

    void _run() {
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(20));
            if (_stopping) continue;
            _Lock lk(this);
            if (_state != ST_ON) {                   // radio stopped: end the task
                _task = nullptr;
                xSemaphoreGive(_radioLock);
                lk.m = nullptr;
                vTaskDelete(nullptr);
            }
            if (_rxFlag) { _rxFlag = false; _receive(); }
            if (_txPending) _sendQueued();
            else if ((int32_t)(millis() - _announceAt) >= 0) { _announce(); _announceAt = millis() + ANNOUNCE_MS; }
            else if (_replyAt && (int32_t)(millis() - _replyAt) >= 0) { _replyAt = 0; _lastReply = millis(); _announceOn(Mesh::CH_MITAMA, false); }
            else if ((int32_t)(millis() - _crewAt) >= 0) { _crewAt = millis() + CREW_MS; _announceOn(Mesh::CH_MITAMA, false); }
        }
    }

    void _sendQueued() { _transmitText(); _txPending = false; }

    // Handles packets for `ms` instead of a bare delay, which would leave
    // them unread (the next one overwrites the chip's buffer). The radio
    // must already be receiving.
    void _listen(uint32_t ms) {
        uint32_t t0 = millis();
        while (millis() - t0 < ms) {
            if (_rxFlag) { _rxFlag = false; _receive(); }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    void _receive() {
        _Crumb cr("kotodama rx");
        uint8_t buf[Mesh::MAX_PKT];
        size_t len = _radio->getPacketLength();
        int st = (len > 0 && len <= sizeof(buf)) ? _radio->readData(buf, len) : RADIOLIB_ERR_PACKET_TOO_LONG;
        int16_t rssi = (int16_t)_radio->getRSSI();
        int8_t snr = (int8_t)_radio->getSNR();
        _radio->startReceive();
        if (st != RADIOLIB_ERR_NONE || len <= (size_t)Mesh::HDR) return;

        Mesh::Header h = Mesh::unpackHeader(buf);
        if (h.from == nodeNum() || _dup(h.from, h.id)) return;
        if (h.to != Mesh::BROADCAST) return;          // direct messages use keys we don't have
        int ch = -1;
        for (uint8_t c = 0; c < Mesh::CH_COUNT; c++) if (Mesh::channel(c).hash == h.hash) ch = c;
        if (ch < 0) return;

        uint8_t* body = buf + Mesh::HDR;
        size_t blen = len - Mesh::HDR;
        Mesh::crypt(Mesh::channel(ch).key, h.id, h.from, body, blen);
        uint32_t port; const uint8_t* pl; size_t plen; bool wantResp;
        if (!Mesh::decodeData(body, blen, port, pl, plen, &wantResp)) return;
        _heard(h.from, ch, rssi);

        if (port == Mesh::PORT_NODEINFO && pl) {
            Node n; n.num = h.from;
            Mesh::decodeUser(pl, plen, n.longName, sizeof(n.longName), n.shortName, sizeof(n.shortName));
            // Names from the air go into friends.txt and the log one per line.
            for (char* c = n.longName; *c; c++) if ((uint8_t)*c < 0x20) *c = ' ';
            for (char* c = n.shortName; *c; c++) if ((uint8_t)*c < 0x20) *c = ' ';
            _rememberNode(n);
            // Another Mitama just arrived and asked who's around: answer with
            // our name on the Mitama channel, after a random pause so several
            // replies don't collide, and at most once a minute.
            if (wantResp && ch == Mesh::CH_MITAMA && !_replyAt && (!_lastReply || millis() - _lastReply > 60000))
                _replyAt = millis() + 1000 + esp_random() % 4000;
        } else if (port == Mesh::PORT_CARD && pl && ch == Mesh::CH_MITAMA) {
            Card c;
            if (_unpackCard(pl, plen, c)) {
                portENTER_CRITICAL(&_mux);
                Node* n = _slot(h.from);
                n->card = c; n->hasCard = true;
                portEXIT_CRITICAL(&_mux);
            }
        } else if (port == Mesh::PORT_TEXT && pl) {
            Msg m;
            m.from = h.from; m.ch = ch; m.rssi = rssi; m.snr = snr;
            size_t n = plen < (size_t)Mesh::MAX_TEXT ? plen : Mesh::MAX_TEXT;
            memcpy(m.text, pl, n); m.text[n] = 0;
            for (char* p = m.text; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
            _store(m, true);
        }
    }

    bool _dup(uint32_t from, uint32_t id) {
        for (auto& s : _seen) if (s.from == from && s.id == id) return true;
        _seen[_seenNext] = { from, id };
        _seenNext = (_seenNext + 1) % 32;
        return false;
    }

    // Call under _mux.
    Node* _find(uint32_t num) {
        for (auto& n : _nodes) if (n.num == num) return &n;
        return nullptr;
    }
    int _friendIdx(uint32_t num) const {
        for (int i = 0; i < _nFriends; i++) if (_friends[i].num == num) return i;
        return -1;
    }
    // The node's slot, made if new (replacing the longest-silent one). Under _mux.
    Node* _slot(uint32_t num) {
        Node* n = _find(num);
        if (n) return n;
        // Busy LongFast meshes have more nodes than slots: give up strangers
        // first, so crew don't drop out and "arrive" again.
        uint32_t now = millis();
        auto keep = [&](const Node& x) { return x.crew && x.heardMs && now - x.heardMs < CREW_AWAY_MS; };
        Node* best = nullptr;
        for (auto& x : _nodes) {
            if (!x.num) { best = &x; break; }
            if (best && keep(x) && !keep(*best)) continue;
            if (!best || (keep(*best) && !keep(x)) || (int32_t)(x.heardMs - best->heardMs) < 0) best = &x;
        }
        *best = Node();
        best->num = num;
        return best;
    }

    // Any packet we could read: note when and how strong; on the Mitama
    // channel that's a crew member, maybe just arriving.
    void _heard(uint32_t from, int ch, int16_t rssi) {
        uint32_t now = millis() | 1;
        portENTER_CRITICAL(&_mux);
        Node* n = _slot(from);
        if (ch == Mesh::CH_MITAMA) {
            bool arriving = !n->crew || now - n->heardMs > CREW_AWAY_MS;
            if (!n->crew) _crewNewN++;
            n->crew = true;
            if (arriving) {
                if (_nArrivals == 4) memmove(_arrivals, _arrivals + 1, sizeof(Arrival) * --_nArrivals);   // full: drop the oldest
                _arrivals[_nArrivals++] = { from, _friendIdx(from) >= 0, now };
            }
        }
        n->heardMs = now; n->rssi = rssi;
        portEXIT_CRITICAL(&_mux);
    }

    void _rememberNode(const Node& in) {
        portENTER_CRITICAL(&_mux);
        Node* n = _slot(in.num);
        memcpy(n->longName, in.longName, sizeof(n->longName));
        memcpy(n->shortName, in.shortName, sizeof(n->shortName));
        int f = _friendIdx(in.num);   // a friend renamed: keep the saved name current
        if (f >= 0 && in.longName[0] && strcmp(_friends[f].name, in.longName)) {
            strncpy(_friends[f].name, in.longName, sizeof(_friends[f].name) - 1);
            _friendsDirty = true;
        }
        portEXIT_CRITICAL(&_mux);
    }

    // friends.txt: one "<node number in hex> <name>" per line. Written
    // whole to a .tmp and swapped in, like the Compendium.
    void _friendsTick() {
        if (SD.cardType() == CARD_NONE) return;
        if (!_friendsLoaded) {
            _friendsLoaded = true;
            if (!SD.exists(FRIENDS) && SD.exists(FRIENDS_TMP)) SD.rename(FRIENDS_TMP, FRIENDS);
            File f = SD.open(FRIENDS, FILE_READ);
            if (f) {
                Friend tmp[MAX_FRIENDS]; int n = 0;
                while (f.available() && n < MAX_FRIENDS) {
                    String line = f.readStringUntil('\n');
                    line.trim();
                    int sp = line.indexOf(' ');
                    if (sp <= 0) continue;
                    uint32_t num = strtoul(line.substring(0, sp).c_str(), nullptr, 16);
                    if (!num) continue;
                    tmp[n].num = num;
                    String nm = line.substring(sp + 1); nm.trim();
                    strncpy(tmp[n].name, nm.c_str(), sizeof(tmp[n].name) - 1);
                    tmp[n].name[sizeof(tmp[n].name) - 1] = 0;
                    n++;
                }
                f.close();
                portENTER_CRITICAL(&_mux);
                memcpy(_friends, tmp, sizeof(Friend) * n);
                _nFriends = n;
                portEXIT_CRITICAL(&_mux);
            }
        }
        if (!_friendsDirty) return;
        Friend copy[MAX_FRIENDS]; int n;
        portENTER_CRITICAL(&_mux);
        _friendsDirty = false;
        n = _nFriends;
        memcpy(copy, _friends, sizeof(Friend) * n);
        portEXIT_CRITICAL(&_mux);
        _Crumb cr("kotodama friends");
        if (!SD.exists("/MitamaOS/kotodama")) SD.mkdir("/MitamaOS/kotodama");
        File f = SD.open(FRIENDS_TMP, FILE_WRITE);
        if (!f) return;
        for (int i = 0; i < n; i++) f.printf("%08lx %s\n", (unsigned long)copy[i].num, copy[i].name);
        f.close();
        SD.remove(FRIENDS);
        SD.rename(FRIENDS_TMP, FRIENDS);
    }

    // k-th oldest message of a channel's ring. Call under _mux.
    Msg* _at(uint8_t ch, int k) {
        const _Ring& r = _ring[ch];
        return &_msgs[ch * MAX_MSGS + (r.head + MAX_MSGS - r.count + k) % MAX_MSGS];
    }

    void _store(Msg& m, bool incoming) {
        time_t t = time(nullptr);
        m.unix = t > 1600000000 ? (uint32_t)t : 0;
        portENTER_CRITICAL(&_mux);
        if (!_msgs || m.ch >= Mesh::CH_COUNT) { portEXIT_CRITICAL(&_mux); return; }
        _Ring& r = _ring[m.ch];
        _msgs[m.ch * MAX_MSGS + r.head] = m;
        r.head = (r.head + 1) % MAX_MSGS;
        if (r.count < MAX_MSGS) r.count++;
        if (incoming) { _unread[m.ch]++; _lastIn = m; _incoming = true; }
        _serial++;
        portEXIT_CRITICAL(&_mux);
    }

    // Listen-before-talk: wait while someone else is mid-packet, receiving
    // it meanwhile (it may well be for us).
    void _waitClear() {
        for (int i = 0; i < 5; i++) {
            // scanChannel() waits on the IRQ pin with no time limit; this
            // gives up after 100 ms and treats the air as clear.
            _rxFlag = false;
            if (_radio->startChannelScan() != RADIOLIB_ERR_NONE) return;
            uint32_t t0 = millis();
            while (!_rxFlag && millis() - t0 < 100) delay(1);
            bool done = _rxFlag;
            _rxFlag = false;                       // that was the scan's own interrupt
            int st = done ? _radio->getChannelScanResult() : RADIOLIB_CHANNEL_FREE;
            if (!done) _radio->standby();
            if (st != RADIOLIB_LORA_DETECTED) return;
            _radio->startReceive();                // the scan left it idle
            _listen(200 + esp_random() % 600);
        }
    }

    bool _sendData(uint8_t ch, uint32_t port, const uint8_t* payload, size_t len, bool wantResponse) {
        uint8_t pkt[Mesh::MAX_PKT];
        size_t n = Mesh::encodeData(pkt + Mesh::HDR, sizeof(pkt) - Mesh::HDR, port, payload, len, wantResponse);
        if (!n) return false;
        Mesh::Header h;
        h.from = nodeNum();
        h.id = esp_random();
        h.flags = Mesh::defaultFlags();
        h.hash = Mesh::channel(ch).hash;
        h.relay = nodeNum() & 0xFF;
        Mesh::packHeader(pkt, h);
        Mesh::crypt(Mesh::channel(ch).key, h.id, h.from, pkt + Mesh::HDR, n);
        _dup(h.from, h.id);
        if (_rxFlag) { _rxFlag = false; _receive(); }   // a packet that landed just now
        CrashLog::crumb("kotodama cad");
        _waitClear();
        CrashLog::crumb(ch ? "kotodama tx LongFast" : "kotodama tx Mitama");
        int st = _radio->transmit(pkt, Mesh::HDR + n);
        _rxFlag = false;                       // the "sent" interrupt, not a packet
        _radio->startReceive();
        CrashLog::done();
        return st == RADIOLIB_ERR_NONE;
    }

    void _transmitText() {
        Msg m;
        m.from = nodeNum(); m.ch = _txCh; m.mine = true;
        strncpy(m.text, _txText, Mesh::MAX_TEXT);
        if (_sendData(_txCh, Mesh::PORT_TEXT, (const uint8_t*)_txText, strlen(_txText), false)) _store(m, false);
        else _txFailed = true;
    }

    // Our name, on both channels. Only the Mitama channel asks for names
    // back: asking the whole public mesh would flood it with replies.
    void _announce() {
        for (uint8_t c = 0; c < Mesh::CH_COUNT; c++) {
            if (_stopping) return;
            if (_txPending) _sendQueued();          // don't make a typed message wait
            _announceOn(c, c == Mesh::CH_MITAMA);
            _listen(500);
        }
    }
    void _announceOn(uint8_t ch, bool wantResponse) {
        uint8_t user[80];
        size_t n = Mesh::encodeUser(user, sizeof(user), nodeNum(), longName(), shortName());
        if (n) _sendData(ch, Mesh::PORT_NODEINFO, user, n, wantResponse);
        if (ch != Mesh::CH_MITAMA) return;
        uint8_t card[CARD_LEN];
        portENTER_CRITICAL(&_mux);
        bool have = _hasMyCard;
        if (have) _packCard(_myCard, card);
        portEXIT_CRITICAL(&_mux);
        if (have && !_stopping) { _listen(300); _sendData(ch, Mesh::PORT_CARD, card, CARD_LEN, false); }
    }
};
