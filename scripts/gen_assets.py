#!/usr/bin/env python3
"""Write the 3DS icon and banner used by bannertool and makerom."""

import struct
import zlib
from pathlib import Path


def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, width, height, rgba_fn):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(rgba_fn(x, y))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def paint(x, y, w, h):
    nx = x / max(1, w - 1)
    ny = y / max(1, h - 1)
    bg = (14, 22, 36, 255)
    # Cyan rounded block.
    cx, cy = 0.50, 0.48
    if abs(nx - cx) < 0.28 and abs(ny - cy) < 0.30:
        return (36, 170, 220, 255)
    if abs(nx - cx) < 0.18 and abs(ny - cy) < 0.16:
        return (236, 246, 252, 255)
    return bg


def write_wav(path):
    rate = 22050
    seconds = 1
    frames = rate * seconds
    data = b"\x00\x00" * frames
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    header += b"data" + struct.pack("<I", len(data))
    Path(path).write_bytes(header + data)


root = Path(__file__).resolve().parents[1] / "3ds-client"
write_png(root / "icon.png", 48, 48, lambda x, y: paint(x, y, 48, 48))
write_png(root / "banner.png", 256, 128, lambda x, y: paint(x, y, 256, 128))
write_wav(root / "banner.wav")
print(f"wrote assets in {root}")
