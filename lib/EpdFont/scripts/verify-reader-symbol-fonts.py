#!/usr/bin/env python3
"""Verify curated symbols and common IPA glyphs in every Noto reader face."""

from pathlib import Path
import re
import sys


SCRIPT_DIR = Path(__file__).resolve().parent
FONT_DIR = SCRIPT_DIR.parent / "builtinFonts"
SYMBOLS = (
    0x2190, 0x2192, 0x2194,
    0x25A0, 0x25A1, 0x25C6, 0x25C7, 0x25CB, 0x25CF,
    0x2605, 0x2606, 0x2661, 0x2665, 0x2713, 0x2717,
)
IPA_SAMPLES = tuple(map(ord, "ɪʊəæʌɑɔɒɜŋβθχðʃʒɡɹˈˌːʰ"))


def header_intervals(path: Path, font_name: str) -> list[tuple[int, int]]:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        rf"static const EpdUnicodeInterval {re.escape(font_name)}Intervals\[\] = \{{(.*?)\n\}};",
        text,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"không đọc được interval array trong {path.name}")
    return [
        (int(first, 16), int(last, 16))
        for first, last in re.findall(r"\{\s*0x([0-9A-Fa-f]+),\s*0x([0-9A-Fa-f]+),", match.group(1))
    ]


def main() -> int:
    errors: list[str] = []
    for family in ("notoserif", "notosans"):
        for size in (12, 14, 16, 18):
            for style in ("regular", "italic", "bold", "bolditalic"):
                name = f"{family}_{size}_{style}"
                path = FONT_DIR / f"{name}.h"
                if not path.is_file():
                    errors.append(f"thiếu {path.name}")
                    continue
                text = path.read_text(encoding="utf-8")
                if "--fallback-only-additional" not in text.split("*/", 1)[0]:
                    errors.append(f"{path.name} không khóa fallback vào curated intervals")
                try:
                    intervals = header_intervals(path, name)
                except ValueError as error:
                    errors.append(str(error))
                    continue
                required = SYMBOLS + (IPA_SAMPLES if family == "notosans" else ())
                missing = [
                    f"U+{cp:04X}" for cp in required
                    if not any(first <= cp <= last for first, last in intervals)
                ]
                if missing:
                    errors.append(f"{path.name} thiếu {', '.join(missing)}")

    if errors:
        print("Reader symbol verification failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(
        f"Verified {len(SYMBOLS)} curated symbols in all 32 Noto faces and "
        f"{len(IPA_SAMPLES)} IPA glyphs in all 16 Noto Sans faces."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
