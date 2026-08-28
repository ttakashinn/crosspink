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

## Kiểm chứng phần cứng

Simulator/framebuffer chỉ xác nhận layout và pixel đầu ra. Chất lượng nét chữ, dither, ghosting và waveform phải được đánh giá riêng trên X3/X4/Sticky. Mỗi kết quả phải ghi firmware SHA, EPUB SHA-256, font, cỡ chữ, AA, orientation và trạng thái cache.
