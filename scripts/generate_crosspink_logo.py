#!/usr/bin/env python3
"""Generate CrossPink's 120 px monochrome boot logo and source assets.

The firmware uses a packed 1-bit header; the matching SVG and PNG are kept for
documentation and the user-guide EPUB.  The mark is intentionally built from
hard-edged page ribbons: it stays recognisable on a dithered e-ink panel and
does not rely on pink colour being available.
"""

from __future__ import annotations

import struct
import zlib
from pathlib import Path


WIDTH = HEIGHT = 120
ROOT = Path(__file__).resolve().parents[1]
IMAGES = ROOT / "src" / "images"

# Two crossed page ribbons, followed by a subtle two-page baseline. Coordinates
# are deliberately integral so the 1-bit firmware asset has crisp, stable edges.
POLYGONS = (
    ((18, 27), (33, 18), (102, 87), (87, 102)),
    ((87, 18), (102, 27), (33, 102), (18, 87)),
    ((20, 103), (58, 111), (58, 116), (20, 108)),
    ((62, 111), (100, 103), (100, 108), (62, 116)),
)


def contains(polygon: tuple[tuple[int, int], ...], x: float, y: float) -> bool:
    """Return whether the pixel centre lies inside a polygon."""
    inside = False
    previous_x, previous_y = polygon[-1]
    for current_x, current_y in polygon:
        crosses = (current_y > y) != (previous_y > y)
        if crosses and x < (previous_x - current_x) * (y - current_y) / (previous_y - current_y) + current_x:
            inside = not inside
        previous_x, previous_y = current_x, current_y
    return inside


def pixels() -> list[list[bool]]:
    return [
        [any(contains(polygon, x + 0.5, y + 0.5) for polygon in POLYGONS) for x in range(WIDTH)]
        for y in range(HEIGHT)
    ]


def write_svg() -> None:
    paths = "\n".join(
        "  <path d=\"M " + " L ".join(f"{x} {y}" for x, y in polygon) + " Z\" fill=\"black\"/>"
        for polygon in POLYGONS
    )
    (IMAGES / "logo.svg").write_text(
        "<svg width=\"120\" height=\"120\" viewBox=\"0 0 120 120\" fill=\"none\" "
        "xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <title>CrossPink crossed-page mark</title>\n"
        "  <rect width=\"120\" height=\"120\" fill=\"white\"/>\n"
        f"{paths}\n"
        "</svg>\n",
        encoding="utf-8",
    )


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_png(bitmap: list[list[bool]]) -> None:
    raw = bytearray()
    for row in bitmap:
        raw.append(0)  # PNG filter: None
        for black in row:
            raw.extend((0, 0, 0) if black else (255, 255, 255))
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0))
    png += png_chunk(b"IDAT", zlib.compress(bytes(raw), level=9))
    png += png_chunk(b"IEND", b"")
    (IMAGES / "Logo120.png").write_bytes(png)


def write_header(bitmap: list[list[bool]]) -> None:
    lines = [
        "#pragma once",
        "#include <cstdint>",
        "",
        "// CrossPink crossed-page mark, 120x120 px, packed 1-bit MSB-first.",
        "static const uint8_t Logo120[] = {",
    ]
    for row in bitmap:
        packed = []
        for byte_index in range(0, WIDTH, 8):
            value = 0
            for bit in range(8):
                # The renderer treats one bits as white and zero bits as black.
                if not row[byte_index + bit]:
                    value |= 1 << (7 - bit)
            packed.append(f"0x{value:02x}")
        lines.append("    " + ", ".join(packed) + ",")
    lines.append("};")
    lines.append("")
    (IMAGES / "Logo120.h").write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    bitmap = pixels()
    write_svg()
    write_png(bitmap)
    write_header(bitmap)


if __name__ == "__main__":
    main()
