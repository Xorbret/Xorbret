#!/usr/bin/env python3
"""
merge.py - builds a single combined PaperOS_full.bin

model.bin now lives on the SD card, not in this image - copy it to the
SD card's root yourself after flashing. This script only merges the
bootloader/partition-table/app binaries; spiffs and the model area are left
erased (0xFF) and get formatted on first boot / first ROM launch.

Usage:
    python merge.py

Output: PaperOS_full.bin - flash it at address 0x0000
"""

import os, sys

BUILD_DIR  = ".pio/build/paperos-cardputer"
OUTPUT     = "PaperOS_full.bin"
FLASH_SIZE = 0x800000   # 8MB

def find_boot_app0():
    candidates = [
        f"{BUILD_DIR}/boot_app0.bin",
        os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None

def main():
    print("=== PaperOS merge.py ===\n")

    missing = []
    for path in [f"{BUILD_DIR}/bootloader.bin",
                 f"{BUILD_DIR}/partitions.bin",
                 f"{BUILD_DIR}/firmware.bin"]:
        if not os.path.exists(path):
            missing.append(f"  {path}  (run: pio run -e paperos-cardputer)")
    if not find_boot_app0():
        missing.append("  boot_app0.bin  (run: pio run -e paperos-cardputer)")
    if missing:
        print("Missing files:")
        for m in missing: print(m)
        sys.exit(1)

    print(f"Creating 8MB flash image... ", end="", flush=True)
    flash = bytearray(b'\xFF' * FLASH_SIZE)
    print("OK")

    layout = [
        (0x000000, f"{BUILD_DIR}/bootloader.bin"),
        (0x008000, f"{BUILD_DIR}/partitions.bin"),
        (0x00E000, find_boot_app0()),
        (0x010000, f"{BUILD_DIR}/firmware.bin"),
    ]

    for offset, path in layout:
        with open(path, "rb") as f:
            data = f.read()
        label = os.path.basename(path)
        end = offset + len(data)
        if end > FLASH_SIZE:
            print(f"ERROR: {label} exceeds flash size!")
            sys.exit(1)
        flash[offset:end] = data
        print(f"  0x{offset:06X}: {label} ({len(data)/1024:.1f} KB)")

    with open(OUTPUT, "wb") as f:
        f.write(flash)

    print(f"\nDone: {OUTPUT} ({os.path.getsize(OUTPUT)/1024/1024:.1f} MB)")
    print(f"\nFlash with esptool:")
    print(f"  esptool.py --chip esp32s3 --port COM40 --baud 1500000 write_flash 0x0 {OUTPUT}")
    print(f"\nOr with ESPHome / M5Burner - select {OUTPUT}, address 0x0")
    print(f"\nDon't forget: copy model.bin to the SD card's root before booting myAI.")

if __name__ == "__main__":
    main()
