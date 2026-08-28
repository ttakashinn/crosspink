# CrossPoint render reference

`crosspoint-render-reference-v1.0.epub` là fixture end-to-end chuẩn cho pipeline EPUB của CrossPoint. Nội dung do dự án tự tạo, ưu tiên tiếng Việt và không phụ thuộc vào sách thương mại hoặc nội dung có bản quyền bên ngoài.

## Phạm vi

Fixture khóa các đường chạy sau:

- tiếng Việt NFC/NFD, combining marks và toàn bộ ma trận thanh điệu;
- regular, bold, italic, bold-italic, kerning, ligature, superscript và subscript;
- CSS cascade, grouped/descendant/attribute selector và `display: none`;
- pagination, page break, heading orphan, URL dài, anchor và footnote;
- bảng, cell wrap và danh sách lồng nhau;
- JPEG, indexed PNG, RGBA transparency, line art, ảnh rộng/cao và grayscale;
- mixed LTR/RTL, Arabic, Hebrew và CJK fallback;
- chapter tiếng Việt dài cho cache, heap và timing.

Fixture không nhằm kiểm tra audio, video, JavaScript hoặc fixed-layout vì các đường chạy đó không thuộc mục tiêu render hiện tại.

## Sinh và kiểm tra

```sh
.venv/bin/python scripts/generate_render_reference_epub.py
.venv/bin/python scripts/generate_render_reference_epub.py --check
```

`--check` sinh lại EPUB trong bộ nhớ, so sánh byte với artifact đã commit và kiểm tra ZIP/XML cùng các chuỗi Unicode bắt buộc.

Checkpoint và cấu hình đo nằm trong `expected-manifest.json`. Checkpoint dùng `spine href + anchor`; không khóa số trang tuyệt đối vì pagination phụ thuộc font và thiết lập reader.

## Render lab và golden

Cài dependency host một lần:

```sh
# macOS
brew install sdl2

# Debian/Ubuntu
sudo apt install libsdl2-dev libssl-dev
```

Kiểm tra nhanh manifest và simulator pin mà không build:

```sh
.venv/bin/python scripts/render_lab.py validate
```

Build simulator và chạy smoke suite. Mỗi case chạy trong SD root riêng; 2 lần chạy độc lập phải tạo cùng PBM, PGM và manifest cấu trúc:

```sh
.venv/bin/python scripts/render_lab.py verify --suite smoke
```

Suite đầy đủ chạy 33 case trên X4, X3 và X4 không AA. Checkpoint `table-continuation` dùng `page_offset: 1` để khóa trang tiếp nối của bảng. Checkpoint table chính còn kiểm tra cấu trúc hàng/cột, grid/stacked fallback, cell wrap và page split theo từng viewport. Golden chỉ được thay đổi sau khi review ảnh diff và truyền `--accept` rõ ràng:

```sh
.venv/bin/python scripts/render_lab.py verify --suite full
.venv/bin/python scripts/render_lab.py verify --suite full --accept
```

`framebuffer.pbm` khóa base framebuffer 1-bit. `framebuffer.pgm` khóa kết quả ghép base với 2 grayscale plane theo 4 mức simulator. Các trường timing và heap host được ghi để chẩn đoán nhưng không tham gia golden vì không tất định và không đại diện cho ESP32.

## Kiểm chứng phần cứng

Simulator/framebuffer chỉ xác nhận layout và pixel đầu ra. Chất lượng nét chữ, dither, ghosting và waveform phải được đánh giá riêng trên X3/X4/Sticky. Mỗi kết quả phải ghi firmware SHA, EPUB SHA-256, font, cỡ chữ, AA, orientation và trạng thái cache.
