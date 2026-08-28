#!/usr/bin/env python3
"""Bootstrap and verify the local toolchain used by Codex and contributors."""

from __future__ import annotations

import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Sequence


ROOT = Path(__file__).resolve().parents[1]
VENV = ROOT / ".venv"
PIOARDUINO_CORE = (
    "https://github.com/pioarduino/platformio-core/archive/refs/tags/v6.1.19.zip"
)
# platform-espressif32 imports these before its private build environment is
# initialized, so a clean PlatformIO environment must provide them up front.
PIOARDUINO_PLATFORM_DEPS = ("littlefs-python>=0.16.0", "fatfs-ng>=0.1.14")
FORMAT_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}
FORMAT_EXCLUDES = (
    "lib/EpdFont/builtinFonts/",
    "lib/Epub/Epub/hyphenation/generated/",
    "lib/uzlib/",
    "lib/miniz/third_party/",
)


class SetupError(RuntimeError):
    pass


def run(command: Sequence[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    print(f"+ {shlex.join(str(part) for part in command)}", flush=True)
    return subprocess.run(
        [str(part) for part in command], cwd=ROOT, check=check, text=True
    )


def capture(command: Sequence[str]) -> str:
    return subprocess.check_output(
        [str(part) for part in command], cwd=ROOT, text=True, stderr=subprocess.STDOUT
    ).strip()


def venv_executable(name: str) -> Path:
    directory = "Scripts" if os.name == "nt" else "bin"
    suffix = ".exe" if os.name == "nt" else ""
    return VENV / directory / f"{name}{suffix}"


def find_tool(*names: str, prefer_venv: bool = False) -> str | None:
    if prefer_venv:
        for name in names:
            local = venv_executable(name)
            if local.is_file():
                return str(local)
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return None


def require_tool(*names: str, prefer_venv: bool = False) -> str:
    found = find_tool(*names, prefer_venv=prefer_venv)
    if not found:
        raise SetupError(f"Không tìm thấy công cụ: {' hoặc '.join(names)}")
    return found


def bootstrap(_: argparse.Namespace) -> None:
    if sys.version_info < (3, 10):
        raise SetupError("Cần Python 3.10 trở lên cho platform-espressif32 hiện tại")

    run(["git", "submodule", "update", "--init", "--recursive"])

    uv = find_tool("uv")
    python = venv_executable("python")
    if not python.exists():
        if uv:
            run([uv, "venv", "--python", sys.executable, str(VENV)])
        else:
            run([sys.executable, "-m", "venv", str(VENV)])

    if uv:
        run(
            [
                uv,
                "pip",
                "install",
                "--python",
                str(python),
                "--upgrade",
                PIOARDUINO_CORE,
                *PIOARDUINO_PLATFORM_DEPS,
                "-r",
                "requirements.txt",
            ]
        )
    else:
        run([str(python), "-m", "pip", "install", "--upgrade", "pip"])
        run(
            [
                str(python),
                "-m",
                "pip",
                "install",
                "--upgrade",
                PIOARDUINO_CORE,
                *PIOARDUINO_PLATFORM_DEPS,
                "-r",
                "requirements.txt",
            ]
        )

    print("\nBootstrap hoàn tất. Chạy `python3 scripts/codex_setup.py doctor`.")


def version_line(tool: str, *arguments: str) -> str:
    try:
        return capture([tool, *arguments]).splitlines()[0]
    except (subprocess.CalledProcessError, IndexError):
        return "không đọc được phiên bản"


def doctor(_: argparse.Namespace) -> None:
    rows: list[tuple[str, bool, str]] = []

    rows.append(("Python >= 3.10", sys.version_info >= (3, 10), sys.version.split()[0]))
    rows.append(("uv (tăng tốc cài đặt)", bool(find_tool("uv")), find_tool("uv") or "tùy chọn"))

    for label, names, prefer_venv, arguments in (
        ("PlatformIO", ("pio",), True, ("--version",)),
        ("CMake", ("cmake",), False, ("--version",)),
        ("Ninja (build nhanh)", ("ninja",), False, ("--version",)),
        ("clang-format >= 21", ("clang-format-21", "clang-format"), False, ("--version",)),
    ):
        tool = find_tool(*names, prefer_venv=prefer_venv)
        detail = version_line(tool, *arguments) if tool else "chưa có"
        ok = bool(tool)
        if label.startswith("clang-format") and tool:
            match = re.search(r"(\d+)(?:\.\d+)+", detail)
            ok = bool(match and int(match.group(1)) >= 21)
        rows.append((label, ok, detail))

    venv_python = venv_executable("python")
    if venv_python.is_file():
        dependency_check = subprocess.run(
            [str(venv_python), "-c", "import fatfs, littlefs"],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        build_deps_ok = dependency_check.returncode == 0
    else:
        build_deps_ok = False
    rows.append(
        (
            "PlatformIO build deps",
            build_deps_ok,
            "fatfs + littlefs" if build_deps_ok else "chạy bootstrap để cài",
        )
    )

    try:
        submodules = capture(["git", "submodule", "status", "--recursive"])
        submodule_ok = bool(submodules) and all(
            not line.startswith(("-", "+", "U")) for line in submodules.splitlines()
        )
        submodule_detail = "đã đồng bộ" if submodule_ok else "thiếu hoặc lệch commit"
    except subprocess.CalledProcessError:
        submodule_ok = False
        submodule_detail = "không đọc được trạng thái"
    rows.append(("Git submodules", submodule_ok, submodule_detail))

    width = max(len(label) for label, _, _ in rows)
    for label, ok, detail in rows:
        state = "OK" if ok else "WARN"
        print(f"{state:4}  {label:<{width}}  {detail}")

    required = {
        "Python >= 3.10",
        "PlatformIO",
        "PlatformIO build deps",
        "CMake",
        "clang-format >= 21",
        "Git submodules",
    }
    failed = [label for label, ok, _ in rows if label in required and not ok]
    if failed:
        raise SetupError("Thiếu yêu cầu nền: " + ", ".join(failed))


def unit_tests(_: argparse.Namespace | None = None) -> None:
    cmake = require_tool("cmake")
    build_dir = ROOT / "build" / "test"
    configure = [cmake, "-S", "test", "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release"]
    if not (build_dir / "CMakeCache.txt").exists() and find_tool("ninja"):
        configure.extend(["-G", "Ninja"])
    run(configure)
    run([cmake, "--build", str(build_dir), "--parallel"])
    run(["ctest", "--test-dir", str(build_dir), "--output-on-failure", "-j"])


def firmware_build(environments: Sequence[str]) -> None:
    pio = require_tool("pio", prefer_venv=True)
    command = [pio, "run"]
    for environment in environments:
        command.extend(["-e", environment])
    run(command)


def build(args: argparse.Namespace) -> None:
    firmware_build(args.env)


def format_check() -> None:
    formatter = require_tool("clang-format-21", "clang-format")
    detail = version_line(formatter, "--version")
    match = re.search(r"(\d+)(?:\.\d+)+", detail)
    if not match or int(match.group(1)) < 21:
        raise SetupError(f"clang-format phải từ 21 trở lên; hiện tại: {detail}")

    tracked = capture(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"]
    ).splitlines()
    files = [
        path
        for path in tracked
        if Path(path).suffix in FORMAT_SUFFIXES
        and not path.startswith(FORMAT_EXCLUDES)
    ]
    for start in range(0, len(files), 100):
        run([formatter, "--dry-run", "--Werror", "-style=file", *files[start : start + 100]])


def static_check() -> None:
    pio = require_tool("pio", prefer_venv=True)
    run(
        [
            pio,
            "check",
            "--fail-on-defect",
            "low",
            "--fail-on-defect",
            "medium",
            "--fail-on-defect",
            "high",
        ]
    )


def verify(args: argparse.Namespace) -> None:
    unit_tests()
    if args.level == "quick":
        firmware_build(["default"])
        return

    format_check()
    static_check()
    firmware_build(["default", "sticky"])


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description="Cài đặt và kiểm chứng toolchain phát triển CrossPoint"
    )
    commands = result.add_subparsers(dest="command", required=True)

    bootstrap_parser = commands.add_parser("bootstrap", help="Tạo .venv và cài dependency")
    bootstrap_parser.set_defaults(handler=bootstrap)

    doctor_parser = commands.add_parser("doctor", help="Kiểm tra toolchain hiện tại")
    doctor_parser.set_defaults(handler=doctor)

    test_parser = commands.add_parser("test", help="Build và chạy host unit tests")
    test_parser.set_defaults(handler=unit_tests)

    build_parser = commands.add_parser("build", help="Build firmware bằng PlatformIO")
    build_parser.add_argument(
        "--env", action="append", default=[], help="PlatformIO env; lặp lại để build nhiều env"
    )
    build_parser.set_defaults(handler=build)

    verify_parser = commands.add_parser("verify", help="Chạy bộ kiểm tra trước khi bàn giao")
    verify_parser.add_argument("--level", choices=("quick", "full"), default="quick")
    verify_parser.set_defaults(handler=verify)
    return result


def main() -> int:
    args = parser().parse_args()
    if args.command == "build" and not args.env:
        args.env = ["default"]
    try:
        args.handler(args)
    except (SetupError, subprocess.CalledProcessError) as error:
        print(f"\nLỗi: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
