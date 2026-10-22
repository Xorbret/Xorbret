#pragma once
// Keeps a copy of /MitamaOS/model.bin in flash and maps it into memory, so MiTalk reads its weights through the flash cache
// instead of re-reading 4.7 MB from the SD card for every character.
//
// Where: a "model" data partition if the flash layout has one, else the
// unused space after MitamaOS's own program in its app slot (Launcher
// gives each app one slot; anything past the program image is free, and a
// reinstall just means the copy is redone on the next MiTalk open).
//
// Layout: one 4 KB header sector (magic, size, SD file fingerprint), then
// the model bytes. The header is written last, so a copy that was cut off
// (power loss, card pulled) is never trusted and is simply redone.
#include <Arduino.h>
#include <SD.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_spi_flash.h>
#include <rom/crc.h>

namespace ModelFlash {

static constexpr const char* SD_PATH = "/MitamaOS/model.bin";
static constexpr uint32_t MAGIC = 0x314D544D;   // "MTM1"
static constexpr uint32_t DATA_OFF = 0x1000;
static constexpr uint32_t CHUNK = 0x10000;      // 64 KB: one erase/copy step

struct Header { uint32_t magic, size, mtime, crc; };

static const esp_partition_t* _part = nullptr;
static uint32_t _base = 0;      // where our area starts inside _part
static uint32_t _room = 0;      // bytes available from _base
static const uint8_t* _map = nullptr;
static spi_flash_mmap_handle_t _handle = 0;

inline const esp_partition_t* part() {
    if (_part) return _part;
    _part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "model");
    if (_part) { _base = 0; _room = _part->size; return _part; }
    // Our own app slot, past the program (64 KB aligned, plus 64 KB spare).
    const esp_partition_t* run = esp_ota_get_running_partition();
    uint32_t img = (ESP.getSketchSize() + CHUNK - 1) / CHUNK * CHUNK + CHUNK;
    if (run && run->size > img + DATA_OFF + CHUNK) { _part = run; _base = img; _room = run->size - img; }
    return _part;
}

inline uint32_t room() { part(); return _room; }

// What identifies this exact SD file: size, timestamp, and a CRC of its
// first and last 4 KB (cheap to read, catches a retrained model).
static constexpr size_t BUF = 4096;

inline Header _fingerprint(File& f, uint8_t* buf) {
    Header h{ MAGIC, (uint32_t)f.size(), (uint32_t)f.getLastWrite(), 0 };
    size_t n = f.read(buf, BUF);
    h.crc = crc32_le(0, buf, n);
    if (h.size > BUF) {
        f.seek(h.size - BUF);
        n = f.read(buf, BUF);
        h.crc = crc32_le(h.crc, buf, n);
    }
    f.seek(0);
    return h;
}

inline bool _copy(const esp_partition_t* p, File& f, uint8_t* buf, void (*progress)(int), const char** why);

static uint32_t _mapSize = 0;

inline void unmap() {
    if (_handle) spi_flash_munmap(_handle);
    _handle = 0; _map = nullptr;
}

// Makes sure flash holds the same model as the SD card, copying it if not.
// progress(pct) is called during a copy. Returns false (with a reason) if
// the flash copy can't be used; MiTalk then falls back to the SD card.
inline bool sync(void (*progress)(int pct), const char** why) {
    const esp_partition_t* p = part();
    if (!p) { *why = "no free flash for the model"; return false; }
    File f = SD.open(SD_PATH, FILE_READ);
    if (!f) {
        // No SD file: a copy already in flash is still good to use.
        Header h; esp_partition_read(p, _base, &h, sizeof(h));
        if (h.magic == MAGIC) return true;
        *why = "model.bin not found"; return false;
    }
    uint8_t* buf = (uint8_t*)malloc(BUF);
    if (!buf) { f.close(); *why = "no memory to check model"; return false; }
    bool ok = _copy(p, f, buf, progress, why);
    free(buf);
    f.close();
    return ok;
}

inline bool _copy(const esp_partition_t* p, File& f, uint8_t* buf, void (*progress)(int), const char** why) {
    Header want = _fingerprint(f, buf);
    if (want.size + DATA_OFF > _room) {
        static char msg[48];
        snprintf(msg, sizeof(msg), "model %uK, flash room %uK", (unsigned)(want.size / 1024), (unsigned)(_room / 1024));
        *why = msg; return false;
    }
    Header have; esp_partition_read(p, _base, &have, sizeof(have));
    if (memcmp(&have, &want, sizeof(Header)) == 0) return true;

    unmap();
    uint32_t total = (DATA_OFF + want.size + CHUNK - 1) / CHUNK * CHUNK;
    if (total > _room) total = _room;
    // Erase as we go, 64 KB at a time, so the screen can show progress.
    for (uint32_t off = 0; off < total; off += CHUNK) {
        if (esp_partition_erase_range(p, _base + off, min(CHUNK, total - off)) != ESP_OK) {
            *why = "flash erase failed"; return false;
        }
        uint32_t from = off < DATA_OFF ? DATA_OFF : off;
        for (uint32_t w = from; w < off + CHUNK && w < DATA_OFF + want.size; w += BUF) {
            size_t want_n = min((uint32_t)BUF, DATA_OFF + want.size - w);
            size_t n = f.read(buf, want_n);
            // A short read would shift everything after it, so it's a failure too.
            if (n != want_n) { *why = "SD read failed"; return false; }
            if (esp_partition_write(p, _base + w, buf, n) != ESP_OK) { *why = "flash write failed"; return false; }
        }
        if (progress) progress((int)((off + CHUNK) * 100ULL / total));
        vTaskDelay(1);
    }
    esp_partition_write(p, _base, &want, sizeof(want));    // header last
    return true;
}

// Model bytes in memory, or nullptr. Call after sync() succeeded.
inline const uint8_t* map() {
    if (_map) return _map;
    const esp_partition_t* p = part();
    if (!p) return nullptr;
    Header h; esp_partition_read(p, _base, &h, sizeof(h));
    if (h.magic != MAGIC || h.size + DATA_OFF > _room) return nullptr;
    const void* ptr = nullptr;
    if (esp_partition_mmap(p, _base, DATA_OFF + h.size, SPI_FLASH_MMAP_DATA, &ptr, &_handle) != ESP_OK) return nullptr;
    _map = (const uint8_t*)ptr + DATA_OFF;
    _mapSize = h.size;
    return _map;
}

// Bytes of model behind map().
inline uint32_t mapSize() { return _map ? _mapSize : 0; }

} // namespace ModelFlash
