# Kế hoạch nâng chất lượng render cho `vns-next`

Ngày lập: 28/08/2026

Baseline mã nguồn: `vns-next` tại `fdd8c7a2`

Nguồn CrossInk đối chiếu: `uxjulia/CrossInk` tại `cab4f249`

## Kết luận

Không nên port trực tiếp toàn bộ renderer của CrossInk. Hướng khuyến nghị là:

1. Đồng bộ `upstream/develop` vào một nhánh tích hợp xuất phát từ `vns-next`.
2. Dựng môi trường render tái lập được và bộ fixture trước khi đổi output.
3. Xác nhận các cải tiến upstream trên X3/X4/Sticky.
4. Chỉ backport từng cơ chế CrossInk còn thiếu và chứng minh bằng ảnh, số đo, test.

Lý do chính: tại thời điểm lập kế hoạch, `vns-next` có 14 commit riêng nhưng thiếu 81 commit từ `upstream/develop`. Phần thiếu đã chứa nhiều thay đổi render mới hơn CrossInk hoặc đã hợp nhất ý tưởng tương tự. Port CrossInk trước sẽ tạo xung đột lớn trong `Epub`, `GfxRenderer`, font cache và binary cache, đồng thời làm khó việc xác định cải thiện đến từ đâu.

## Hiện trạng có căn cứ

### Điểm mạnh cần bảo toàn

- `EpubReaderActivity::renderContents()` đã tách grayscale của text và image; image vẫn được render grayscale khi text anti-aliasing tắt.
- Pipeline hiện tại hỗ trợ tiled grayscale, ghi plane theo strip, tier theo heap và overlap render với refresh bất đồng bộ khi phần cứng cho phép.
- Image decode có đường ghi trực tiếp vào framebuffer/strip; không nên quay lại mô hình giữ full-image buffer.
- Log hiện tại đã tách `prewarm`, `bw_render`, grayscale render/write/display, cleanup và tổng thời gian. Đây là nền tốt cho benchmark.
- Cache section đã khóa theo `ReaderRenderSpec`, viewport và các tùy chọn layout chính.

Vị trí nguồn: `src/activities/reader/EpubReaderActivity.cpp`, `lib/GfxRenderer/GfxRenderer.*`, `lib/Epub/Epub/converters/`, `lib/Epub/Epub/ReaderRenderSpec.h`.

### Khoảng trống hiện tại

- Repo chưa có env simulator và chưa có kiểm thử end-to-end tạo framebuffer/golden image tự động.
- Các EPUB trong `test/epubs/` chủ yếu là fixture thủ công; host test chưa xác nhận pagination, CSS cascade, image placement và output pixel của cả trang.
- CSS parser trên `vns-next` chỉ hỗ trợ selector element, class, `element.class` và grouped selector; chưa hỗ trợ descendant selector.
- Nhánh hiện tại chưa có table layout. Không nên lấy bản table cũ của CrossInk vì `upstream/develop` đã có triển khai mới hơn tại commit `da3d50c2`.
- CrossInk có render mode theo sách và fallback khi thiếu RAM; `vns-next` chưa có cơ chế tương đương.
- Flash của env `default` đang dùng 5.804.113 B/6.553.600 B theo build cục bộ ngày 28/08/2026. Vì vậy, thêm nhiều built-in font CrossInk trước khi lấy tối ưu font upstream là rủi ro rõ ràng.

### Baseline cục bộ đã đo

| Kiểm tra | Kết quả |
| --- | --- |
| Host tests | 141/141 test qua |
| `default` ESP32-C3 | Build qua; RAM 51.308 B/327.680 B; flash 5.804.113 B/6.553.600 B |
| `sticky` ESP32-S3 | Build qua; RAM 60.972 B/327.680 B; flash 5.601.003 B/6.553.600 B |
| Format toàn repo | Chưa sạch: `lib/MiniBidi/minibidi.c` lệch clang-format |
| Static analysis | Chưa sạch: 1 cảnh báo `low` tại `src/features/vannhanso/VanNhanSoCache.cpp:23` |

Hai lỗi nền cuối bảng phải được xử lý thành commit riêng; không trộn chúng vào thay đổi render.

## So sánh chiến lược

| Phương án | Cơ chế | Lợi ích | Rủi ro/đánh đổi | Kết luận |
| --- | --- | --- | --- | --- |
| Port CrossInk diện rộng | Chép parser, renderer, font và setting từ fork | Nhìn thấy nhiều tính năng nhanh | Diff rất lớn, đè cải tiến mới của upstream, tăng flash, khó bisect lỗi | Không chọn |
| Upstream trước, CrossInk chọn lọc | Hợp nhất 81 commit upstream rồi backport từng cơ chế còn thiếu | Ít mã trùng, giữ được sửa lỗi mới, dễ đo từng bước | Cần giải quyết conflict tích hợp trước | Khuyến nghị |
| Viết lại renderer | Tạo engine layout/render mới | Kiến trúc có thể sạch hơn | Thời gian dài, rủi ro pagination/cache/heap cao, thiếu oracle chất lượng | Chỉ cân nhắc dài hạn |

## Kế hoạch triển khai

### Giai đoạn 0 — Làm sạch baseline tích hợp

Mục tiêu: đưa `vns-next` lên nền upstream hiện hành mà không làm mất tính năng Vạn Nhân Số.

1. Tạo nhánh tích hợp từ `vns-next`; merge `upstream/develop`, không rebase lịch sử đã publish.
2. Chia conflict theo vùng: agent setup, settings/state, `main`, reader, cache, network và Vạn Nhân Số.
3. Giữ test Vạn Nhân Số hiện có; thêm test tại điểm conflict nếu hành vi chưa được khóa.
4. Xử lý riêng 2 lỗi baseline format/static analysis đã nêu.
5. Chạy host tests, `default`, `sticky`, format và cppcheck.
6. Ghi lại RAM, flash và timing sau merge; không dùng số liệu trước merge làm kết quả cuối.

Các thay đổi upstream cần xác nhận đặc biệt:

- `d3b3b566`: image decode overflow và viewport clipping.
- `da3d50c2`: EPUB table layout theo cột.
- `b6746c1d`: CSS cache retry khi thiếu RAM và host tests cho CSS parser.
- `8141d7b9`: style-aware font prewarm và benchmark fixture.
- `c484dc72`: định dạng font thưa/nén tốt hơn; commit upstream tuyên bố giảm 323 KB flash, nhưng phải build lại trên `vns-next` để xác nhận con số thực.

Điều kiện hoàn thành: CI-equivalent pass; tính năng Vạn Nhân Số không regress; cả `default` và `sticky` tạo được firmware.

### Giai đoạn 1 — Dựng render lab và golden corpus

Mục tiêu: mọi tuyên bố “render đẹp hơn” phải có output so sánh được.

1. Port tối thiểu env simulator/HAL từ CrossInk hoặc `crossink-simulator`; không port UI/tính năng CrossInk không liên quan.
2. Thêm chế độ chạy không tương tác: mở fixture, chọn viewport/font/settings, render trang chỉ định và xuất framebuffer ra PNG/PBM cùng manifest JSON.
3. Tạo corpus ban đầu gồm các nhóm sau:
   - Latin và tiếng Việt có dấu, NFC/NFD và combining marks.
   - Bold/italic/kerning/ligature, superscript/subscript và fallback glyph.
   - Căn lề, indent âm/dương, margin/padding, page break và descendant CSS.
   - Table đơn giản, header/footer, nội dung dài và cell wrap.
   - JPEG/PNG: photo, line art, transparency, crop, scale down/up và image xen text.
   - RTL/Bidi và trang chuyển từ image sang text để kiểm tra ghosting trên thiết bị.
4. Golden test kiểm tra pixel/layout ở simulator. Thay đổi golden chỉ được chấp nhận khi diff được review như artifact.
5. Thêm script thu log `prewarm`, `bw_render`, `total`, free heap và largest block theo fixture.

Simulator chỉ xác nhận layout/framebuffer. Ghosting, độ đậm nét và waveform vẫn phải A/B trên panel thật.

Điều kiện hoàn thành: một lệnh tạo lại toàn bộ output; output ổn định qua 2 lần chạy; CI giữ artifact khi diff thất bại.

### Giai đoạn 2 — Nhận các cải tiến upstream và khóa regression

Mục tiêu: tận dụng phần đã được upstream giải quyết trước khi viết mã mới.

1. Tạo golden cho table layout upstream và bổ sung structural assertion cho số hàng/cột, wrap và page split.
2. Khóa test cho image clipping/overflow, transparent PNG và image-only grayscale.
3. Benchmark font prewarm với trang nhiều style; so sánh cache miss/cache hit.
4. Kiểm tra font format mới trên built-in font và SD font, gồm fallback, kerning, combining marks và dữ liệu cache cũ.
5. Kiểm tra pipeline tiled grayscale của `vns-next` còn nguyên sau merge, nhất là trang image → text.

Điều kiện hoàn thành: không có regression golden ngoài thay đổi đã duyệt; không tăng peak heap hoặc thời gian trang một cách không giải thích được.

### Giai đoạn 3 — Backport CrossInk chọn lọc

Thứ tự ưu tiên sau đây dựa trên lợi ích render và độ rủi ro.

#### 3.1. Descendant CSS và page-break — ưu tiên 1

Cơ chế tham khảo: CrossInk commit `c5a615b`; CSS mới nhất tại `lib/Epub/Epub/css/` của CrossInk.

- Port matcher selector 2 phần và ancestor stack, không chép nguyên cache implementation nếu upstream đã có cấu trúc mới hơn.
- Giới hạn số descendant rule và có fallback khi heap thấp.
- Bổ sung `page-break-before/after` nếu corpus thực tế chứng minh cần.
- Bump CSS/section cache version và test cache cũ bị invalid đúng cách.

- Lợi ích dự kiến: EPUB giữ đúng style theo ngữ cảnh, chapter opener và paragraph class tốt hơn.
- Rủi ro: tăng chi phí lookup và RAM trong parse/layout.
- Cách bác bỏ: bỏ thay đổi nếu corpus không cải thiện đáng kể hoặc p95 indexing/tổng heap xấu đi không thể khống chế.

#### 3.2. Typography profile — ưu tiên 2

Cơ chế tham khảo: CrossInk dùng typeface có stroke chắc hơn; commit `cff54d7` đổi Bitter mặc định từ weight 400 sang 500 cho e-ink có AA.

- Không đưa toàn bộ bộ font CrossInk vào firmware.
- Thử nghiệm 2 hướng riêng: điều chỉnh weight ở pipeline tạo font; cung cấp Bitter Medium dưới dạng SD font/preset tùy chọn.
- So sánh cùng point size, line height và AA; chụp panel thật ở chữ nhỏ và tiếng Việt.
- Chỉ đổi default nếu cải thiện trên X3 và X4 mà không làm chữ bết, mất counter hoặc tăng flash quá mức.

- Lợi ích dự kiến: nét chữ đều và ít washed-out khi AA bật.
- Rủi ro: chữ đậm quá, pagination thay đổi, thiếu glyph/fallback hoặc tăng flash.
- Cách bác bỏ: đánh giá mù A/B trên bộ trang cố định và kiểm tra page count/visible-text offsets.

#### 3.3. Render mode và OOM fallback — ưu tiên 3

Cơ chế tham khảo: CrossInk `CrossInk Default → Balanced → Light → Safe Mode`, cùng các commit `e00b6f4` và `6e2f6dd`.

- Trước hết thêm budget/telemetry cho heap và max allocation theo section build.
- Tách feature flags của layout: complex selector, table, publisher spacing/image sizing, embedded style.
- Khi OOM hoặc guard chạm ngưỡng, retry theo cấp nhẹ hơn và lưu mode theo sách.
- Signature/cache phải chứa render mode; progress/bookmark không được mất khi đổi mode.

- Lợi ích dự kiến: sách khó vẫn đọc được thay vì crash.
- Rủi ro: fallback che lỗi memory leak hoặc làm output thay đổi âm thầm.
- Cách bác bỏ: fault-injection với budget heap thấp; UI/log phải cho biết mode đang dùng.

#### 3.4. CSS visual extras — ưu tiên 4, chỉ làm theo corpus

Các ứng viên gồm simple black background/redaction (`e11e792`) và small caps. Chỉ triển khai khi có fixture/sách thực tế cần; không đưa vào critical path trước descendant CSS, typography và OOM fallback.

### Giai đoạn 4 — Tối ưu image tone và panel waveform

Mục tiêu: xử lý chất lượng ảnh sau khi layout đã ổn định.

1. Tách pipeline scale, chuyển luminance, quantize/dither và waveform để benchmark độc lập.
2. So sánh Atkinson hiện tại với ít nhất một baseline không error diffusion trên photo và line art; dither phải chạy sau khi scale về kích thước đích.
3. Kiểm tra polarity, alpha/background và vùng clipping bằng golden.
4. Trên thiết bị, chụp chuỗi trang: text → image → text; đo thời gian và đánh giá ghosting sau refresh.
5. Không nhận thuật toán mới nếu chỉ đẹp ở screenshot nhưng tăng ghosting hoặc làm page turn chậm rõ rệt trên panel.

## Chỉ số và gate đề xuất

Các ngưỡng dưới đây là gate ban đầu của kế hoạch, không phải dữ liệu đã đo. Sau Giai đoạn 1 phải hiệu chỉnh bằng corpus và thiết bị thật.

| Nhóm | Chỉ số |
| --- | --- |
| Correctness | Host/golden pass; visible-text offset và bookmark mapping ổn định; cache cũ invalid đúng |
| Visual | Pixel diff có artifact; A/B panel cho nét chữ, gradient, clipping và ghosting |
| Tốc độ | `prewarm`, `bw_render`, grayscale và total theo từng fixture; báo median và p95 |
| Bộ nhớ | Free heap thấp nhất, largest block thấp nhất, peak allocation; không OOM trên corpus |
| Flash | Mỗi PR không tăng quá 64 KiB nếu chưa có lý do và số đo lợi ích |
| Static RAM | Mỗi PR không tăng quá 4 KiB nếu chưa có phân tích runtime heap tương ứng |

## Cách chia thay đổi

Mỗi nhóm sau nên là một PR/commit series độc lập để bisect được:

1. Upstream integration và baseline cleanup.
2. Simulator + headless screenshot + corpus/golden.
3. Upstream render regression tests.
4. Descendant CSS/page-break.
5. Typography experiment và quyết định default.
6. Render mode/OOM fallback.
7. Image tone/waveform tuning.

Không gộp thay đổi font, CSS parser, image dither và display waveform vào cùng một PR.

## Nguồn đối chiếu

- CrossInk repository: <https://github.com/uxjulia/CrossInk>
- CrossInk render modes: <https://github.com/uxjulia/CrossInk/blob/main/docs/epub-render-modes.md>
- CrossInk simulator hướng dẫn: <https://github.com/uxjulia/CrossInk/blob/main/docs/simulator.md>
- Upstream CrossPoint: <https://github.com/crosspoint-reader/crosspoint-reader>
- Hiện trạng pipeline trong repo: `src/activities/reader/EpubReaderActivity.cpp`, `lib/Epub/`, `lib/GfxRenderer/`, `lib/EpdFont/`.
