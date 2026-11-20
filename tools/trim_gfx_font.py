#!/usr/bin/env python3
"""Riduce un font Adafruit GFX (.h) a un intervallo di caratteri contiguo.

Le bitmap e le metriche dei glifi tenuti restano identiche byte per byte:
cambiano solo gli offset (ribasati) e first/last del GFXfont.

    python3 tools/trim_gfx_font.py lib/fonts/Font.h 0x30 0x3A
"""
import re
import sys


def parse(src):
    bitmaps = re.search(r"const uint8_t (\w+)\[\] PROGMEM = \{(.*?)\};", src, re.S)
    glyphs = re.search(r"const GFXglyph (\w+)\[\] PROGMEM = \{(.*?)\};", src, re.S)
    font = re.search(
        r"const GFXfont (\w+) PROGMEM = \{\s*\(uint8_t\s*\*\)\w+,\s*\(GFXglyph \*\)\w+,\s*"
        r"(0x[0-9A-Fa-f]+|\d+),\s*(0x[0-9A-Fa-f]+|\d+),\s*(\d+)\s*\};", src, re.S)
    data = [int(b, 16) for b in re.findall(r"0x[0-9A-Fa-f]{2}", bitmaps.group(2))]
    rows = [tuple(int(v) for v in g) for g in
            re.findall(r"\{\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}", glyphs.group(2))]
    first, last, y_adv = int(font.group(2), 0), int(font.group(3), 0), int(font.group(4))
    assert len(rows) == last - first + 1, "glyph table does not match first/last"
    return bitmaps.group(1), glyphs.group(1), font.group(1), data, rows, first, y_adv


def glyph_bytes(row):
    return (row[1] * row[2] + 7) // 8


def trim(src, lo, hi):
    bname, gname, fname, data, rows, first, y_adv = parse(src)
    keep = rows[lo - first:hi - first + 1]
    start = keep[0][0]
    end = max(r[0] + glyph_bytes(r) for r in keep)
    out_bitmap = data[start:end]
    lines = [f"// Ridotto con tools/trim_gfx_font.py ai caratteri 0x{lo:02X}..0x{hi:02X}",
             f"const uint8_t {bname}[] PROGMEM = {{"]
    for i in range(0, len(out_bitmap), 12):
        chunk = ", ".join(f"0x{b:02X}" for b in out_bitmap[i:i + 12])
        lines.append(f"  {chunk},")
    lines[-1] = lines[-1].rstrip(",")
    lines.append("};\n")
    lines.append(f"const GFXglyph {gname}[] PROGMEM = {{")
    for i, r in enumerate(keep):
        c = lo + i
        sep = "," if i < len(keep) - 1 else " };"
        lines.append(f"  {{ {r[0] - start:5d}, {r[1]:3d}, {r[2]:3d}, {r[3]:3d}, {r[4]:4d}, {r[5]:4d} }}{sep}   // 0x{c:02X} '{chr(c)}'")
    lines.append("")
    lines.append(f"const GFXfont {fname} PROGMEM = {{")
    lines.append(f"  (uint8_t  *){bname},")
    lines.append(f"  (GFXglyph *){gname},")
    lines.append(f"  0x{lo:02X}, 0x{hi:02X}, {y_adv} }};")
    lines.append(f"\n// Approx. {len(out_bitmap) + 7 * len(keep) + 7} bytes\n")
    return "\n".join(lines)


if __name__ == "__main__":
    path, lo, hi = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
    with open(path) as f:
        src = f.read()
    out = trim(src, lo, hi)
    with open(path, "w") as f:
        f.write(out)
