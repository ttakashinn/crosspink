#!/usr/bin/env python3
"""Print host-specific compiler/linker flags for native simulator builds."""

from __future__ import annotations

import platform
import shlex
import shutil
import subprocess
import sys


def main() -> int:
    sdl_config = shutil.which("sdl2-config")
    if sdl_config is None:
        print(
            "sdl2-config not found; install SDL2 (brew install sdl2 or the host package equivalent)",
            file=sys.stderr,
        )
        return 1

    result = subprocess.run(
        [sdl_config, "--cflags", "--libs"],
        check=True,
        capture_output=True,
        text=True,
    )
    flags = shlex.split(result.stdout)
    if platform.system() == "Linux":
        flags.extend(("-lssl", "-lcrypto", "-Wno-deprecated-declarations", "-Wno-narrowing"))

    print(" ".join(shlex.quote(flag) for flag in flags))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
