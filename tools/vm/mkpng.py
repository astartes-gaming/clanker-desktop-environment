#!/usr/bin/python3
"""Write the self-test wallpaper: mkpng.py OUT.png

A 1200x800 RGB PNG (no alpha, like most photos): green (20,120,60)
with a red frame and a yellow ring, using only the standard library.
"""
import struct, sys, zlib

W, H = 1200, 800
rows = []
for y in range(H):
    row = bytearray(b"\0")                      # filter: none
    for x in range(W):
        d2 = (x - 600) ** 2 + (y - 400) ** 2
        if x < 10 or y < 10 or x >= W - 10 or y >= H - 10:
            px = (220, 30, 30)
        elif 180 ** 2 <= d2 <= 200 ** 2:
            px = (240, 220, 0)
        else:
            px = (20, 120, 60)
        row += bytes(px)
    rows.append(bytes(row))

def chunk(kind, data):
    c = struct.pack(">I", len(data)) + kind + data
    return c + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"".join(rows), 6))
png += chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
