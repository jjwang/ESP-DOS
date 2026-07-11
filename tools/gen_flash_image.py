#!/usr/bin/env python3
"""Generate QEMU flash image + eFuse image from PlatformIO build output."""

import os
import binascii

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(PROJ, '.pio', 'build', 'esp32-s3-dev')

PARTITIONS = [
    (0x00000, "bootloader.bin"),
    (0x08000, "partitions.bin"),
    (0x0D000, "ota_data_initial.bin"),
    (0x10000, "firmware.bin"),
    (0x410000, "spiffs.bin"),  # from project root
]

FLASH_SIZE = 16 * 1024 * 1024  # 16 MB


def pad_to(img: bytearray, offset: int) -> None:
    if len(img) < offset:
        img.extend(b'\xff' * (offset - len(img)))


def gen_efuse():
    """Generate eFuse image for ESP32-S3 rev 0.3."""
    efuse = binascii.unhexlify(
        "0000000000000000000000000000000000000000000000000000000000000000"
        "00000000000000000000000000000000000000000000000c0000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        + "00" * 400
    )
    path = os.path.join(BUILD, "qemu_efuse.bin")
    with open(path, "wb") as f:
        f.write(efuse)
    print(f"  qemu_efuse.bin          @ efuse  ({len(efuse)} bytes)")
    return path


def gen_flash():
    img = bytearray()
    for offset, name in PARTITIONS:
        if name == "spiffs.bin":
            path = os.path.join(PROJ, name)
        else:
            path = os.path.join(BUILD, name)

        if not os.path.exists(path):
            print(f"  skip (not found): {name}")
            pad_to(img, offset + 0x1000)
            continue

        pad_to(img, offset)
        with open(path, 'rb') as f:
            data = f.read()
        img.extend(data)
        print(f"  {name:25s} @ 0x{offset:06x}  ({len(data)} bytes)")

    pad_to(img, FLASH_SIZE)
    img = img[:FLASH_SIZE]

    path = os.path.join(PROJ, 'flash.bin')
    with open(path, 'wb') as f:
        f.write(img)
    print(f"\nFlash image: {path} ({len(img)} bytes, {len(img)/1024/1024:.1f} MB)")
    return path


def main():
    gen_flash()
    gen_efuse()


if __name__ == '__main__':
    main()
