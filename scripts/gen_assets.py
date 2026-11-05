#!/usr/bin/env python3
"""Write the 3DS icon and banner used by bannertool and makerom."""

import struct
import zlib
from pathlib import Path

# 5x7 glyphs, one string per row, '#' is ink.
FONT = {
    " ": [".....", ".....", ".....", ".....", ".....", ".....", "....."],
    ".": [".....", ".....", ".....", ".....", ".....", ".##..", ".##.."],
    "2": ["####.", "...#.", "...#.", ".###.", "#....", "#....", "####."],
    "A": [".#...", "#.#..", "#.#..", "####.", "#..#.", "#..#.", "#..#."],
    "C": [".###.", "#....", "#....", "#....", "#....", "#....", ".###."],
    "D": ["###..", "#..#.", "#..#.", "#..#.", "#..#.", "#..#.", "###.."],
    "E": ["####.", "#....", "#....", "###..", "#....", "#....", "####."],
    "H": ["#..#.", "#..#.", "#..#.", "####.", "#..#.", "#..#.", "#..#."],
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "####."],
    "N": ["#..#.", "##.#.", "##.#.", "#.##.", "#.##.", "#..#.", "#..#."],
    "O": [".##..", "#..#.", "#..#.", "#..#.", "#..#.", "#..#.", ".##.."],
    "R": ["###..", "#..#.", "#..#.", "###..", "#.#..", "#..#.", "#..#."],
    "S": [".###.", "#....", "#....", ".##..", "...#.", "...#.", "###.."],
    "U": ["#..#.", "#..#.", "#..#.", "#..#.", "#..#.", "#..#.", ".##.."],
    "V": ["#..#.", "#..#.", "#..#.", "#..#.", "#..#.", ".##..", ".#..."],
    "W": ["#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"],
    "Y": ["#..#.", "#..#.", ".##..", ".#...", ".#...", ".#...", ".#..."],
    "a": [".....", ".....", ".##..", "...#.", ".###.", "#..#.", ".###."],
    "c": [".....", ".....", ".###.", "#....", "#....", "#....", ".###."],
    "e": [".....", ".....", ".##..", "#..#.", "####.", "#....", ".###."],
    "h": ["#....", "#....", "###..", "#..#.", "#..#.", "#..#.", "#..#."],
    "l": [".#...", ".#...", ".#...", ".#...", ".#...", ".#...", "..#.."],
    "n": [".....", ".....", "##...", "#.#..", "#.#..", "#.#..", "#.#.."],
    "o": [".....", ".....", ".##..", "#..#.", "#..#.", "#..#.", ".##.."],
    "r": [".....", ".....", "#.##.", "##...", "#....", "#....", "#...."],
    "s": [".....", ".....", ".###.", "#....", ".##..", "...#.", "###.."],
    "u": [".....", ".....", "#..#.", "#..#.", "#..#.", "#..#.", ".###."],
    "v": [".....", ".....", "#..#.", "#..#.", "#..#.", ".##..", ".#..."],
    "w": [".....", ".....", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"],
    "y": [".....", ".....", "#..#.", "#..#.", ".###.", "...#.", "##..."],
}


def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, width, height, rgba):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(rgba[y * width * 4 : (y + 1) * width * 4])
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def put(px, w, h, x, y, color):
    if 0 <= x < w and 0 <= y < h:
        i = (y * w + x) * 4
        px[i : i + 4] = bytes(color)


def fill_rect(px, w, h, x, y, rw, rh, color):
    for yy in range(y, y + rh):
        for xx in range(x, x + rw):
            put(px, w, h, xx, yy, color)


def text(px, w, h, s, x, y, scale, color):
    cx = x
    for ch in s:
        glyph = FONT.get(ch, FONT[" "])
        for row, bits in enumerate(glyph):
            for col, bit in enumerate(bits):
                if bit == "#":
                    fill_rect(px, w, h, cx + col * scale, y + row * scale, scale, scale, color)
        cx += 6 * scale


def banner():
    w, h = 256, 128
    px = bytearray(w * h * 4)
    for y in range(h):
        t = y / (h - 1)
        color = (
            int(8 + 10 * t),
            int(18 + 28 * t),
            int(36 + 40 * t),
            255,
        )
        fill_rect(px, w, h, 0, y, w, 1, color)
    fill_rect(px, w, h, 0, 0, w, 4, (40, 190, 220, 255))
    fill_rect(px, w, h, 0, h - 4, w, 4, (40, 190, 220, 255))
    # Two console cards and arrows, so the banner is a picture rather than a block.
    fill_rect(px, w, h, 18, 36, 36, 28, (36, 170, 220, 255))
    fill_rect(px, w, h, 22, 40, 28, 16, (236, 246, 252, 255))
    fill_rect(px, w, h, 70, 64, 36, 28, (36, 170, 220, 255))
    fill_rect(px, w, h, 74, 68, 28, 16, (236, 246, 252, 255))
    fill_rect(px, w, h, 56, 46, 12, 3, (236, 246, 252, 255))
    fill_rect(px, w, h, 64, 42, 4, 4, (236, 246, 252, 255))
    fill_rect(px, w, h, 56, 78, 12, 3, (236, 246, 252, 255))
    fill_rect(px, w, h, 56, 78, 4, 4, (236, 246, 252, 255))
    text(px, w, h, "2DSync", 118, 34, 3, (244, 250, 255, 255))
    text(px, w, h, "Your saves. Anywhere.", 118, 68, 1, (90, 210, 230, 255))
    return w, h, px


def icon():
    w, h = 48, 48
    px = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            put(px, w, h, x, y, (10, 22, 38, 255))
    fill_rect(px, w, h, 4, 4, 40, 40, (28, 150, 200, 255))
    fill_rect(px, w, h, 8, 8, 32, 32, (10, 28, 46, 255))
    text(px, w, h, "2D", 10, 16, 2, (244, 250, 255, 255))
    return w, h, px


def write_wav(path):
    rate = 22050
    frames = rate
    data = b"\x00\x00" * frames
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
    header += b"data" + struct.pack("<I", len(data))
    Path(path).write_bytes(header + data)


def main():
    root = Path(__file__).resolve().parents[1] / "3ds-client"
    write_png(root / "banner.png", *banner())
    write_png(root / "icon.png", *icon())
    write_wav(root / "banner.wav")
    print(f"wrote assets in {root}")


if __name__ == "__main__":
    main()
