#!/usr/bin/env python3
"""Verify mobile navigation and font-picker contracts in embedded web pages."""

from __future__ import annotations

from html.parser import HTMLParser
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HTML_ROOT = ROOT / "src" / "network" / "html"
PAGES = ("HomePage.html", "FilesPage.html", "SettingsPage.html", "FontsPage.html")


class FontPickerParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.attributes: dict[str, str | None] | None = None

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attributes = dict(attrs)
        if tag == "input" and attributes.get("id") == "fontFiles":
            self.attributes = attributes


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> None:
    for name in PAGES:
        html = (HTML_ROOT / name).read_text(encoding="utf-8")
        require("box-sizing: border-box" in html, f"{name}: missing border-box sizing")
        require("flex-wrap: wrap" in html, f"{name}: desktop navigation cannot wrap")
        require(
            "grid-template-columns: repeat(2, minmax(0, 1fr))" in html,
            f"{name}: missing two-column mobile navigation",
        )

    fonts_html = (HTML_ROOT / "FontsPage.html").read_text(encoding="utf-8")
    parser = FontPickerParser()
    parser.feed(fonts_html)
    attributes = parser.attributes
    require(attributes is not None, "FontsPage.html: missing #fontFiles picker")
    require(attributes.get("type") == "file", "FontsPage.html: font picker is not a file input")
    require("multiple" in attributes, "FontsPage.html: font picker must allow a family-size set")
    require(".cpfont" in (attributes.get("accept") or ""), "FontsPage.html: font picker must advertise .cpfont")
    require("webkitdirectory" not in attributes, "FontsPage.html: webkitdirectory blocks Safari iOS file picking")
    require("directory" not in attributes, "FontsPage.html: directory-only picking is not mobile-compatible")

    print(f"OK: {len(PAGES)} responsive pages and Safari-compatible font picker")


if __name__ == "__main__":
    main()
