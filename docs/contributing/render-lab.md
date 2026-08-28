# Render lab EPUB

Render lab tạo baseline pixel và layout tất định cho pipeline EPUB của CrossPoint. Entry point headless nằm trong firmware nhưng chỉ được biên dịch khi có `SIMULATOR` và `CROSSPOINT_RENDER_LAB`; device firmware không mang mã capture hoặc dependency SDL2.

## Đường chạy

`scripts/render_lab.py` tạo một SD root tạm cho từng case, chép fixture vào `/books`, cấu hình reader từ profile trong `expected-manifest.json`, rồi chạy native firmware. `EpubReaderActivity` mở sách qua `HalStorage`, resolve `spine href + anchor`, áp dụng `page_offset` nếu checkpoint cần khóa trang tiếp nối, layout section, render page bằng font/cache/image decoder thật và xuất:

- `framebuffer.pbm`: base framebuffer logic 1-bit, đã chuyển từ panel landscape sang orientation của reader;
- `framebuffer.pgm`: ảnh 4 mức xám ghép từ base, LSB và MSB plane trước khi SDL/HiDPI can thiệp;
- `result.json`: viewport, spine/page, visible-text offset, cache state, font/settings, timing và heap host được simulator báo cáo. Checkpoint có `structural_expectations` còn ghi telemetry table từ parser: hàng grid/stacked, số cột, cell wrap và page split.

Mỗi lần chạy là một process riêng để settings, global state và cache không rò sang case khác. Case warm được prime bằng một lần cold trên cùng SD root, sau đó chạy lại bằng process mới.

## Profiles và suite

- `x4-default`: portrait `480 × 800`, Noto Serif 14 pt, text AA bật.
- `x3-default`: portrait `528 × 792`, cùng render settings để khóa khác biệt pagination do viewport.
- `x4-no-text-aa`: portrait `480 × 800`, text AA tắt; ảnh vẫn đi qua grayscale plane.
- `smoke`: 3 checkpoint tiếng Việt, bảng/danh sách và ảnh trên X4.
- `full`: 15 checkpoint trên 3 profile; checkpoint stress chạy cả cold và warm, tổng cộng 48 case. `table-continuation` khóa trang ngay sau anchor bảng; 5 checkpoint ảnh bổ sung khóa từng đường PNG alpha, line art, JPEG/ảnh rộng, ảnh cao và chuyển ảnh → text.

## Review regression

Chạy:

```sh
.venv/bin/python scripts/render_lab.py verify --suite smoke
```

Khi pixel khác, output thực tế và log nằm dưới `build/render-lab/actual/`. Nếu Pillow có sẵn, harness sinh thêm `expected.png`, `actual.png` và `diff.png`. Không chấp nhận golden chỉ để làm CI xanh; trước tiên phải xác định thay đổi đến từ parser, render spec, font, decoder hay viewport.

Sau khi diff là thay đổi chủ ý:

```sh
.venv/bin/python scripts/render_lab.py verify --suite full --accept
```

Review cả ảnh và `result.json`, đặc biệt `page_count`, `page_index`, `visible_text_offset`, kích thước logic và trạng thái grayscale plane. Với table, manifest khóa riêng hành vi theo viewport: X3 giữ grid 4 cột; X4 480 px fallback sang stacked để tránh cột quá hẹp. Checkpoint CSS và pagination còn khóa selector descendant 2 phần cùng `page-break-before/after`; thay đổi page count ở 2 checkpoint này phải được review như thay đổi layout, không chỉ pixel.

## Giới hạn

Timing và heap của native process chỉ dùng để phát hiện thay đổi tương đối hoặc ép đường fallback; chúng không chứng minh hiệu năng/RAM trên ESP32-C3/S3. Simulator cũng không mô hình hóa LUT, waveform, ghosting, power sequencing hoặc độ đậm thực tế của panel. Mọi tuyên bố về các đặc tính đó phải kèm build SHA và đo/ảnh trên X3, X4 hoặc Sticky thật.
