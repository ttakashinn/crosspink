#!/usr/bin/env python3
"""Pin every axis of a variable font and write a reproducible static face."""

from __future__ import annotations

import argparse
import os
import tempfile
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont


def parse_axis(value: str) -> tuple[str, float]:
    try:
        name, raw = value.split("=", 1)
        return name, float(raw)
    except ValueError as error:
        raise argparse.ArgumentTypeError("axis must use NAME=VALUE") from error


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--axis", action="append", type=parse_axis, required=True)
    args = parser.parse_args()

    axes = dict(args.axis)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    source = TTFont(args.source)
    try:
        # Intermediate e-ink weights such as 450 are valid axis coordinates but
        # are not named STAT instances. File names and generated C identifiers
        # carry the face identity, so preserving the variable name table is safe.
        instance = instantiateVariableFont(source, axes, updateFontNames=False, optimize=False)
        descriptor, temporary_name = tempfile.mkstemp(suffix=".ttf", dir=args.output.parent)
        os.close(descriptor)
        temporary = Path(temporary_name)
        try:
            instance.save(temporary)
            temporary.replace(args.output)
        finally:
            instance.close()
            temporary.unlink(missing_ok=True)
    finally:
        source.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
