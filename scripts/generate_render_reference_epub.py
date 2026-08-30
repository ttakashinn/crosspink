#!/usr/bin/env python3
"""Generate and validate the deterministic CrossPoint render reference EPUB."""

from __future__ import annotations

import argparse
import hashlib
import html
import io
import json
import sys
import unicodedata
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "test" / "epubs" / "render-reference"
MANIFEST_PATH = SOURCE_DIR / "expected-manifest.json"
OUTPUT_PATH = ROOT / "test" / "epubs" / "crosspoint-render-reference-v1.0.epub"
FIXED_ZIP_TIME = (2026, 8, 28, 0, 0, 0)
BOOK_ID = "urn:uuid:8bbac2c8-a910-4c47-bdd7-a4f77c0279e0"
MODIFIED_AT = "2026-08-28T00:00:00Z"


CSS = """\
html { margin: 0; padding: 0; }
body {
  margin: 0.45em 0.55em;
  padding: 0;
  color: #111111;
  background: #ffffff;
  font-family: serif;
  font-size: 1em;
  line-height: 1.38;
}
h1, h2, h3 {
  font-family: sans-serif;
  font-weight: bold;
  text-align: left;
  page-break-after: avoid;
}
h1 { font-size: 1.48em; margin: 0.2em 0 0.55em 0; }
h2 { font-size: 1.22em; margin: 0.8em 0 0.34em 0; }
h3 { font-size: 1.08em; margin: 0.66em 0 0.24em 0; }
p { margin: 0 0 0.34em 0; padding: 0; }
a { color: #222222; }
code, pre, .mono { font-family: monospace; }
pre {
  white-space: pre-wrap;
  border: 1px solid #777777;
  padding: 0.35em;
  margin: 0.4em 0;
}
.instruction {
  border-left: 4px solid #444444;
  padding: 0.3em 0.45em;
  margin: 0.4em 0 0.6em 0;
  font-family: sans-serif;
  font-size: 0.9em;
}
.sample {
  border: 1px solid #888888;
  padding: 0.35em 0.45em;
  margin: 0.35em 0;
}
.serif { font-family: serif; }
.sans { font-family: sans-serif; }
.center { text-align: center; }
.right { text-align: right; }
.justify { text-align: justify; }
.indent { text-indent: 1.5em; }
.soft-flush-indent { text-indent: 1.5em; }
.nested-inset { margin-left: 2em; margin-right: 2em; padding-left: 2em; padding-right: 2em; }
.small { font-size: 0.82em; }
.large { font-size: 1.25em; }
.small-caps { font-variant-caps: small-caps; }
.small-caps-reset { font-variant-caps: normal; }
.nfd { letter-spacing: 0; }
.css-group-a, .css-group-b { font-weight: bold; }
.descendant-test .target { font-style: italic; }
.specificity-test p.target { font-weight: bold; text-align: center; }
.inherit-bold { font-weight: bold; }
.inherit-italic { font-style: italic; }
.inherit-underline { text-decoration: underline; }
.add-strike { text-decoration: line-through; }
.normal-weight { font-weight: normal; }
.normal-style { font-style: normal; }
.stylesheet-important { font-weight: bold !important; }
.specificity-test p.important-target { font-weight: normal; text-align: right; }
.important-target { font-weight: bold !important; text-align: left !important; }
.no-decoration { text-decoration: none; }
[data-check="attribute"] { text-decoration: underline; }
.first-child-test p:first-child { font-weight: bold; }
.hidden-sentinel { display: none; }
.force-page-before { page-break-before: always; }
.force-page-after { page-break-after: always; }
.keep-heading { page-break-after: avoid; }
ul, ol { margin: 0.3em 0 0.45em 1.2em; padding-left: 0.5em; }
li { margin: 0 0 0.14em 0; }
table {
  border-collapse: collapse;
  border-spacing: 0;
  width: 100%;
  margin: 0.4em 0 0.65em 0;
  font-family: sans-serif;
  font-size: 0.78em;
}
th, td {
  border: 1px solid #777777;
  padding: 0.2em 0.26em;
  text-align: left;
  vertical-align: top;
  word-wrap: break-word;
}
th { font-weight: bold; background: #dddddd; }
.figure { margin: 0.55em 0 0.75em 0; text-align: center; }
.figure img {
  display: block;
  max-width: 100%;
  height: auto;
  margin: 0 auto;
  border: 0;
}
.caption {
  margin-top: 0.2em;
  font-family: sans-serif;
  font-size: 0.78em;
  text-align: left;
}
.cover { margin: 0; padding: 0; text-align: center; }
.cover img { display: block; max-width: 100%; height: auto; margin: 0 auto; }
.rtl { text-align: right; }
.stress-a { text-align: justify; }
.stress-b { text-align: left; text-indent: 1.2em; }
.stress-c { font-style: italic; }
.checkpoint { border-top: 2px solid #333333; padding-top: 0.25em; }
"""


TONE_ROWS = (
    ("a", "a à á ả ã ạ"),
    ("ă", "ă ằ ắ ẳ ẵ ặ"),
    ("â", "â ầ ấ ẩ ẫ ậ"),
    ("e", "e è é ẻ ẽ ẹ"),
    ("ê", "ê ề ế ể ễ ệ"),
    ("i", "i ì í ỉ ĩ ị"),
    ("o", "o ò ó ỏ õ ọ"),
    ("ô", "ô ồ ố ổ ỗ ộ"),
    ("ơ", "ơ ờ ớ ở ỡ ợ"),
    ("u", "u ù ú ủ ũ ụ"),
    ("ư", "ư ừ ứ ử ữ ự"),
    ("y", "y ỳ ý ỷ ỹ ỵ"),
)


def xhtml_document(title: str, body: str, *, language: str = "vi") -> str:
    return f"""\
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="{language}" lang="{language}">
<head>
  <meta charset="utf-8"/>
  <title>{html.escape(title)}</title>
  <link rel="stylesheet" type="text/css" href="../styles/reference.css"/>
</head>
<body>
{body}
</body>
</html>
"""


def vietnamese_chapter() -> str:
    nfc_sentence = (
        "Buổi sớm, những giọt sương đọng trên cành bưởi; người thợ trẻ mở cửa, "
        "đón luồng gió mát và ghi chép kỹ từng thay đổi của chiếc máy đọc sách."
    )
    nfd_sentence = unicodedata.normalize("NFD", nfc_sentence)
    tone_table = "\n".join(
        f"<tr><th>{html.escape(base)}</th><td>{html.escape(values)}</td>"
        f"<td>{html.escape(values.upper())}</td></tr>"
        for base, values in TONE_ROWS
    )
    return xhtml_document(
        "Tiếng Việt và Unicode",
        f"""
<h1>1. Tiếng Việt và Unicode</h1>
<p class="instruction">Mục tiêu: mọi dấu phải nằm đúng trên chữ cái, không tách thành ký tự rời, không biến mất và không làm sai khoảng cách dòng.</p>

<h2 id="nfc-nfd">NFC và NFD</h2>
<div class="sample">
  <h3>NFC — ký tự dựng sẵn</h3>
  <p>{html.escape(nfc_sentence)}</p>
  <h3>NFD — chữ cái và dấu tổ hợp</h3>
  <p class="nfd">{html.escape(nfd_sentence)}</p>
</div>
<p>Hai đoạn trên phải có hình thức tương đương. Bookmark và visible-text offset phải ổn định khi chuyển trang qua đoạn NFD.</p>

<h2 id="tone-matrix">Ma trận nguyên âm và thanh điệu</h2>
<table>
  <thead><tr><th>Nhóm</th><th>Chữ thường</th><th>Chữ hoa</th></tr></thead>
  <tbody>
{tone_table}
    <tr><th>đ</th><td>đ Đ</td><td>Đ đ</td></tr>
  </tbody>
</table>

<h2 id="combining-order">Thứ tự dấu tổ hợp</h2>
<p>Chuẩn: ấ, ằ, ở, ự, Ấ, Ở.</p>
<p>Thứ tự nguồn khác nhau nhưng hình thức phải hợp lý: á̂, ở, ự.</p>

<h2 id="vietnamese-punctuation">Dấu câu và từ dài</h2>
<p>“Chất lượng hiển thị” — không chỉ là chữ rõ; còn gồm ngắt dòng, khoảng trắng, căn lề, dấu ngoặc kép và dấu gạch ngang.</p>
<p>Từ ghép thử nghiệm: nghiêng-ngả, khuỷu-tay, chuyển-hướng, Nguyễn, Huỳnh, Quỳnh, khuya, khuỵu, ngoằn-ngoèo.</p>
""",
    )


def typography_chapter() -> str:
    return xhtml_document(
        "Kiểu chữ và nhịp đọc",
        """
<h1>2. Kiểu chữ và nhịp đọc</h1>
<p class="instruction">So sánh độ đậm, độ nghiêng, kerning và baseline. Không được đổi pagination âm thầm giữa cache miss và cache hit.</p>

<h2 id="style-matrix">Ma trận kiểu chữ</h2>
<div class="sample">
  <p>Thường: Tiếng Việt cần nét chữ đều và khoảng trắng tự nhiên.</p>
  <p><strong>Đậm: Tiếng Việt cần nét chữ đều và khoảng trắng tự nhiên.</strong></p>
  <p><em>Nghiêng: Tiếng Việt cần nét chữ đều và khoảng trắng tự nhiên.</em></p>
  <p><strong><em>Đậm nghiêng: Tiếng Việt cần nét chữ đều và khoảng trắng tự nhiên.</em></strong></p>
</div>

<h2 id="families">Họ chữ và kích thước</h2>
<p class="serif">Serif: Trăng sáng trên mái ngói cũ, gió nhẹ qua hàng cây.</p>
<p class="sans">Sans-serif: Trăng sáng trên mái ngói cũ, gió nhẹ qua hàng cây.</p>
<p class="mono">Monospace: Việt_Nam = "độc lập, tự do";</p>
<p class="small">Cỡ nhỏ: Nguyễn Huệ dẫn đoàn người vượt qua con đường hẹp.</p>
<p class="large">Cỡ lớn: Chữ Việt rõ ràng.</p>

<h2 id="kerning-ligature">Kerning và ligature</h2>
<p>AVATAR WA To Ta Ty Yo — office affinity efficient difficult official.</p>
<p>Trường hợp tiếng Việt: TẠI, VÀO, QUẢ, NGƯỜI, VIỆT, THƯỞNG, QUYỀN.</p>

<h2 id="decorations">Baseline và trang trí</h2>
<p>H<sub>2</sub>O, CO<sub>2</sub>, x<sup>2</sup> + y<sup>2</sup>, ngày 28<sup>th</sup>; <u>gạch chân</u>; <s>gạch bỏ</s>.</p>

<div id="small-caps" class="force-page-before small-caps">
  <h2>Small caps tiếng Việt</h2>
  <p>Văn Nhân Số — tiếng Việt: ă â ê ô ơ ư đ; ằ ấ ệ ộ ợ ự ỳ.</p>
  <p>NFD tương đương: Văn Nhân Số — tiếng Việt, người đọc kỹ.</p>
  <p class="small-caps-reset">Dòng này đặt lại normal: chữ thường phải trở về đúng cỡ gốc.</p>
</div>
""",
    )


def css_chapter() -> str:
    soft_flush_flow = "".join(
        f"<span>nhịp đọc tiếng Việt ổn định qua lần gom thứ {index}; </span>" for index in range(1, 181)
    )
    return xhtml_document(
        "CSS cascade và selector",
        f"""
<h1>3. CSS cascade và selector</h1>
<p class="instruction">Các dòng PASS mô tả output mong đợi. Dòng FAIL dùng `display:none` tuyệt đối không được xuất hiện.</p>

<h2 id="inheritance-important">Kế thừa, kiểu mặc định và important</h2>
<div class="inherit-bold"><div><p>PASS INHERIT BOLD: đoạn lồng qua hai block vẫn phải đậm.</p></div></div>
<div class="inherit-italic"><section><p>PASS INHERIT ITALIC: đoạn lồng qua hai block vẫn phải nghiêng.</p></section></div>
<div class="inherit-underline"><p><span class="add-strike">PASS DECORATION: dòng này phải vừa gạch chân vừa gạch bỏ.</span></p></div>
<p><strong class="normal-weight">PASS B RESET: chữ này phải thường, không đậm.</strong></p>
<p><em class="normal-style">PASS I RESET: chữ này phải đứng, không nghiêng.</em></p>
<h3 class="normal-weight">PASS HEADING RESET: tiêu đề này phải có nét thường.</h3>
<div class="specificity-test"><p class="important-target">PASS IMPORTANT: phải đậm, căn trái dù selector normal cụ thể hơn.</p></div>
<p><span class="stylesheet-important" style="font-weight: normal">PASS STYLESHEET IMPORTANT: vẫn phải đậm.</span></p>
<p><strong style="font-weight: normal !important">PASS INLINE IMPORTANT RESET: phải là chữ thường.</strong></p>
<p><u class="no-decoration">PASS DECORATION RESET: không được có gạch chân riêng.</u></p>

<h2 id="selector-matrix">Ma trận selector</h2>
<p class="css-group-a">PASS GROUP A: dòng này phải đậm.</p>
<p class="css-group-b">PASS GROUP B: dòng này cũng phải đậm.</p>
<div class="descendant-test">
  <p class="target">PASS DESCENDANT: dòng này phải nghiêng do selector tổ tiên.</p>
</div>
<div class="specificity-test">
  <p class="target">PASS SPECIFICITY: dòng này phải đậm và căn giữa.</p>
</div>
<p data-check="attribute">PASS UNSUPPORTED ATTRIBUTE: dòng này phải giữ nét thường, không gạch chân.</p>
<div class="first-child-test">
  <p>PASS UNSUPPORTED FIRST CHILD: đoạn đầu phải giữ nét thường.</p>
  <p>Đoạn thứ hai phải là chữ thường.</p>
</div>
<p class="hidden-sentinel">FAIL DISPLAY NONE: NẾU THẤY DÒNG NÀY THÌ RENDER SAI.</p>

<h2 id="alignment-spacing">Căn lề và khoảng cách</h2>
<p class="center">Đoạn căn giữa — dấu tiếng Việt không được đụng nhau.</p>
<p class="right">Đoạn căn phải — mép phải phải thẳng.</p>
<p class="justify">Đoạn căn đều: Mỗi dòng cần phân phối khoảng trắng hợp lý mà không tạo khe lớn bất thường giữa các từ tiếng Việt ngắn.</p>
<p class="indent">Đoạn thụt đầu dòng: đây là nội dung kiểm tra text-indent và việc giữ indent sau khi đọc cache.</p>
<div class="nested-inset"><div class="nested-inset"><p>PASS INSET LỒNG: vùng chữ vẫn nằm trong viewport và đủ rộng để đọc tiếng Việt.</p></div></div>

<h2 id="soft-flush">Đoạn dài qua ranh giới soft flush</h2>
<p class="soft-flush-indent">{soft_flush_flow}</p>
""",
    )


def pagination_chapter() -> str:
    repeated = " ".join(
        [
            "Người đọc chuyển sang trang kế tiếp và mong nội dung không lặp, không mất, không nhảy lùi.",
            "Dấu tiếng Việt phải giữ đúng vị trí dù từ nằm sát mép dòng hoặc bị ngắt sang dòng mới.",
        ]
        * 10
    )
    return xhtml_document(
        "Phân trang và liên kết",
        f"""
<h1>4. Phân trang và liên kết</h1>
<p class="instruction">Ghi page count, visible-text offset và bookmark trước/sau. Chạy cả cache cold và cache warm.</p>

<h2 id="long-flow">Luồng văn bản dài</h2>
<p>{html.escape(repeated)}</p>

<h2 class="keep-heading">Tiêu đề không đứng một mình</h2>
<p>Ít nhất một dòng nội dung nên đi cùng tiêu đề khi còn đủ không gian hợp lý trên trang.</p>

<div id="page-breaks" class="force-page-before">
  <span id="publisher-page-101" epub:type="pagebreak marker" role="doc-pagebreak separator" aria-label="INLINE-101"></span>
  <h2>Điểm bắt đầu sau page-break-before</h2>
  <p>Dòng này phải bắt đầu ở trang mới khi thuộc tính page break được hỗ trợ.</p>
</div>
<p class="force-page-after">Đoạn này yêu cầu page-break-after. Nội dung kế tiếp không được nằm cùng trang.</p>
<h2>Điểm bắt đầu sau page-break-after</h2>
<p>Không được tạo trang trắng dư thừa giữa hai checkpoint page break.</p>

<h2 id="line-breaks">Ngắt dòng nguồn</h2>
<p>Dòng một<br/>Dòng hai sau một thẻ br<br/><br/>Dòng bốn sau hai thẻ br.</p>
<p class="mono">không​gian​số​dữ​liệu​tiếng​Việt​được​ngắt​dòng​tại​zero​width​space​mà​không​chèn​khoảng​trắng.</p>
<p class="mono">https://example.invalid/duong-dan-rat-dai/kiem-tra-ngat-dong/tiếng-việt-không-tràn-khỏi-viewport-480-pixel</p>

<h2 id="footnotes">Footnote và anchor</h2>
<p>Đây là câu có chú thích tiếng Việt.<sup><a href="#note-1">[1]</a></sup> Liên kết phải đưa tới đúng anchor.</p>
<div id="note-1" class="sample"><p><strong>[1]</strong> Nội dung chú thích phải đọc được và không phá visible-text offset. <a href="#footnotes">Quay lại.</a></p></div>
""",
    )


def tables_chapter() -> str:
    return xhtml_document(
        "Bảng và danh sách",
        """
<h1>5. Bảng và danh sách</h1>
<p class="instruction">Ô phải wrap trong viewport; thứ tự đọc không được đảo; dấu tiếng Việt không được mất khi split trang.</p>

<h2 id="wide-table">Bảng nhiều cột</h2>
<table>
  <thead><tr><th>Mục</th><th>Trạng thái</th><th>Mô tả tiếng Việt</th><th>Giá trị</th></tr></thead>
  <tbody>
    <tr><td>1</td><td>Đạt</td><td>Chữ rõ, dấu đúng, khoảng cách đều.</td><td>Ngắn</td></tr>
    <tr><td>2</td><td>Cần xem</td><td>Nội dung rất dài để buộc cell phải xuống nhiều dòng trên màn hình hẹp 480 pixel.</td><td>123.456</td></tr>
    <tr><td>3</td><td>Không đạt</td><td>Không được cắt mất cột cuối hoặc ghi đè chữ giữa các ô.</td><td>−42%</td></tr>
  </tbody>
</table>

<h2 id="nested-lists">Danh sách lồng nhau</h2>
<ol>
  <li>Tiếng Việt
    <ul>
      <li>NFC: chữ dựng sẵn.</li>
      <li>NFD: chữ và dấu tổ hợp.</li>
      <li>Fallback: ký tự ngoài font chính.</li>
    </ul>
  </li>
  <li>Hình ảnh
    <ol>
      <li>Giải mã.</li>
      <li>Thu phóng.</li>
      <li>Dither và refresh.</li>
    </ol>
  </li>
</ol>
""",
    )


def images_chapter() -> str:
    figures = (
        ("grayscale-indexed.png", "PNG indexed: phải thấy bốn mức xám và toàn bộ khung."),
        ("transparent-overlay.png", "PNG RGBA: vùng trong suốt phải dùng nền trang, không thành mảng đen."),
        ("line-art.png", "Line art: đường chéo, vòng tròn và nét mảnh phải còn rõ."),
        ("photo-pattern.jpg", "JPEG photo-like: gradient phải mượt trong giới hạn bốn mức xám."),
        ("wide-image.png", "Ảnh rộng: hai mép và dấu góc phải cùng xuất hiện."),
        ("tall-image.png", "Ảnh cao: phải scale hợp lý, không overflow hoặc cấp phát full-size nguy hiểm."),
    )
    body = "\n".join(
        f"""<div class="figure"><img src="../images/{name}" alt="{html.escape(caption)}"/><p class="caption">{html.escape(caption)}</p></div>"""
        for name, caption in figures
    )
    return xhtml_document(
        "Hình ảnh và grayscale",
        f"""
<h1>6. Hình ảnh và grayscale</h1>
<p id="image-matrix" class="instruction">Đánh giá riêng image render và text render. Chạy chuỗi text → image → text để quan sát ghosting trên panel thật.</p>
{body}
<p>Đoạn chữ sau ảnh phải trở lại căn trái và không kế thừa căn giữa từ figure.</p>
""",
    )


def bidi_chapter() -> str:
    return xhtml_document(
        "Bidi và font fallback",
        """
<h1>7. Bidi và font fallback</h1>
<p class="instruction">Đây là phạm vi phụ. Tiếng Việt vẫn là baseline chính; các dòng sau phát hiện crash, đảo thứ tự hoặc fallback glyph bị mất.</p>

<h2 id="mixed-direction">Mixed LTR/RTL</h2>
<p class="rtl" lang="vi" dir="rtl">FOCUS RTL LATIN: alpha beta gamma delta; tiếng Việt vẫn phải rõ nét.</p>
<p>Tiếng Việt trước — العربية ١٢٣ — Hebrew עברית 456 — tiếng Việt sau.</p>
<p class="rtl" lang="ar" dir="rtl">العربية: هذا سطر قصير لاختبار اتجاه الكتابة من اليمين إلى اليسار.</p>
<p class="rtl" lang="he" dir="rtl">עברית: זהו משפט קצר לבדיקת כיוון הכתיבה.</p>

<h2 id="fallback">Fallback glyph</h2>
<p>Latin: Việt Nam — Greek: Αθήνα — Cyrillic: Москва — CJK: 日本語 中文 한국어.</p>
<p>Ký hiệu: © ™ → ← ± × ÷ € ₫ … “ ” ‘ ’.</p>
""",
    )


def stress_chapter() -> str:
    source_sentences = (
        "Buổi sáng, nhóm phát triển mở bản thử nghiệm và đọc lại từng trang bằng cùng một cấu hình.",
        "Mỗi dấu tiếng Việt phải bám đúng ký tự nền, kể cả khi đoạn văn nằm sát ranh giới trang.",
        "Bộ nhớ trống, khối cấp phát lớn nhất và thời gian dựng trang được ghi lại cho cả cache lạnh lẫn cache ấm.",
        "Nếu kết quả đẹp hơn nhưng làm mất bookmark hoặc tăng nguy cơ hết bộ nhớ, thay đổi đó chưa đạt yêu cầu.",
        "Ảnh, bảng và chữ được xen kẽ để phát hiện trạng thái căn lề hoặc style bị rò từ block trước sang block sau.",
    )
    paragraphs: list[str] = []
    for index in range(1, 121):
        sentence = " ".join(source_sentences[index % len(source_sentences) :] + source_sentences[: index % len(source_sentences)])
        if index % 12 == 0:
            paragraphs.append(f'<h2 id="stress-{index}">Mốc stress {index}</h2>')
        css_class = ("stress-a", "stress-b", "stress-c")[index % 3]
        if index % 10 == 0:
            sentence = unicodedata.normalize("NFD", sentence)
        paragraphs.append(f'<p class="{css_class}"><strong>{index:03d}.</strong> {html.escape(sentence)}</p>')
    body = "\n".join(paragraphs)
    return xhtml_document(
        "Stress tiếng Việt",
        f"""
<h1 id="stress-start">8. Stress tiếng Việt</h1>
<p class="instruction">Chạy từ đầu chapter tới cuối, ghi page count, prewarm, bw_render, tổng thời gian, free heap và largest block. Lặp lại sau khi cache đã hình thành.</p>
{body}
""",
    )


def checkpoints_chapter() -> str:
    return xhtml_document(
        "Checkpoint vận hành",
        """
<h1>9. Checkpoint vận hành</h1>
<p class="instruction">Không đánh giá bằng cảm giác chung. Mỗi mục dưới đây phải có screenshot hoặc framebuffer, cấu hình và log tương ứng.</p>
<ol>
  <li><a href="01-tieng-viet.xhtml#nfc-nfd">NFC/NFD tiếng Việt</a></li>
  <li><a href="01-tieng-viet.xhtml#tone-matrix">Ma trận thanh điệu</a></li>
  <li><a href="02-kieu-chu.xhtml#style-matrix">Ma trận kiểu chữ</a></li>
  <li><a href="02-kieu-chu.xhtml#small-caps">Small caps tiếng Việt</a></li>
  <li><a href="03-css.xhtml#selector-matrix">CSS cascade</a></li>
  <li><a href="04-phan-trang.xhtml#page-breaks">Page break</a></li>
  <li><a href="05-bang-danh-sach.xhtml#wide-table">Bảng rộng</a></li>
  <li><a href="06-hinh-anh.xhtml#image-matrix">Ma trận ảnh</a></li>
  <li><a href="07-bidi-fallback.xhtml#mixed-direction">Bidi và fallback</a></li>
  <li><a href="08-stress.xhtml#stress-start">Stress cold/warm cache</a></li>
</ol>
<h2 class="checkpoint">Kết thúc fixture</h2>
<p>PASS tổng thể chỉ có ý nghĩa khi tất cả checkpoint liên quan đến thay đổi đã được kiểm tra.</p>
""",
    )


CHAPTER_SPECS = (
    ("ch01", "text/01-tieng-viet.xhtml", "1. Tiếng Việt và Unicode", vietnamese_chapter),
    ("ch02", "text/02-kieu-chu.xhtml", "2. Kiểu chữ và nhịp đọc", typography_chapter),
    ("ch03", "text/03-css.xhtml", "3. CSS cascade và selector", css_chapter),
    ("ch04", "text/04-phan-trang.xhtml", "4. Phân trang và liên kết", pagination_chapter),
    ("ch05", "text/05-bang-danh-sach.xhtml", "5. Bảng và danh sách", tables_chapter),
    ("ch06", "text/06-hinh-anh.xhtml", "6. Hình ảnh và grayscale", images_chapter),
    ("ch07", "text/07-bidi-fallback.xhtml", "7. Bidi và font fallback", bidi_chapter),
    ("ch08", "text/08-stress.xhtml", "8. Stress tiếng Việt", stress_chapter),
    ("ch09", "text/09-checkpoints.xhtml", "9. Checkpoint vận hành", checkpoints_chapter),
)


def make_cover() -> bytes:
    image = Image.new("L", (480, 800), 255)
    draw = ImageDraw.Draw(image)
    for y in range(800):
        shade = 255 - (y * 160 // 799)
        draw.line((0, y, 479, y), fill=shade)
    draw.rectangle((24, 24, 455, 775), outline=0, width=8)
    draw.rectangle((56, 100, 423, 330), fill=255, outline=0, width=5)
    for index, shade in enumerate((0, 85, 170, 255)):
        left = 72 + index * 84
        draw.rectangle((left, 380, left + 70, 520), fill=shade, outline=0, width=3)
    for y in range(590, 710, 24):
        draw.line((72, y, 408, y), fill=0, width=4)
    return encode_image(image, "PNG")


def make_indexed_grayscale() -> bytes:
    image = Image.new("P", (960, 720), 0)
    palette = []
    for index in range(256):
        value = index
        palette.extend((value, value, value))
    image.putpalette(palette)
    draw = ImageDraw.Draw(image)
    levels = (0, 85, 170, 255)
    for index, level in enumerate(levels):
        left = index * 240
        draw.rectangle((left, 0, left + 239, 719), fill=level)
        draw.rectangle((left + 18, 18, left + 221, 701), outline=255 - level, width=8)
    return encode_image(image, "PNG")


def make_transparent_overlay() -> bytes:
    image = Image.new("RGBA", (900, 600), (255, 255, 255, 0))
    draw = ImageDraw.Draw(image)
    draw.rectangle((20, 20, 879, 579), outline=(0, 0, 0, 255), width=8)
    draw.ellipse((100, 90, 500, 490), fill=(0, 0, 0, 90), outline=(0, 0, 0, 255), width=7)
    draw.rectangle((390, 140, 800, 460), fill=(128, 128, 128, 150), outline=(0, 0, 0, 255), width=7)
    draw.line((40, 560, 860, 40), fill=(0, 0, 0, 255), width=5)
    return encode_image(image, "PNG")


def make_line_art() -> bytes:
    image = Image.new("L", (1000, 700), 255)
    draw = ImageDraw.Draw(image)
    draw.rectangle((5, 5, 994, 694), outline=0, width=5)
    for x in range(40, 960, 40):
        draw.line((x, 30, 1000 - x, 670), fill=0, width=1 + (x // 40) % 3)
    for radius in range(40, 300, 35):
        draw.ellipse((500 - radius, 350 - radius, 500 + radius, 350 + radius), outline=0, width=2)
    return encode_image(image, "PNG")


def make_photo_pattern() -> bytes:
    image = Image.new("RGB", (1200, 900))
    pixels = image.load()
    for y in range(900):
        for x in range(1200):
            wave = ((x // 24) ^ (y // 24)) & 31
            red = (x * 255 // 1199 + wave * 2) % 256
            green = (y * 255 // 899 + wave * 3) % 256
            blue = ((x + y) * 255 // 2098 + wave * 4) % 256
            pixels[x, y] = (red, green, blue)
    draw = ImageDraw.Draw(image)
    draw.rectangle((8, 8, 1191, 891), outline=(0, 0, 0), width=8)
    draw.ellipse((330, 180, 870, 720), outline=(255, 255, 255), width=12)
    return encode_image(
        image,
        "JPEG",
        quality=90,
        subsampling=0,
        optimize=False,
        progressive=False,
    )


def make_wide_image() -> bytes:
    image = Image.new("L", (1600, 420), 245)
    draw = ImageDraw.Draw(image)
    draw.rectangle((3, 3, 1596, 416), outline=0, width=7)
    for x in range(0, 1600, 100):
        draw.rectangle((x, 80, min(x + 99, 1599), 340), fill=(x // 100 % 4) * 80)
    for x, y in ((20, 20), (1520, 20), (20, 340), (1520, 340)):
        draw.rectangle((x, y, x + 60, y + 60), fill=0)
    return encode_image(image, "PNG")


def make_tall_image() -> bytes:
    image = Image.new("L", (480, 1600), 255)
    draw = ImageDraw.Draw(image)
    draw.rectangle((3, 3, 476, 1596), outline=0, width=7)
    for y in range(0, 1600, 100):
        draw.rectangle((80, y, 400, min(y + 99, 1599)), fill=(y // 100 % 4) * 80)
    return encode_image(image, "PNG")


def encode_image(image: Image.Image, image_format: str, **options: object) -> bytes:
    output = io.BytesIO()
    image.save(output, format=image_format, **options)
    return output.getvalue()


def cover_xhtml() -> str:
    return xhtml_document(
        "CrossPoint — Bộ tham chiếu render tiếng Việt",
        """
<div class="cover">
  <img src="../images/cover.png" alt="Bìa hình học bốn mức xám của bộ tham chiếu render tiếng Việt"/>
  <h1>CrossPoint</h1>
  <p>Bộ tham chiếu render tiếng Việt — phiên bản 1.0</p>
</div>
""",
    )


def nav_xhtml() -> str:
    items = "\n".join(
        f'      <li><a href="{href}">{html.escape(title)}</a></li>'
        for _, href, title, _ in CHAPTER_SPECS
    )
    return f"""\
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="vi" lang="vi">
<head><meta charset="utf-8"/><title>Mục lục</title></head>
<body>
  <nav epub:type="toc" id="toc">
    <h1>Mục lục</h1>
    <ol>
{items}
    </ol>
  </nav>
  <nav epub:type="landmarks page-list" id="publisher-pages">
    <h1>Trang nhà xuất bản</h1>
    <ol>
      <li><a href="text/04-phan-trang.xhtml#publisher-page-101">101</a></li>
    </ol>
  </nav>
</body>
</html>
"""


def toc_ncx() -> str:
    nav_points = "\n".join(
        f"""    <navPoint id="nav-{index}" playOrder="{index}">
      <navLabel><text>{html.escape(title)}</text></navLabel>
      <content src="{href}"/>
    </navPoint>"""
        for index, (_, href, title, _) in enumerate(CHAPTER_SPECS, start=1)
    )
    return f"""\
<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="{BOOK_ID}"/></head>
  <docTitle><text>CrossPoint — Bộ tham chiếu render tiếng Việt</text></docTitle>
  <navMap>
{nav_points}
  </navMap>
</ncx>
"""


def content_opf() -> str:
    chapter_manifest = "\n".join(
        f'    <item id="{chapter_id}" href="{href}" media-type="application/xhtml+xml"/>'
        for chapter_id, href, _, _ in CHAPTER_SPECS
    )
    spine = "\n".join(
        f'    <itemref idref="{chapter_id}"/>' for chapter_id, _, _, _ in CHAPTER_SPECS
    )
    return f"""\
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="book-id" version="3.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="book-id">{BOOK_ID}</dc:identifier>
    <dc:title>CrossPoint — Bộ tham chiếu render tiếng Việt</dc:title>
    <dc:creator>CrossPoint Reader Project</dc:creator>
    <dc:language>vi</dc:language>
    <dc:rights>Original test content; distributed under the repository license.</dc:rights>
    <meta property="dcterms:modified">{MODIFIED_AT}</meta>
    <meta name="cover" content="cover-image"/>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="css" href="styles/reference.css" media-type="text/css"/>
    <item id="cover" href="text/cover.xhtml" media-type="application/xhtml+xml"/>
{chapter_manifest}
    <item id="cover-image" href="images/cover.png" media-type="image/png" properties="cover-image"/>
    <item id="grayscale" href="images/grayscale-indexed.png" media-type="image/png"/>
    <item id="transparent" href="images/transparent-overlay.png" media-type="image/png"/>
    <item id="line-art" href="images/line-art.png" media-type="image/png"/>
    <item id="photo" href="images/photo-pattern.jpg" media-type="image/jpeg"/>
    <item id="wide" href="images/wide-image.png" media-type="image/png"/>
    <item id="tall" href="images/tall-image.png" media-type="image/png"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="cover"/>
{spine}
  </spine>
</package>
"""


CONTAINER_XML = """\
<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""


def zip_info(name: str, compress_type: int) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, FIXED_ZIP_TIME)
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    info.compress_type = compress_type
    return info


def add_entry(archive: zipfile.ZipFile, name: str, data: str | bytes, *, stored: bool = False) -> None:
    payload = data.encode("utf-8") if isinstance(data, str) else data
    compress_type = zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED
    archive.writestr(zip_info(name, compress_type), payload, compresslevel=None if stored else 9)


def build_epub() -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        add_entry(archive, "mimetype", "application/epub+zip", stored=True)
        add_entry(archive, "META-INF/container.xml", CONTAINER_XML)
        add_entry(archive, "OEBPS/content.opf", content_opf())
        add_entry(archive, "OEBPS/nav.xhtml", nav_xhtml())
        add_entry(archive, "OEBPS/toc.ncx", toc_ncx())
        add_entry(archive, "OEBPS/styles/reference.css", CSS)
        add_entry(archive, "OEBPS/text/cover.xhtml", cover_xhtml())
        for _, href, _, chapter_factory in CHAPTER_SPECS:
            add_entry(archive, f"OEBPS/{href}", chapter_factory())
        images = (
            ("cover.png", make_cover()),
            ("grayscale-indexed.png", make_indexed_grayscale()),
            ("transparent-overlay.png", make_transparent_overlay()),
            ("line-art.png", make_line_art()),
            ("photo-pattern.jpg", make_photo_pattern()),
            ("wide-image.png", make_wide_image()),
            ("tall-image.png", make_tall_image()),
        )
        for name, data in images:
            add_entry(archive, f"OEBPS/images/{name}", data)
    return output.getvalue()


def validate_epub(data: bytes) -> None:
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries = archive.infolist()
        if not entries or entries[0].filename != "mimetype":
            raise ValueError("mimetype phải là entry đầu tiên")
        if entries[0].compress_type != zipfile.ZIP_STORED:
            raise ValueError("mimetype phải được lưu không nén")
        if archive.read("mimetype") != b"application/epub+zip":
            raise ValueError("mimetype không hợp lệ")

        required = {
            "META-INF/container.xml",
            "OEBPS/content.opf",
            "OEBPS/nav.xhtml",
            "OEBPS/toc.ncx",
            "OEBPS/text/01-tieng-viet.xhtml",
            "OEBPS/text/08-stress.xhtml",
            "OEBPS/images/photo-pattern.jpg",
            "OEBPS/images/transparent-overlay.png",
        }
        missing = required.difference(archive.namelist())
        if missing:
            raise ValueError(f"Thiếu entry bắt buộc: {sorted(missing)}")

        for entry in archive.namelist():
            if entry.endswith((".xml", ".opf", ".ncx", ".xhtml")):
                ET.fromstring(archive.read(entry))

        vietnamese = archive.read("OEBPS/text/01-tieng-viet.xhtml").decode("utf-8")
        required_text = (
            "ă ằ ắ ẳ ẵ ặ",
            "ư ừ ứ ử ữ ự",
            "đ Đ",
            unicodedata.normalize("NFD", "Buổi sớm"),
            "FAIL DISPLAY NONE",
        )
        corpus = vietnamese + archive.read("OEBPS/text/03-css.xhtml").decode("utf-8")
        for sample in required_text:
            if sample not in corpus:
                raise ValueError(f"Thiếu mẫu Unicode/CSS bắt buộc: {sample!r}")

    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    if manifest["primary_language"] != "vi" or len(manifest["checkpoints"]) < 9:
        raise ValueError("Manifest checkpoint không đầy đủ")


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Kiểm tra artifact đã commit có tái lập đúng")
    parser.add_argument("--output", type=Path, default=OUTPUT_PATH, help="Đường dẫn EPUB đầu ra")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    generated = build_epub()
    validate_epub(generated)

    if args.check:
        if not args.output.is_file():
            print(f"Thiếu artifact: {args.output}", file=sys.stderr)
            return 1
        existing = args.output.read_bytes()
        validate_epub(existing)
        if existing != generated:
            print(
                f"Artifact không tái lập: existing={sha256(existing)} generated={sha256(generated)}",
                file=sys.stderr,
            )
            return 1
        print(f"OK: {args.output} ({len(existing)} bytes, sha256={sha256(existing)})")
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(generated)
    print(f"Generated: {args.output}")
    print(f"Size: {len(generated)} bytes")
    print(f"SHA-256: {sha256(generated)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
