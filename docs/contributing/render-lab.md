# Render lab EPUB

Render lab tạo baseline pixel và layout tất định cho pipeline EPUB của CrossPoint. Entry point headless nằm trong firmware nhưng chỉ được biên dịch khi có `SIMULATOR` và `CROSSPOINT_RENDER_LAB`; device firmware không mang mã capture hoặc dependency SDL2.

## Đường chạy

`scripts/render_lab.py` tạo một SD root tạm cho từng case, chép fixture vào `/books`, cấu hình reader từ profile trong `expected-manifest.json`, rồi chạy native firmware. `EpubReaderActivity` mở sách qua `HalStorage`, resolve `spine href + anchor`, áp dụng `page_offset` nếu checkpoint cần khóa trang tiếp nối, layout section, render page bằng font/cache/image decoder thật và xuất:

- `framebuffer.pbm`: base framebuffer logic 1-bit, đã chuyển từ panel landscape sang orientation của reader;
- `framebuffer.pgm`: ảnh 4 mức xám ghép từ base, LSB và MSB plane trước khi SDL/HiDPI can thiệp;
- `result.json`: viewport, spine/page, visible-text offset, cache state, font/settings, timing và heap host được simulator báo cáo. Checkpoint có `structural_expectations` còn ghi telemetry table từ parser: hàng grid/stacked, số cột, cell wrap và page split.

Mỗi lần chạy là một process riêng để settings, global state và cache không rò sang case khác. Case warm được prime bằng một lần cold trên cùng SD root, sau đó chạy lại bằng process mới.

Ảnh JPEG/PNG được scale về kích thước đích rồi lượng tử hóa bằng Bayer 4 × 4 giữa 4 mức xám gốc. Golden ảnh khóa cả cold decode và đường `.pxc` v2; đổi quy tắc lượng tử hóa phải tăng version cache để thiết bị không dùng output cũ.

## Profiles và suite

- `x4-default`: portrait `480 × 800`, Noto Serif 14 pt, text AA bật.
- `x3-default`: portrait `528 × 792`, cùng render settings để khóa khác biệt pagination do viewport.
- `x4-no-text-aa`: portrait `480 × 800`, text AA tắt; ảnh vẫn đi qua grayscale plane.
- `x4-sd-font`: portrait `480 × 800`, dùng fixture `.cpfont` v4 `CrossPointTest` có đủ 4 style và Latin mở rộng/combining marks cho tiếng Việt.
- `x4-safe-fallback`: portrait `480 × 800`, fault-injection buộc lần dựng Standard báo thiếu bộ nhớ để kiểm tra retry Safe và namespace cache riêng.
- `x4-focus-reading`: portrait `480 × 800`, bật Focus Reading để khóa vị trí split đậm/thường cho tiếng Việt và thứ tự 2 run trong từ RTL.
- `x4-book-style`: portrait `480 × 800`, dùng lựa chọn căn đoạn “theo kiểu của sách” và tắt khoảng cách đoạn bổ sung để kiểm tra trực tiếp `text-align`/`text-indent` từ CSS thay vì bị thiết lập của người dùng ghi đè.
- `smoke`: 3 checkpoint tiếng Việt, bảng/danh sách và ảnh trên X4.
- `full`: 26 checkpoint trên 3 profile; checkpoint stress chạy cả cold và warm, tổng cộng 81 case. 3 checkpoint ma trận tiếng Việt khóa toàn bộ nguyên âm/dấu, `Đ/đ`, thứ tự combining mark và từ ghép; `css-inheritance-important-continuation` khóa trang chứa các trường hợp reset kiểu chữ và `!important`; cặp checkpoint `css-layout-spacing` khóa căn lề, thụt đầu dòng và inset lồng nhưng vẫn giữ vùng chữ đọc được; cặp checkpoint `css-soft-flush` khóa indent ở đầu đoạn và ngăn indent lặp lại sau nhiều lần gom parser; `unicode-line-breaks` khóa ngắt dòng ở U+200B và chuỗi tiếng Việt dài; `table-continuation` khóa trang ngay sau anchor bảng; `publisher-page-list` khóa nhãn trang nhà xuất bản khi marker nằm trong `span` rỗng; 5 checkpoint ảnh bổ sung khóa từng đường PNG alpha, line art, JPEG/ảnh rộng, ảnh cao và chuyển ảnh → text.
- `css`: 7 checkpoint trên profile `x4-book-style`, khóa kế thừa/`!important`, cascade, căn lề theo CSS, inset lồng và đoạn tiếng Việt đi qua nhiều lần soft flush.
- `font`: 6 checkpoint trên profile SD font; checkpoint stress chạy cả cold và warm, tổng cộng 7 case. Fixture được chép vào `/.fonts/CrossPointTest/` trong SD root tạm để đi qua đúng loader và cache font của firmware; 2 trang ma trận dấu khóa coverage tiếng Việt ở cả chữ thường và chữ hoa.
- `safe`: checkpoint stress cold/warm, tổng cộng 2 case. Log phải có lần Standard thất bại có phân loại, lần Safe thành công; warm run phải nạp được cache `.safe.bin` thay vì dựng lại.
- `focus`: checkpoint typography tiếng Việt và mixed LTR/RTL trên profile Focus Reading, tổng cộng 2 case.

## Review regression

Chạy:

```sh
.venv/bin/python scripts/render_lab.py verify --suite smoke
.venv/bin/python scripts/render_lab.py verify --suite css
.venv/bin/python scripts/render_lab.py verify --suite font
.venv/bin/python scripts/render_lab.py verify --suite safe
.venv/bin/python scripts/render_lab.py verify --suite focus
```

Khi pixel khác, output thực tế và log nằm dưới `build/render-lab/actual/`. Nếu Pillow có sẵn, harness sinh thêm `expected.png`, `actual.png` và `diff.png`. Không chấp nhận golden chỉ để làm CI xanh; trước tiên phải xác định thay đổi đến từ parser, render spec, font, decoder hay viewport.

Sau khi diff là thay đổi chủ ý:

```sh
.venv/bin/python scripts/render_lab.py verify --suite full --accept
```

Review cả ảnh và `result.json`, đặc biệt `page_count`, `page_index`, `visible_text_offset`, kích thước logic và trạng thái grayscale plane. Với table, manifest khóa riêng hành vi theo viewport: X3 giữ grid 4 cột; X4 480 px fallback sang stacked để tránh cột quá hẹp. Checkpoint CSS và pagination còn khóa selector descendant 2 phần cùng `page-break-before/after`; thay đổi page count ở 2 checkpoint này phải được review như thay đổi layout, không chỉ pixel. Checkpoint stress luôn dựng xong section trước khi chụp nên `page_count` là tổng chính xác, không phải ước lượng tạm thời của incremental parser.

## Giới hạn

Timing và heap của native process chỉ dùng để phát hiện thay đổi tương đối hoặc ép đường fallback; chúng không chứng minh hiệu năng/RAM trên ESP32-C3/S3. Simulator cũng không mô hình hóa LUT, waveform, ghosting, power sequencing hoặc độ đậm thực tế của panel. Mọi tuyên bố về các đặc tính đó phải kèm build SHA và đo/ảnh trên X3, X4 hoặc Sticky thật.
