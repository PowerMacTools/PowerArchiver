#!/usr/bin/env python3
"""Build helpers for the application's Finder icon on classic Mac OS.

  mac_icon.py rez ICON.png OUT.r
      Writes the icon family (ICN#, icl8, icl4, ics#, ics8, ics4, ID 128)
      for a 32x32 RGBA PNG as Rez source.

  mac_icon.py bundle IN.bin OUT.bin
      Copies a MacBinary file with its "has bundle" Finder flag set, which
      the Finder needs before it will look at the BNDL resource. Retro68's
      Rez has no way to set it.

Only the standard library is used, so the PNG has to be 8-bit RGBA and not
interlaced.
"""

import struct
import sys
import zlib


def read_png(path):
    """Returns (width, height, rows of (r, g, b, a) tuples)."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat = 8, b""
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(
                ">IIBBBBB", body)
            if depth != 8 or color != 6 or interlace:
                sys.exit(f"{path}: must be 8-bit RGBA, not interlaced")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length

    raw = zlib.decompress(idat)
    stride = width * 4
    rows, prev = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = line[i - 4] if i >= 4 else 0
            up = prev[i]
            corner = prev[i - 4] if i >= 4 else 0
            if kind == 1:
                line[i] = (line[i] + left) & 0xff
            elif kind == 2:
                line[i] = (line[i] + up) & 0xff
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 0xff
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                pred = left if pa <= pb and pa <= pc else up if pb <= pc else corner
                line[i] = (line[i] + pred) & 0xff
        rows.append([tuple(line[x * 4:x * 4 + 4]) for x in range(width)])
        prev = line
    return width, height, rows


def halve(rows):
    """Scales an image down by two, averaging each 2x2 block."""
    out = []
    for y in range(0, len(rows), 2):
        line = []
        for x in range(0, len(rows[0]), 2):
            block = [rows[y][x], rows[y][x + 1], rows[y + 1][x],
                     rows[y + 1][x + 1]]
            line.append(tuple(sum(p[c] for p in block) // 4 for c in range(4)))
        out.append(line)
    return out


def palette_8bit():
    """The standard Mac 256 colour palette (clut 8)."""
    levels = [0xff, 0xcc, 0x99, 0x66, 0x33, 0x00]
    colors = [(r, g, b) for r in levels for g in levels for b in levels]
    colors.pop()  # black comes last, after the ramps
    ramp = [0xee, 0xdd, 0xbb, 0xaa, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11]
    colors += [(v, 0, 0) for v in ramp]
    colors += [(0, v, 0) for v in ramp]
    colors += [(0, 0, v) for v in ramp]
    colors += [(v, v, v) for v in ramp]
    colors.append((0, 0, 0))
    return colors


# The standard Mac 16 colour palette (clut 4).
PALETTE_4BIT = [
    (0xff, 0xff, 0xff), (0xfc, 0xf3, 0x05), (0xff, 0x64, 0x02),
    (0xdd, 0x08, 0x06), (0xf2, 0x08, 0x84), (0x46, 0x00, 0xa5),
    (0x00, 0x00, 0xd4), (0x02, 0xab, 0xea), (0x1f, 0xb7, 0x14),
    (0x00, 0x64, 0x11), (0x56, 0x2c, 0x05), (0x90, 0x71, 0x3a),
    (0xc0, 0xc0, 0xc0), (0x80, 0x80, 0x80), (0x40, 0x40, 0x40),
    (0x00, 0x00, 0x00),
]


def nearest(palette, pixel):
    r, g, b = pixel[:3]
    return min(range(len(palette)),
               key=lambda i: (palette[i][0] - r) ** 2 +
               (palette[i][1] - g) ** 2 + (palette[i][2] - b) ** 2)


def opaque(pixel):
    return pixel[3] >= 128


def bits(rows, test):
    """Packs a 1-bit image, most significant bit first."""
    out = bytearray()
    for line in rows:
        for x in range(0, len(line), 8):
            byte = 0
            for p in line[x:x + 8]:
                byte = byte << 1 | test(p)
            out.append(byte)
    return bytes(out)


def indexed(rows, palette, depth):
    """Packs a 4 or 8-bit indexed image. Transparent pixels are white, the
    first entry, since the mask hides them."""
    out = bytearray()
    per_byte = 8 // depth
    for line in rows:
        for x in range(0, len(line), per_byte):
            byte = 0
            for p in line[x:x + per_byte]:
                byte = byte << depth | (nearest(palette, p) if opaque(p) else 0)
            out.append(byte)
    return bytes(out)


def rez_data(kind, data):
    lines = [f"data '{kind}' (128) {{"]
    for i in range(0, len(data), 16):
        lines.append(f'\t$"{data[i:i + 16].hex().upper()}"')
    lines.append("};\n")
    return "\n".join(lines)


def make_rez(png, out):
    width, height, large = read_png(png)
    if (width, height) != (32, 32):
        sys.exit(f"{png}: must be 32x32, not {width}x{height}")
    small = halve(large)

    # 1-bit icons are black wherever the pixel is opaque and dark
    def dark(p):
        return opaque(p) and (p[0] * 299 + p[1] * 587 + p[2] * 114) < 128000

    clut8 = palette_8bit()
    resources = [
        ("ICN#", bits(large, dark) + bits(large, opaque)),
        ("icl8", indexed(large, clut8, 8)),
        ("icl4", indexed(large, PALETTE_4BIT, 4)),
        ("ics#", bits(small, dark) + bits(small, opaque)),
        ("ics8", indexed(small, clut8, 8)),
        ("ics4", indexed(small, PALETTE_4BIT, 4)),
    ]
    with open(out, "w") as f:
        f.write(f"/* Generated from {png} by mac_icon.py. */\n\n")
        for kind, data in resources:
            f.write(rez_data(kind, data))


def crc16(data):
    """CRC-16/XMODEM, which MacBinary II uses for its header."""
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1) & 0xffff
    return crc


def set_bundle(src, out):
    data = bytearray(open(src, "rb").read())
    data[73] |= 0x20  # high byte of the Finder flags; 0x2000 is kHasBundle
    struct.pack_into(">H", data, 124, crc16(data[:124]))
    open(out, "wb").write(data)


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "rez":
        make_rez(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "bundle":
        set_bundle(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)
