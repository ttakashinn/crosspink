#!/usr/bin/env python3
"""Build and verify deterministic CrossPoint EPUB render artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "test" / "epubs" / "render-reference" / "expected-manifest.json"
EPUB_PATH = ROOT / "test" / "epubs" / "crosspoint-render-reference-v1.0.epub"
GOLDEN_ROOT = ROOT / "test" / "golden" / "render"
ACTUAL_ROOT = ROOT / "build" / "render-lab" / "actual"
NONDETERMINISTIC_RESULT_FIELDS = {
    "firmware_version",
    "prewarm_ms",
    "bw_render_ms",
    "total_render_ms",
    "reported_min_free_heap",
    "reported_max_alloc_heap",
}


class RenderLabError(RuntimeError):
    pass


@dataclass(frozen=True)
class RenderCase:
    profile: dict[str, Any]
    checkpoint: dict[str, Any]
    cache_state: str

    @property
    def case_id(self) -> str:
        return f"{self.profile['id']}/{self.checkpoint['id']}/{self.cache_state}"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_keys(value: dict[str, Any], keys: tuple[str, ...], context: str) -> None:
    missing = [key for key in keys if key not in value]
    if missing:
        raise RenderLabError(f"{context} thiếu trường bắt buộc: {', '.join(missing)}")


def load_and_validate_manifest() -> dict[str, Any]:
    try:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RenderLabError(f"Không đọc được manifest {MANIFEST_PATH}: {exc}") from exc

    require_keys(
        manifest,
        ("fixture_id", "version", "primary_language", "simulator", "checkpoints", "render_profiles", "suites"),
        "manifest",
    )
    if manifest["primary_language"] != "vi":
        raise RenderLabError("Fixture render chuẩn phải dùng primary_language=vi")

    simulator = manifest["simulator"]
    require_keys(simulator, ("repository", "commit"), "simulator")
    commit = simulator["commit"]
    if not isinstance(commit, str) or len(commit) != 40 or any(ch not in "0123456789abcdef" for ch in commit):
        raise RenderLabError("simulator.commit phải là Git SHA-1 đầy đủ 40 ký tự")
    platformio_text = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    if f"{simulator['repository']}#{commit}" not in platformio_text:
        raise RenderLabError("platformio.ini không pin đúng simulator.repository và simulator.commit trong manifest")

    checkpoint_ids: set[str] = set()
    for checkpoint in manifest["checkpoints"]:
        require_keys(checkpoint, ("id", "href", "covers"), "checkpoint")
        if checkpoint["id"] in checkpoint_ids:
            raise RenderLabError(f"Checkpoint trùng id: {checkpoint['id']}")
        checkpoint_ids.add(checkpoint["id"])
        if "#" not in checkpoint["href"]:
            raise RenderLabError(f"Checkpoint {checkpoint['id']} phải dùng href + anchor")
        page_offset = checkpoint.get("page_offset", 0)
        if not isinstance(page_offset, int) or isinstance(page_offset, bool) or page_offset < 0:
            raise RenderLabError(f"Checkpoint {checkpoint['id']} có page_offset không hợp lệ")
        if "full_build" in checkpoint and not isinstance(checkpoint["full_build"], bool):
            raise RenderLabError(f"Checkpoint {checkpoint['id']} có full_build không hợp lệ")
        if "image_metrics" in checkpoint and not isinstance(checkpoint["image_metrics"], bool):
            raise RenderLabError(f"Checkpoint {checkpoint['id']} có image_metrics không hợp lệ")
        if checkpoint.get("image_metrics") and not checkpoint.get("full_build", False):
            raise RenderLabError(f"Checkpoint {checkpoint['id']} bật image_metrics phải bật full_build")
        if page_offset > 0 and not checkpoint.get("full_build", False):
            raise RenderLabError(f"Checkpoint {checkpoint['id']} có page_offset phải bật full_build")
        if checkpoint.get("structural_expectations") and not checkpoint.get("full_build", False):
            raise RenderLabError(f"Checkpoint {checkpoint['id']} có structural expectation phải bật full_build")

    profile_ids: set[str] = set()
    required_profile_keys = (
        "id",
        "environment",
        "viewport",
        "orientation",
        "text_antialiasing",
        "embedded_styles",
        "font_family",
        "font_point_size",
        "line_spacing",
        "paragraph_alignment",
        "screen_margin",
    )
    for profile in manifest["render_profiles"]:
        require_keys(profile, required_profile_keys, "render profile")
        if profile["id"] in profile_ids:
            raise RenderLabError(f"Render profile trùng id: {profile['id']}")
        profile_ids.add(profile["id"])
        require_keys(profile["viewport"], ("width", "height"), f"viewport của {profile['id']}")
        if profile["orientation"] != "portrait":
            raise RenderLabError(f"Giai đoạn 1 chỉ khóa orientation=portrait: {profile['id']}")
        expected_viewport = (528, 792) if profile["environment"] == "simulator_x3" else (480, 800)
        actual_viewport = (profile["viewport"]["width"], profile["viewport"]["height"])
        if actual_viewport != expected_viewport:
            raise RenderLabError(
                f"Viewport {profile['id']} là {actual_viewport[0]}x{actual_viewport[1]}, "
                f"phải là {expected_viewport[0]}x{expected_viewport[1]}"
            )

    required_table_metrics = {
        "tables",
        "rows",
        "grid_rows",
        "stacked_rows",
        "max_columns",
        "wrapped_cells",
        "max_cell_lines",
        "page_split_rows",
        "page_splits",
    }
    required_image_metrics = {
        "images",
        "png_images",
        "jpeg_images",
        "scaled_down",
        "out_of_bounds",
        "image_pages",
        "max_source_width",
        "max_source_height",
        "max_display_width",
        "max_display_height",
    }
    for checkpoint in manifest["checkpoints"]:
        by_profile = checkpoint.get("structural_expectations", {})
        if not isinstance(by_profile, dict):
            raise RenderLabError(f"Checkpoint {checkpoint['id']} có structural_expectations không hợp lệ")
        unknown_profiles = set(by_profile) - profile_ids
        if unknown_profiles:
            raise RenderLabError(
                f"Checkpoint {checkpoint['id']} có structural profile không tồn tại: {sorted(unknown_profiles)}"
            )
        for profile_id, sections in by_profile.items():
            if not isinstance(sections, dict):
                raise RenderLabError(f"Structural expectation {checkpoint['id']}/{profile_id} phải là object")
            if "table_layout" in sections:
                table_metrics = sections["table_layout"]
                if not isinstance(table_metrics, dict) or set(table_metrics) != required_table_metrics:
                    raise RenderLabError(
                        f"Structural table_layout {checkpoint['id']}/{profile_id} phải có đủ metric chuẩn"
                    )
                if any(
                    not isinstance(value, int) or isinstance(value, bool) or value < 0
                    for value in table_metrics.values()
                ):
                    raise RenderLabError(
                        f"Structural table_layout {checkpoint['id']}/{profile_id} chỉ nhận số nguyên >= 0"
                    )
            if "image_layout" in sections:
                image_metrics = sections["image_layout"]
                if not isinstance(image_metrics, dict) or set(image_metrics) != required_image_metrics:
                    raise RenderLabError(
                        f"Structural image_layout {checkpoint['id']}/{profile_id} phải có đủ metric chuẩn"
                    )
                if any(
                    not isinstance(value, int) or isinstance(value, bool) or value < 0
                    for value in image_metrics.values()
                ):
                    raise RenderLabError(
                        f"Structural image_layout {checkpoint['id']}/{profile_id} chỉ nhận số nguyên >= 0"
                    )

    for suite_name, suite in manifest["suites"].items():
        require_keys(suite, ("profiles", "checkpoints", "cache_states"), f"suite {suite_name}")
        unknown_profiles = set(suite["profiles"]) - profile_ids
        if unknown_profiles:
            raise RenderLabError(f"Suite {suite_name} tham chiếu profile không tồn tại: {sorted(unknown_profiles)}")
        selected_checkpoints = checkpoint_ids if suite["checkpoints"] == "all" else set(suite["checkpoints"])
        unknown_checkpoints = selected_checkpoints - checkpoint_ids
        if unknown_checkpoints:
            raise RenderLabError(
                f"Suite {suite_name} tham chiếu checkpoint không tồn tại: {sorted(unknown_checkpoints)}"
            )
        invalid_cache_states = set(suite["cache_states"]) - {"cold", "warm"}
        if invalid_cache_states:
            raise RenderLabError(f"Suite {suite_name} có cache state không hợp lệ: {sorted(invalid_cache_states)}")
        warm_only = set(suite.get("warm_cache_checkpoints", [])) - checkpoint_ids
        if warm_only:
            raise RenderLabError(f"Suite {suite_name} có warm checkpoint không tồn tại: {sorted(warm_only)}")

    if not EPUB_PATH.is_file():
        raise RenderLabError(f"Thiếu fixture EPUB: {EPUB_PATH}")
    return manifest


def expand_suite(manifest: dict[str, Any], suite_name: str) -> list[RenderCase]:
    if suite_name not in manifest["suites"]:
        raise RenderLabError(f"Suite không tồn tại: {suite_name}")
    suite = manifest["suites"][suite_name]
    profiles = {profile["id"]: profile for profile in manifest["render_profiles"]}
    checkpoints = {checkpoint["id"]: checkpoint for checkpoint in manifest["checkpoints"]}
    checkpoint_ids = list(checkpoints) if suite["checkpoints"] == "all" else suite["checkpoints"]
    warm_checkpoints = set(suite.get("warm_cache_checkpoints", []))

    cases: list[RenderCase] = []
    for profile_id in suite["profiles"]:
        for checkpoint_id in checkpoint_ids:
            states = list(suite["cache_states"])
            if checkpoint_id in warm_checkpoints and "warm" not in states:
                states.append("warm")
            for cache_state in states:
                cases.append(RenderCase(profiles[profile_id], checkpoints[checkpoint_id], cache_state))
    return cases


def pio_command() -> str:
    local = ROOT / ".venv" / "bin" / "pio"
    if local.is_file():
        return str(local)
    resolved = shutil.which("pio")
    if resolved:
        return resolved
    raise RenderLabError("Không tìm thấy PlatformIO CLI")


def build_environments(cases: list[RenderCase]) -> None:
    environments = sorted({case.profile["environment"] for case in cases})
    for environment in environments:
        print(f"[render-lab] Build {environment}")
        subprocess.run([pio_command(), "run", "-e", environment], cwd=ROOT, check=True)


def normalize_result(result: dict[str, Any]) -> dict[str, Any]:
    return {key: value for key, value in result.items() if key not in NONDETERMINISTIC_RESULT_FIELDS}


def prepare_sd_root(root: Path) -> None:
    books = root / "books"
    books.mkdir(parents=True, exist_ok=True)
    shutil.copy2(EPUB_PATH, books / "render-reference.epub")


def render_process(case: RenderCase, sd_root: Path, output_dir: Path, cache_state: str) -> dict[str, Any]:
    profile = case.profile
    checkpoint = case.checkpoint
    simulator_output = sd_root / "render-lab"
    shutil.rmtree(simulator_output, ignore_errors=True)

    environment = profile["environment"]
    program = ROOT / ".pio" / "build" / environment / "program"
    if not program.is_file():
        raise RenderLabError(f"Thiếu simulator binary: {program}")

    child_env = os.environ.copy()
    child_env.update(
        {
            "CROSSPOINT_SIM_SD": str(sd_root),
            "CROSSPOINT_RENDER_LAB": "1",
            "CROSSPOINT_RENDER_LAB_BOOK": "/books/render-reference.epub",
            "CROSSPOINT_RENDER_LAB_PROFILE": profile["id"],
            "CROSSPOINT_RENDER_LAB_CHECKPOINT": checkpoint["id"],
            "CROSSPOINT_RENDER_LAB_HREF": checkpoint["href"],
            "CROSSPOINT_RENDER_LAB_PAGE_OFFSET": str(checkpoint.get("page_offset", 0)),
            "CROSSPOINT_RENDER_LAB_FULL_BUILD": "1" if checkpoint.get("full_build", False) else "0",
            "CROSSPOINT_RENDER_LAB_TABLE_METRICS": "1" if checkpoint.get("structural_expectations") else "0",
            "CROSSPOINT_RENDER_LAB_IMAGE_METRICS": "1" if checkpoint.get("image_metrics", False) else "0",
            "CROSSPOINT_RENDER_LAB_CACHE_STATE": cache_state,
            "CROSSPOINT_RENDER_LAB_EPUB_SHA256": sha256_file(EPUB_PATH),
            "CROSSPOINT_RENDER_LAB_TEXT_AA": "1" if profile["text_antialiasing"] else "0",
            "CROSSPOINT_RENDER_LAB_EMBEDDED_STYLES": "1" if profile["embedded_styles"] else "0",
            "CROSSPOINT_RENDER_LAB_FONT_SIZE": str(profile["font_point_size"]),
            "CROSSPOINT_RENDER_LAB_SCREEN_MARGIN": str(profile["screen_margin"]),
            "SDL_VIDEODRIVER": child_env.get("SDL_VIDEODRIVER", "dummy"),
            "SDL_RENDER_DRIVER": child_env.get("SDL_RENDER_DRIVER", "software"),
        }
    )
    completed = subprocess.run(
        [str(program)],
        cwd=ROOT,
        env=child_env,
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )

    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "simulator.log").write_text(completed.stdout + completed.stderr, encoding="utf-8")
    result_path = simulator_output / "result.json"
    if not result_path.is_file():
        raise RenderLabError(f"{case.case_id}: simulator không tạo result.json (exit={completed.returncode})")
    result = json.loads(result_path.read_text(encoding="utf-8"))
    if completed.returncode != 0 or result.get("status") != "ok":
        raise RenderLabError(f"{case.case_id}: simulator thất bại: {result.get('message', 'không có thông báo')}")

    for name in ("framebuffer.pbm", "framebuffer.pgm", "result.json"):
        source = simulator_output / name
        if not source.is_file():
            raise RenderLabError(f"{case.case_id}: thiếu artifact {name}")
        shutil.copy2(source, output_dir / name)

    expected_viewport = profile["viewport"]
    if (result["logical_width"], result["logical_height"]) != (
        expected_viewport["width"],
        expected_viewport["height"],
    ):
        raise RenderLabError(
            f"{case.case_id}: viewport thực tế {result['logical_width']}x{result['logical_height']} "
            f"khác manifest {expected_viewport['width']}x{expected_viewport['height']}"
        )
    return result


def assert_structural_expectations(case: RenderCase, result: dict[str, Any]) -> None:
    by_profile = case.checkpoint.get("structural_expectations", {})
    expected = by_profile.get(case.profile["id"])
    if expected is None:
        return
    for section, expected_values in expected.items():
        actual_values = result.get(section)
        if not isinstance(actual_values, dict):
            raise RenderLabError(f"{case.case_id}: thiếu structural result {section}")
        for key, expected_value in expected_values.items():
            actual_value = actual_values.get(key)
            if actual_value != expected_value:
                raise RenderLabError(
                    f"{case.case_id}: {section}.{key}={actual_value!r}, phải là {expected_value!r}"
                )


def run_once(case: RenderCase, run_dir: Path) -> tuple[dict[str, Any], Path]:
    with tempfile.TemporaryDirectory(prefix="crosspoint-render-lab-") as temp_name:
        sd_root = Path(temp_name)
        prepare_sd_root(sd_root)
        if case.cache_state == "warm":
            prime_dir = run_dir / "warm-prime"
            render_process(case, sd_root, prime_dir, "cold")
        result = render_process(case, sd_root, run_dir, case.cache_state)
    return result, run_dir


def create_diff_artifacts(expected_pgm: Path, actual_pgm: Path, output_dir: Path) -> None:
    try:
        from PIL import Image, ImageChops
    except ImportError:
        return
    expected = Image.open(expected_pgm).convert("L")
    actual = Image.open(actual_pgm).convert("L")
    if expected.size != actual.size:
        return
    expected.save(output_dir / "expected.png")
    actual.save(output_dir / "actual.png")
    ImageChops.difference(expected, actual).save(output_dir / "diff.png")


def compare_or_accept(case: RenderCase, actual_dir: Path, result: dict[str, Any], accept: bool) -> None:
    golden_dir = GOLDEN_ROOT / case.profile["id"] / case.checkpoint["id"] / case.cache_state
    if accept:
        golden_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(actual_dir / "framebuffer.pbm", golden_dir / "framebuffer.pbm")
        shutil.copy2(actual_dir / "framebuffer.pgm", golden_dir / "framebuffer.pgm")
        (golden_dir / "result.json").write_text(
            json.dumps(normalize_result(result), ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        print(f"[render-lab] Accepted {case.case_id}")
        return

    required = ("framebuffer.pbm", "framebuffer.pgm", "result.json")
    missing = [name for name in required if not (golden_dir / name).is_file()]
    if missing:
        raise RenderLabError(
            f"{case.case_id}: thiếu golden {', '.join(missing)}; review output rồi chạy lại với --accept"
        )

    mismatches: list[str] = []
    for name in ("framebuffer.pbm", "framebuffer.pgm"):
        if (golden_dir / name).read_bytes() != (actual_dir / name).read_bytes():
            mismatches.append(name)
    expected_result = json.loads((golden_dir / "result.json").read_text(encoding="utf-8"))
    if expected_result != normalize_result(result):
        mismatches.append("result.json")
    if mismatches:
        create_diff_artifacts(golden_dir / "framebuffer.pgm", actual_dir / "framebuffer.pgm", actual_dir)
        raise RenderLabError(f"{case.case_id}: render regression ở {', '.join(mismatches)}")


def verify_suite(manifest: dict[str, Any], suite_name: str, accept: bool, runs: int, no_build: bool) -> None:
    cases = expand_suite(manifest, suite_name)
    if not no_build:
        build_environments(cases)
    ACTUAL_ROOT.mkdir(parents=True, exist_ok=True)

    failures: list[str] = []
    for case in cases:
        print(f"[render-lab] Verify {case.case_id}")
        case_root = ACTUAL_ROOT / case.profile["id"] / case.checkpoint["id"] / case.cache_state
        shutil.rmtree(case_root, ignore_errors=True)
        baseline_images: tuple[bytes, bytes] | None = None
        baseline_result: dict[str, Any] | None = None
        first_result: dict[str, Any] | None = None
        first_dir: Path | None = None
        try:
            for run_index in range(runs):
                run_dir = case_root / f"run-{run_index + 1}"
                result, actual_dir = run_once(case, run_dir)
                assert_structural_expectations(case, result)
                images = (
                    (actual_dir / "framebuffer.pbm").read_bytes(),
                    (actual_dir / "framebuffer.pgm").read_bytes(),
                )
                normalized = normalize_result(result)
                if baseline_images is None:
                    baseline_images = images
                    baseline_result = normalized
                    first_result = result
                    first_dir = actual_dir
                elif images != baseline_images or normalized != baseline_result:
                    raise RenderLabError(f"{case.case_id}: output không tất định giữa 2 lần chạy")
            assert first_result is not None and first_dir is not None
            compare_or_accept(case, first_dir, first_result, accept)
        except (OSError, subprocess.SubprocessError, RenderLabError, json.JSONDecodeError) as exc:
            failures.append(str(exc))
            print(f"[render-lab] FAIL: {exc}", file=sys.stderr)

    if failures:
        raise RenderLabError(f"{len(failures)}/{len(cases)} render case thất bại")
    print(f"[render-lab] OK: {len(cases)} case, {runs} lần chạy/case")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("validate", help="Kiểm tra fixture, schema và simulator pin")

    verify = subparsers.add_parser("verify", help="Build simulator và so sánh golden")
    verify.add_argument("--suite", choices=("smoke", "full"), default="smoke")
    verify.add_argument("--accept", action="store_true", help="Ghi output đã review thành golden mới")
    verify.add_argument("--runs", type=int, default=2, help="Số lần chạy độc lập để kiểm tra tính tất định")
    verify.add_argument("--no-build", action="store_true", help="Dùng simulator binary đã build")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        manifest = load_and_validate_manifest()
        fixture_hash = sha256_file(EPUB_PATH)
        if args.command == "validate":
            print(
                f"OK: {len(manifest['checkpoints'])} checkpoint, "
                f"{len(manifest['render_profiles'])} profile, EPUB SHA-256={fixture_hash}"
            )
            return 0
        if args.runs < 1:
            raise RenderLabError("--runs phải >= 1")
        verify_suite(manifest, args.suite, args.accept, args.runs, args.no_build)
        return 0
    except (OSError, subprocess.SubprocessError, RenderLabError, json.JSONDecodeError) as exc:
        print(f"render-lab: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
