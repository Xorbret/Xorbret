#pragma once
// Meshtastic-compatible LoRa packets for Kotodama, written from the
// protocol's public description (meshtastic.org "Mesh Broadcast
// Algorithm" docs): no Meshtastic code is used.
//
// A packet is a 16-byte plain header followed by an AES-128-CTR encrypted
// `Data` protobuf. Both channels Kotodama speaks ride the same frequency
// slot (the default LongFast preset), told apart by the channel hash byte:
//  - "Mitama":   MitamaOS users. Its key is fixed and public, so any
//                Meshtastic user can join by adding the channel by hand.
//  - "LongFast": the default public Meshtastic channel.
// Only broadcast text (port 1) and node info (port 4) are understood.
#include <stdint.h>
#include <string.h>
#include <mbedtls/aes.h>

namespace Mesh {

static constexpr uint32_t BROADCAST = 0xFFFFFFFF;
static constexpr uint8_t  PORT_TEXT = 1, PORT_NODEINFO = 4;
static constexpr uint8_t  HW_PRIVATE = 255;
static constexpr int      HDR = 16, MAX_PKT = 255, MAX_TEXT = 200;

struct Channel { const char* name; uint8_t key[16]; uint8_t hash; };

// hash = XOR of the name's bytes ^ XOR of the key's bytes
inline uint8_t channelHash(const char* name, const uint8_t key[16]) {
    uint8_t h = 0;
    for (const char* p = name; *p; p++) h ^= (uint8_t)*p;
    for (int i = 0; i < 16; i++) h ^= key[i];
    return h;
}

enum Ch : uint8_t { CH_MITAMA, CH_LONGFAST, CH_COUNT };
// The hash bytes are written out (channelHash() of each row) so the table
// is plain constant data: safe to read from both cores at once.
inline const Channel& channel(uint8_t c) {
    static const Channel C[CH_COUNT] = {
        // first 16 bytes of SHA-256("MitamaOS Kotodama"); base64 ZD5oKNQHOnW2XunlY1HeuQ==
        { "Mitama",   { 0x64,0x3e,0x68,0x28,0xd4,0x07,0x3a,0x75,0xb6,0x5e,0xe9,0xe5,0x63,0x51,0xde,0xb9 }, 10 },
        // Meshtastic's default key (PSK "AQ==")
        { "LongFast", { 0xd4,0xf1,0xbb,0x3a,0x20,0x29,0x07,0x59,0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,0x01 }, 8 },
    };
    return C[c < CH_COUNT ? c : 0];
}

// ── radio settings ──────────────────────────────────────────────────────
// LongFast: SF11, 250 kHz, CR 4/5, preamble 16, sync word 0x2B. The slot
// inside a region is djb2("LongFast") % slots.
enum Region : uint8_t { REG_UNSET, REG_US, REG_EU868, REG_COUNT };
struct RegionInfo { const char* name; float lo, hi; int8_t power; };
inline const RegionInfo& region(uint8_t r) {
    static const RegionInfo R[REG_COUNT] = {
        { "Not set", 0, 0, 0 },
        { "US 915",  902.0f, 928.0f, 22 },
        { "EU 868",  869.4f, 869.65f, 20 },
    };
    return R[r < REG_COUNT ? r : 0];
}
static constexpr float BW_KHZ = 250.0f;
static constexpr uint8_t SF = 11, CR = 5, SYNC = 0x2B;
static constexpr uint16_t PREAMBLE = 16;

inline float frequency(uint8_t r) {
    const RegionInfo& R = region(r);
    float bw = BW_KHZ / 1000.0f;
    int slots = (int)((R.hi - R.lo) / bw);
    if (slots < 1) slots = 1;
    uint32_t h = 5381;
    for (const char* p = "LongFast"; *p; p++) h = h * 33 + (uint8_t)*p;
    return R.lo + bw / 2 + (h % slots) * bw;   // US: 906.875 MHz
}

// ── header ──────────────────────────────────────────────────────────────
struct Header {
    uint32_t to = BROADCAST, from = 0, id = 0;
    uint8_t  flags = 0, hash = 0, nextHop = 0, relay = 0;
    uint8_t hopLimit() const { return flags & 7; }
};
inline void _put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
inline uint32_t _get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

inline void packHeader(uint8_t* b, const Header& h) {
    _put32(b, h.to); _put32(b + 4, h.from); _put32(b + 8, h.id);
    b[12] = h.flags; b[13] = h.hash; b[14] = h.nextHop; b[15] = h.relay;
}
inline Header unpackHeader(const uint8_t* b) {
    Header h;
    h.to = _get32(b); h.from = _get32(b + 4); h.id = _get32(b + 8);
    h.flags = b[12]; h.hash = b[13]; h.nextHop = b[14]; h.relay = b[15];
    return h;
}
// hop limit 3, hop start 3 (bits 0-2 and 5-7)
inline uint8_t defaultFlags() { return 3 | (3 << 5); }

// ── AES-128-CTR; nonce = packet id (u64 LE), sender (u32 LE), 0 ─────────
inline void crypt(const uint8_t key[16], uint32_t id, uint32_t from, uint8_t* buf, size_t len) {
    uint8_t nonce[16] = {0}, stream[16] = {0};
    _put32(nonce, id);
    _put32(nonce + 8, from);
    size_t off = 0;
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);
    mbedtls_aes_crypt_ctr(&ctx, len, &off, nonce, stream, buf, buf);
    mbedtls_aes_free(&ctx);
}

// ── minimal protobuf ────────────────────────────────────────────────────
struct Writer {
    uint8_t* b; size_t cap, n = 0; bool ok = true;
    void byte(uint8_t v) { if (n < cap) b[n++] = v; else ok = false; }
    void varint(uint32_t v) { while (v >= 0x80) { byte((v & 0x7F) | 0x80); v >>= 7; } byte(v); }
    void tagVarint(uint8_t field, uint32_t v) { varint(field << 3); varint(v); }
    void tagBytes(uint8_t field, const uint8_t* d, size_t len) {
        varint(field << 3 | 2); varint(len);
        for (size_t i = 0; i < len; i++) byte(d[i]);
    }
    void tagStr(uint8_t field, const char* s) { tagBytes(field, (const uint8_t*)s, strlen(s)); }
};

struct Field { uint8_t num = 0, wire = 0; uint32_t v = 0; const uint8_t* d = nullptr; size_t len = 0; };
struct Reader {
    const uint8_t* b; size_t len, i = 0; bool bad = false;
    bool varint(uint32_t& out) {
        out = 0;
        for (int s = 0; s < 35; s += 7) {
            if (i >= len) { bad = true; return false; }
            uint8_t c = b[i++];
            out |= (uint32_t)(c & 0x7F) << s;
            if (!(c & 0x80)) return true;
        }
        bad = true; return false;
    }
    bool next(Field& f) {
        if (i >= len || bad) return false;
        uint32_t tag;
        if (!varint(tag)) return false;
        f = Field(); f.num = tag >> 3; f.wire = tag & 7;
        switch (f.wire) {
            case 0: return varint(f.v);
            // lengths are compared against what's left, so a huge length
            // from a hostile packet can't wrap the sum past the check
            case 1: if (len - i < 8) { bad = true; return false; } i += 8; return true;
            case 5: if (len - i < 4) { bad = true; return false; } i += 4; return true;
            case 2: {
                uint32_t l;
                if (!varint(l) || l > len - i) { bad = true; return false; }
                f.d = b + i; f.len = l; i += l; return true;
            }
            default: bad = true; return false;
        }
    }
};

// Data { 1: portnum, 2: payload, 3: want_response }
inline size_t encodeData(uint8_t* out, size_t cap, uint8_t port, const uint8_t* payload, size_t len,
                         bool wantResponse = false) {
    Writer w{ out, cap };
    w.tagVarint(1, port);
    w.tagBytes(2, payload, len);
    if (wantResponse) w.tagVarint(3, 1);
    return w.ok ? w.n : 0;
}
inline bool decodeData(const uint8_t* in, size_t len, uint32_t& port, const uint8_t*& payload, size_t& plen,
                       bool* wantResponse = nullptr) {
    Reader r{ in, len };
    Field f;
    port = 0; payload = nullptr; plen = 0;
    if (wantResponse) *wantResponse = false;
    while (r.next(f)) {
        if (f.num == 1 && f.wire == 0) port = f.v;
        else if (f.num == 2 && f.wire == 2) { payload = f.d; plen = f.len; }
        else if (f.num == 3 && f.wire == 0 && wantResponse) *wantResponse = f.v != 0;
    }
    return !r.bad && port != 0;
}

// User { 1: id "!xxxxxxxx", 2: long_name, 3: short_name, 5: hw_model }
inline size_t encodeUser(uint8_t* out, size_t cap, uint32_t node, const char* longName, const char* shortName) {
    char id[12];
    static const char* H = "0123456789abcdef";
    id[0] = '!';
    for (int k = 0; k < 8; k++) id[1 + k] = H[(node >> (28 - 4 * k)) & 15];
    id[9] = 0;
    Writer w{ out, cap };
    w.tagStr(1, id); w.tagStr(2, longName); w.tagStr(3, shortName);
    w.tagVarint(5, HW_PRIVATE);
    return w.ok ? w.n : 0;
}
inline void decodeUser(const uint8_t* in, size_t len, char* longName, size_t ln, char* shortName, size_t sn) {
    Reader r{ in, len };
    Field f;
    while (r.next(f)) {
        if (f.wire != 2) continue;
        char* dst = f.num == 2 ? longName : f.num == 3 ? shortName : nullptr;
        size_t cap = f.num == 2 ? ln : sn;
        if (!dst || !cap) continue;
        size_t n = f.len < cap - 1 ? f.len : cap - 1;
        memcpy(dst, f.d, n); dst[n] = 0;
    }
}

} // namespace Mesh
