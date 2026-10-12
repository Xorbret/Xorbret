#pragma once
#include <esp_ota_ops.h>
#include <Preferences.h>
#include <cstring>

// OTA utilities for Games / Settings (PaperOS in the current partition, emulator in the other)

inline bool otaOtherHasDifferentApp() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* other   = esp_ota_get_next_update_partition(running);
    if (!other) return false;
    esp_app_desc_t a{}, b{};
    if (esp_ota_get_partition_description(other, &a) != ESP_OK) return false;
    if (esp_ota_get_partition_description(running, &b) != ESP_OK) return false;
    return memcmp(a.app_elf_sha256, b.app_elf_sha256, sizeof(a.app_elf_sha256)) != 0;
}

// otaOtherHasDifferentApp() only tells you *something* other than PaperOS
// itself is flashed to the OTA slot - it can't tell an emulator apart from
// an Arduboy game binary, since both simply have a different build hash.
// otaSlotApp()/otaSlotHasEmulator() track that separately via Preferences,
// which every place that flashes something to the OTA slot must keep in sync.
inline String otaSlotApp() {
    Preferences p;
    p.begin("app", true);
    String v = p.getString("OtaApp", "");
    p.end();
    return v;
}

inline bool otaSlotHasEmulator() {
    return otaSlotApp() == "emu";
}

inline bool otaEraseEmulatorSlot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* other   = esp_ota_get_next_update_partition(running);
    if (!other) return false;

    esp_err_t err = esp_partition_erase_range(other, 0, other->size);
    if (err != ESP_OK) return false;

    if (esp_ota_get_boot_partition() == other) {
        esp_ota_set_boot_partition(running);
    }

    Preferences p;
    p.begin("app", false);
    p.remove("EmulInstalled"); // old key, harmless to keep clearing it too
    p.remove("OtaApp");
    p.remove("RomToLoad");
    p.end();
    return true;
}
