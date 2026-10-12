/*
 * minimp3 - minimalistic MP3 decoder
 * https://github.com/lieff/minimp3
 * Public Domain / CC0
 * Stripped to frame decoder only for ESP32 embedded use
 */
#pragma once
#include <stdint.h>
#include <string.h>

// Use the built-in ESP-IDF helix MP3 decoder if available.
// Otherwise fall back to a stub with conversion.
// On ESP32-S3 with espressif32 @ 6.7.0 (IDF 4.4), helix is available via:
// esp_audio, or via direct calls

// Check whether esp_audio is present
#if __has_include("esp_audio.h") || __has_include(<esp_audio.h>)
  #define HAS_ESP_AUDIO 1
#else
  #define HAS_ESP_AUDIO 0
#endif

// Simple MP3 frame sync finder
// Looks for the 0xFFE0 (11-bit) sync word
static inline int mp3_find_sync(const uint8_t* buf, int len) {
    for (int i = 0; i < len - 1; i++) {
        if ((buf[i] == 0xFF) && ((buf[i+1] & 0xE0) == 0xE0)) return i;
    }
    return -1;
}

// Approximate MP3 frame size from the header
static inline int mp3_frame_size(const uint8_t* hdr) {
    if ((hdr[0] != 0xFF) || ((hdr[1] & 0xE0) != 0xE0)) return -1;
    static const int bitrate_table[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const int srate_table[4] = {44100, 48000, 32000, 0};
    int layer = (hdr[1] >> 1) & 3;  // 3=L1, 2=L2, 1=L3
    int br_idx = (hdr[2] >> 4) & 0xF;
    int sr_idx = (hdr[2] >> 2) & 3;
    int pad = (hdr[2] >> 1) & 1;
    if (layer != 1) return -1; // Layer3 (MP3) only
    int br = bitrate_table[br_idx] * 1000;
    int sr = srate_table[sr_idx];
    if (br == 0 || sr == 0) return -1;
    return (144 * br / sr) + pad;
}
