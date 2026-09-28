#!/usr/bin/env python3
"""Convert the renderer's raw XRGB8888 frames to PNGs masked to the round glass,
and build a contact sheet.  No third-party modules required."""
import glob
import math
import os
import struct
import sys
import zlib

W = H = 720
R = 360


def png_write(path, width, height, rows):
    """rows: list of bytes, each row RGBA."""
    raw = b"".join(b"\x00" + r for r in rows)
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def convert(raw_path):
    data = open(raw_path, "rb").read()
    rows = []
    for y in range(H):
        row = bytearray(W * 4)
        dy = y + 0.5 - R
        for x in range(W):
            dx = x + 0.5 - R
            d = math.sqrt(dx * dx + dy * dy)
            i = (y * W + x) * 4
            b, g, r = data[i], data[i + 1], data[i + 2]
            if d > R:
                a = 0
            elif d > R - 1.5:
                a = int(255 * (R - d) / 1.5)
            else:
                a = 255
            if R - 10 < d <= R:      # a thin bezel so the edge reads as glass
                k = 0.35
                r, g, b = int(r * k), int(g * k), int(b * k)
            row[x * 4:x * 4 + 4] = bytes((r, g, b, a))
        rows.append(bytes(row))
    return rows


def main():
    raws = sorted(glob.glob("out/*.raw"))
    frames = []
    for p in raws:
        rows = convert(p)
        out = p[:-4] + ".png"
        png_write(out, W, H, rows)
        print("png", out)
        frames.append((os.path.basename(p)[:-4], rows))

    sheet_for("out/contact_sheet.png", [f for f in frames if not f[0].startswith("anim_")], 4)
    sheet_for("out/motion_sheet.png", [f for f in frames if f[0].startswith("anim_")], 5)


def sheet_for(path, frames, cols):
    # contact sheet scaled 1:2
    sw, sh = W // 2, H // 2
    pad = 24
    n = len(frames)
    rowsn = (n + cols - 1) // cols
    cw, ch = cols * (sw + pad) + pad, rowsn * (sh + pad + 26) + pad
    sheet = [bytearray(b"\x12\x14\x1a\xff" * cw) for _ in range(ch)]
    for idx, (name, rows) in enumerate(frames):
        ox = pad + (idx % cols) * (sw + pad)
        oy = pad + (idx // cols) * (sh + pad + 26)
        for y in range(sh):
            src = rows[y * 2]
            dst = sheet[oy + y]
            for x in range(sw):
                px = src[x * 8:x * 8 + 4]
                if px[3] == 0:
                    continue
                dst[(ox + x) * 4:(ox + x) * 4 + 4] = px
    png_write(path, cw, ch, [bytes(r) for r in sheet])
    print("png %s (%d frames)" % (path, n))


if __name__ == "__main__":
    main()
