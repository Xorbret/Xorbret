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
// Messages are kept in RAM and appended to /MitamaOS/kotodama/log.txt by
// the main loop (tick()).
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
    struct Node { uint32_t num = 0; char shortName[5] = ""; char longName[25] = ""; };

    static constexpr int MAX_MSGS = 24, MAX_NODES = 24;

    enum State : uint8_t { ST_OFF, ST_NO_REGION, ST_NO_CAP, ST_RADIO_FAIL, ST_ON };

    // ── settings (NVS "kotodama", mirrored to settings.conf) ──
    uint8_t region() { _loadPrefs(); return _region; }
    const char* longName() { _loadPrefs(); return _long; }
    const char* shortName() { _loadPrefs(); return _short; }
    bool wantOn() { _loadPrefs(); return _on; }

    void setRegion(uint8_t r) {
        _loadPrefs(); _region = r;
        Preferences p; p.begin("kotodama", false); p.putUChar("region", r); p.end();
        SdSettings::changed();
        if (_state == ST_ON) { stop(); start(); }
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
        if (on) start(); else stop();
    }

    // At boot: starts the radio if it was left on.
    void boot() { if (wantOn()) start(); }

    void start() {
        if (_state == ST_ON) return;
        _loadPrefs();
        _Lock lk(this);
        if (_region == Mesh::REG_UNSET) { _state = ST_NO_REGION; return; }
        if (!_capFitted()) { _state = ST_NO_CAP; return; }
        if (!_msgs) _msgs = new (std::nothrow) Msg[MAX_MSGS];
        if (!_msgs || !SdBus::spi) { _state = ST_RADIO_FAIL; return; }
        _rfSwitch(true);
        if (!_radio) _radio = new SX1262(new Module(LORA_CS, LORA_IRQ, LORA_RST, LORA_BUSY, *SdBus::spi,
                                                    SPISettings(2000000, MSBFIRST, SPI_MODE0)));
        const auto& R = Mesh::region(_region);
        int st = _radio->begin(Mesh::frequency(_region), Mesh::BW_KHZ, Mesh::SF, Mesh::CR, Mesh::SYNC,
                               R.power, Mesh::PREAMBLE, 3.0f, true);
        if (st != RADIOLIB_ERR_NONE) { _err = st; _state = ST_RADIO_FAIL; return; }
        _radio->setCurrentLimit(140);
        _radio->setDio1Action(_isr);
        _rxFlag = false;
        _radio->startReceive();
        _state = ST_ON;
        _announceAt = millis() + 3000;      // say who we are shortly after starting
        if (!_task) xTaskCreatePinnedToCore(_taskFn, "kotodama", 6144, this, 1, &_task, 0);
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
        return true;
    }
    bool sending() const { return _txPending; }
    // Set when the last queued message failed to go out; read once.
    bool takeSendFailed() { bool f = _txFailed; _txFailed = false; return f; }

    // Messages, oldest first. Copies under the lock.
    int messages(Msg* out, int max, uint8_t ch) {
        int n = 0;
        if (!_msgs) return 0;
        portENTER_CRITICAL(&_mux);
        for (int k = 0; k < _count && n < max; k++) {
            const Msg& m = _msgs[(_head + MAX_MSGS - _count + k) % MAX_MSGS];
            if (m.ch == ch) out[n++] = m;
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

    // Newest incoming text, for the Mitama to mention (once).
    bool takeIncoming(Msg& out) {
        portENTER_CRITICAL(&_mux);
        bool had = _incoming;
        if (had) { out = _lastIn; _incoming = false; }
        portEXIT_CRITICAL(&_mux);
        return had;
    }

    // Main loop: append new messages to the SD log.
    void tick() {
        if (!_msgs || _serial == _loggedSerial) return;
        _loggedSerial = _serial;
        if (SD.cardType() == CARD_NONE) return;
        if (!SD.exists("/MitamaOS/kotodama")) SD.mkdir("/MitamaOS/kotodama");
        File f = SD.open("/MitamaOS/kotodama/log.txt", FILE_APPEND);
        if (!f) return;
        Msg m;
        for (int k = 0; k < MAX_MSGS; k++) {
            bool found = false;
            portENTER_CRITICAL(&_mux);
            for (int j = 0; j < _count; j++) {
                Msg& x = _msgs[(_head + MAX_MSGS - _count + j) % MAX_MSGS];
                if (!x.logged) { x.logged = true; m = x; found = true; break; }
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

    SX1262*      _radio = nullptr;
    TaskHandle_t _task = nullptr;
    volatile State _state = ST_OFF;
    int          _err = 0;
    uint32_t     _node = 0;
    static inline volatile bool _rxFlag = false;
    volatile bool _stopping = false, _txFailed = false;
    SemaphoreHandle_t _radioLock = nullptr;
    struct _Lock {
        SemaphoreHandle_t m;
        _Lock(Kotodama* k) {
            if (!k->_radioLock) k->_radioLock = xSemaphoreCreateMutex();
            m = k->_radioLock; xSemaphoreTake(m, portMAX_DELAY);
        }
        ~_Lock() { xSemaphoreGive(m); }
    };
    uint32_t _replyAt = 0, _lastReply = 0;       // answering a Mitama's "who's there?"

    bool    _prefsLoaded = false, _on = false;
    uint8_t _region = Mesh::REG_UNSET;
    char    _long[25] = "", _short[5] = "";

    volatile bool _txPending = false;
    char     _txText[Mesh::MAX_TEXT + 1];
    uint8_t  _txCh = 0;
    uint32_t _announceAt = 0;

    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    Msg*     _msgs = nullptr;
    int      _head = 0, _count = 0;
    volatile uint32_t _serial = 0;
    uint32_t _loggedSerial = 0;
    volatile int _unread[Mesh::CH_COUNT] = {0, 0};
    Node     _nodes[MAX_NODES];
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
            if (_state != ST_ON || _stopping) continue;
            _Lock lk(this);
            if (_state != ST_ON) continue;           // stopped while we waited
            if (_rxFlag) { _rxFlag = false; _receive(); }
            if (_txPending) { _transmitText(); _txPending = false; }
            else if ((int32_t)(millis() - _announceAt) >= 0) { _announce(); _announceAt = millis() + ANNOUNCE_MS; }
            else if (_replyAt && (int32_t)(millis() - _replyAt) >= 0) { _replyAt = 0; _lastReply = millis(); _announceOn(Mesh::CH_MITAMA, false); }
        }
    }

    void _receive() {
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

        if (port == Mesh::PORT_NODEINFO && pl) {
            Node n; n.num = h.from;
            Mesh::decodeUser(pl, plen, n.longName, sizeof(n.longName), n.shortName, sizeof(n.shortName));
            _rememberNode(n);
            // Another Mitama just arrived and asked who's around: answer with
            // our name on the Mitama channel, after a random pause so several
            // replies don't collide, and at most once a minute.
            if (wantResp && ch == Mesh::CH_MITAMA && !_replyAt && millis() - _lastReply > 60000)
                _replyAt = millis() + 1000 + esp_random() % 4000;
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

    void _rememberNode(const Node& n) {
        portENTER_CRITICAL(&_mux);
        int slot = -1;
        for (int i = 0; i < MAX_NODES; i++) if (_nodes[i].num == n.num) slot = i;
        if (slot < 0) for (int i = 0; i < MAX_NODES; i++) if (!_nodes[i].num) { slot = i; break; }
        if (slot < 0) slot = esp_random() % MAX_NODES;
        _nodes[slot] = n;
        portEXIT_CRITICAL(&_mux);
    }

    void _store(Msg& m, bool incoming) {
        time_t t = time(nullptr);
        m.unix = t > 1600000000 ? (uint32_t)t : 0;
        portENTER_CRITICAL(&_mux);
        _msgs[_head] = m;
        _head = (_head + 1) % MAX_MSGS;
        if (_count < MAX_MSGS) _count++;
        if (incoming) { _unread[m.ch]++; _lastIn = m; _incoming = true; }
        _serial++;
        portEXIT_CRITICAL(&_mux);
    }

    // Listen-before-talk: wait while someone else is mid-packet.
    void _waitClear() {
        for (int i = 0; i < 5; i++) {
            if (_radio->scanChannel() != RADIOLIB_LORA_DETECTED) return;
            delay(200 + esp_random() % 600);
        }
    }

    bool _sendData(uint8_t ch, uint8_t port, const uint8_t* payload, size_t len, bool wantResponse) {
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
        _waitClear();
        int st = _radio->transmit(pkt, Mesh::HDR + n);
        _rxFlag = false;
        _radio->startReceive();
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
            if (_txPending || _stopping) return;
            _announceOn(c, c == Mesh::CH_MITAMA);
            delay(500);
        }
    }
    void _announceOn(uint8_t ch, bool wantResponse) {
        uint8_t user[80];
        size_t n = Mesh::encodeUser(user, sizeof(user), nodeNum(), longName(), shortName());
        if (n) _sendData(ch, Mesh::PORT_NODEINFO, user, n, wantResponse);
    }
};
