# Kế hoạch nâng chất lượng render cho `vns-next`

Ngày lập: 28/08/2026

Baseline mã nguồn: `vns-next` tại `00ab726a`

Nguồn CrossInk đối chiếu: `uxjulia/CrossInk` tại `cab4f249`

## Kết luận

Không nên port trực tiếp toàn bộ renderer của CrossInk. Hướng khuyến nghị là:

1. Đồng bộ `upstream/develop` vào một nhánh tích hợp xuất phát từ `vns-next`.
2. Dựng môi trường render tái lập được và bộ fixture trước khi đổi output.
3. Xác nhận các cải tiến upstream trên X3/X4/Sticky.
4. Chỉ backport từng cơ chế CrossInk còn thiếu và chứng minh bằng ảnh, số đo, test.

Lý do chính: sau khi fetch ngày 28/08/2026, `vns-next` có 16 commit riêng và thiếu 87 commit từ `upstream/develop` tại `4638119b`. Phần thiếu đã chứa nhiều thay đổi render mới hơn CrossInk hoặc đã hợp nhất ý tưởng tương tự. Port CrossInk trước sẽ tạo xung đột lớn trong `Epub`, `GfxRenderer`, font cache và binary cache, đồng thời làm khó việc xác định cải thiện đến từ đâu.

## Hiện trạng có căn cứ

### Điểm mạnh cần bảo toàn

- `EpubReaderActivity::renderContents()` đã tách grayscale của text và image; image vẫn được render grayscale khi text anti-aliasing tắt.
- Pipeline hiện tại hỗ trợ tiled grayscale, ghi plane theo strip, tier theo heap và overlap render với refresh bất đồng bộ khi phần cứng cho phép.
- Image decode có đường ghi trực tiếp vào framebuffer/strip; không nên quay lại mô hình giữ full-image buffer.
- Log hiện tại đã tách `prewarm`, `bw_render`, grayscale render/write/display, cleanup và tổng thời gian. Đây là nền tốt cho benchmark.
- Cache section đã khóa theo `ReaderRenderSpec`, viewport và các tùy chọn layout chính.

Vị trí nguồn: `src/activities/reader/EpubReaderActivity.cpp`, `lib/GfxRenderer/GfxRenderer.*`, `lib/Epub/Epub/converters/`, `lib/Epub/Epub/ReaderRenderSpec.h`.

### Khoảng trống còn lại

- Render lab đã khóa framebuffer X3/X4 trên host, nhưng chưa đối chiếu với ảnh panel thật; waveform, ghosting và độ đậm vẫn chưa có oracle phần cứng.
- Golden hiện xác nhận pagination, CSS cascade, table, image placement và output pixel; benchmark heap/timing trên ESP32 và kiểm tra cache cũ vẫn chưa đầy đủ.
- CSS parser trên nhánh tích hợp đã hỗ trợ selector descendant 2 phần với bộ nhớ chặn trên; selector phức tạp hơn vẫn được bỏ qua có chủ ý.
- Table layout upstream đã được tích hợp và khóa regression theo viewport; X3 giữ grid 4 cột còn X4 480 px fallback sang stacked khi cell quá hẹp.
- CrossInk có render mode theo sách và fallback khi thiếu RAM; `vns-next` chưa có cơ chế tương đương.

### Baseline cục bộ đã đo

| Kiểm tra | Kết quả |
| --- | --- |
| Host tests | 180/180 test qua sau tích hợp |
| `default` ESP32-C3 | Build qua; RAM 57.180 B/327.680 B; flash 5.641.645 B/6.553.600 B |
| `sticky` ESP32-S3 | Build qua; RAM 66.828 B/327.680 B; flash 5.430.275 B/6.553.600 B |
| Format toàn repo | Sạch; mã vendor `lib/MiniBidi/minibidi.c` được loại khỏi phạm vi format |
| Static analysis | Sạch ở các mức `low`, `medium`, `high` |

Số đo trước merge và delta chi tiết được khóa tại `docs/contributing/upstream-integration-baseline.md`. Kiểm chứng phần cứng vẫn còn thiếu; không được diễn giải các kết quả trên thành xác nhận trên X3/X4/Sticky thật.

## So sánh chiến lược

| Phương án | Cơ chế | Lợi ích | Rủi ro/đánh đổi | Kết luận |
| --- | --- | --- | --- | --- |
| Port CrossInk diện rộng | Chép parser, renderer, font và setting từ fork | Nhìn thấy nhiều tính năng nhanh | Diff rất lớn, đè cải tiến mới của upstream, tăng flash, khó bisect lỗi | Không chọn |
| Upstream trước, CrossInk chọn lọc | Hợp nhất 87 commit upstream rồi backport từng cơ chế còn thiếu | Ít mã trùng, giữ được sửa lỗi mới, dễ đo từng bước | Cần giải quyết conflict tích hợp trước | Khuyến nghị |
| Viết lại renderer | Tạo engine layout/render mới | Kiến trúc có thể sạch hơn | Thời gian dài, rủi ro pagination/cache/heap cao, thiếu oracle chất lượng | Chỉ cân nhắc dài hạn |

## Kế hoạch triển khai

### Giai đoạn 0 — Làm sạch baseline tích hợp

Mục tiêu: đưa `vns-next` lên nền upstream hiện hành mà không làm mất tính năng Văn Nhân Số.

1. Tạo nhánh tích hợp từ `vns-next`; merge `upstream/develop`, không rebase lịch sử đã publish.
2. Chia conflict theo vùng: agent setup, settings/state, `main`, reader, cache, network và Văn Nhân Số.
3. Giữ test Văn Nhân Số hiện có; thêm test tại điểm conflict nếu hành vi chưa được khóa.
4. Xử lý riêng 2 lỗi baseline format/static analysis đã nêu.
5. Chạy host tests, `default`, `sticky`, format và cppcheck.
6. Ghi lại RAM, flash và timing sau merge; không dùng số liệu trước merge làm kết quả cuối.

Các thay đổi upstream cần xác nhận đặc biệt:

- `d3b3b566`: image decode overflow và viewport clipping.
- `da3d50c2`: EPUB table layout theo cột.
- `b6746c1d`: CSS cache retry khi thiếu RAM và host tests cho CSS parser.
- `8141d7b9`: style-aware font prewarm và benchmark fixture.
- `c484dc72`: định dạng font thưa/nén tốt hơn; commit upstream tuyên bố giảm 323 KB flash, nhưng phải build lại trên `vns-next` để xác nhận con số thực.

Điều kiện hoàn thành: CI-equivalent pass; tính năng Văn Nhân Số không regress; cả `default` và `sticky` tạo được firmware.

Trạng thái ngày 28/08/2026: baseline phần mềm và CI-equivalent đã đạt trên nhánh tích hợp. Smoke test phần cứng cho Văn Nhân Số, OTA và reader còn phải thực hiện trước khi đóng hoàn toàn Giai đoạn 0; công việc dựng render lab của Giai đoạn 1 có thể bắt đầu song song.

### Giai đoạn 1 — Dựng render lab và golden corpus

Mục tiêu: mọi tuyên bố “render đẹp hơn” phải có output so sánh được.

Fixture end-to-end chuẩn là `test/epubs/crosspoint-render-reference-v1.0.epub`. Source sinh file và checkpoint nằm tại `scripts/generate_render_reference_epub.py` và `test/epubs/render-reference/`; sách thực tế chỉ dùng làm corpus bổ sung, không thay fixture chuẩn nếu không đại diện cho nội dung tiếng Việt và đường chạy của CrossPoint.

1. Tích hợp tối thiểu simulator/HAL chính thức của CrossPoint, pin theo commit; không port UI/tính năng CrossInk không liên quan.
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

Trạng thái ngày 28/08/2026: phần mềm của Giai đoạn 1 đã được triển khai trên nhánh `codex/render-lab-phase-1`. Render lab dùng simulator commit `8323320dc0ee88207f753a0dd4d195da31520a50`, native PNG/JPEG decoder và pipeline EPUB thật. Bộ golden hiện có 30 case trên `x4-default`, `x3-default` và `x4-no-text-aa`, gồm cold cache cho 9 checkpoint và cold/warm cho checkpoint stress. Smoke suite đã cho output PBM/PGM cùng manifest cấu trúc giống nhau qua 2 lần chạy độc lập. Đối chiếu X3/X4/Sticky trên panel thật vẫn là gate phần cứng chưa thực hiện.

### Giai đoạn 2 — Nhận các cải tiến upstream và khóa regression

Mục tiêu: tận dụng phần đã được upstream giải quyết trước khi viết mã mới.

1. Tạo golden cho table layout upstream và bổ sung structural assertion cho số hàng/cột, wrap và page split.
2. Khóa test cho image clipping/overflow, transparent PNG và image-only grayscale.
3. Benchmark font prewarm với trang nhiều style; so sánh cache miss/cache hit.
4. Kiểm tra font format mới trên built-in font và SD font, gồm fallback, kerning, combining marks và dữ liệu cache cũ.
5. Kiểm tra pipeline tiled grayscale của `vns-next` còn nguyên sau merge, nhất là trang image → text.

Điều kiện hoàn thành: không có regression golden ngoài thay đổi đã duyệt; không tăng peak heap hoặc thời gian trang một cách không giải thích được.

Trạng thái ngày 28/08/2026: phần mềm Giai đoạn 2 đã hoàn thành trên nhánh tích hợp. Full suite hiện có 48 case chạy qua X4, X3 và X4 không AA; khóa table grid/stacked, clipping/alpha/JPEG/line-art, 6 image placement và trang image → text. Suite font SD bổ sung 4 case dùng `.cpfont` v4 có 4 style và Latin mở rộng/combining marks tiếng Việt; cold/warm đều đi qua loader/cache thật. Checkpoint stress full-build khóa tổng trang chính xác thay vì ước lượng incremental. Script kiểm tra font nén built-in đạt 32/32 round-trip; 5 font không nén được bỏ qua đúng thiết kế. Không có thay đổi firmware trong nhóm test này nên RAM/flash runtime không đổi; timing/heap simulator chỉ dùng chẩn đoán, chưa thay cho số đo thiết bị.

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

Trạng thái ngày 28/08/2026: đã triển khai matcher descendant 2 phần bằng mask 64 bit và ancestor stack cố định, không giữ chuỗi theo từng node. Rule descendant bị chặn ở 64; khi chạm giới hạn hoặc thiếu heap, parser giữ phần rule đã nhận và không lặp cấp phát thất bại. `page-break-before/after` cùng alias `break-before/after` đã đi qua pipeline layout. CSS cache tăng lên v11, section cache lên v42. 184/184 host tests và 33/33 render case qua 2 lần chạy; 6 golden thay đổi đã được review trực quan. Build `default` dùng RAM tĩnh 57.180 B, flash 5.646.425 B; `sticky` dùng RAM tĩnh 66.828 B, flash 5.434.939 B. So với baseline trước thay đổi, RAM tĩnh không tăng, flash tăng lần lượt 4.780 B và 4.664 B. Kiểm chứng panel thật vẫn chưa thực hiện.

#### 3.2. Typography profile — ưu tiên 2

Cơ chế tham khảo: CrossInk dùng typeface có stroke chắc hơn; commit `cff54d7` đổi Bitter mặc định từ weight 400 sang 500 cho e-ink có AA.

- Không đưa toàn bộ bộ font CrossInk vào firmware.
- Thử nghiệm 2 hướng riêng: điều chỉnh weight ở pipeline tạo font; cung cấp Bitter Medium dưới dạng SD font/preset tùy chọn.
- So sánh cùng point size, line height và AA; chụp panel thật ở chữ nhỏ và tiếng Việt.
- Chỉ đổi default nếu cải thiện trên X3 và X4 mà không làm chữ bết, mất counter hoặc tăng flash quá mức.

- Lợi ích dự kiến: nét chữ đều và ít washed-out khi AA bật.
- Rủi ro: chữ đậm quá, pagination thay đổi, thiếu glyph/fallback hoặc tăng flash.
- Cách bác bỏ: đánh giá mù A/B trên bộ trang cố định và kiểm tra page count/visible-text offsets.

Trạng thái ngày 28/08/2026: Bitter trong pipeline SD font hiện đã dùng weight 500 cho regular/italic và 700 cho bold/bold-italic; fixture SD font tiếng Việt đã khóa NFC/NFD cùng 4 style. Không đổi font firmware mặc định vì chưa có A/B mù trên X3/X4 để loại trừ chữ bết, mất counter và pagination xấu hơn.

#### 3.3. Render mode và OOM fallback — ưu tiên 3

Cơ chế tham khảo: CrossInk `CrossInk Default → Balanced → Light → Safe Mode`, cùng các commit `e00b6f4` và `6e2f6dd`.

- Trước hết thêm budget/telemetry cho heap và max allocation theo section build.
- Tách feature flags của layout: complex selector, table, publisher spacing/image sizing, embedded style.
- Khi OOM hoặc guard chạm ngưỡng, retry theo cấp nhẹ hơn và lưu mode theo sách.
- Signature/cache phải chứa render mode; progress/bookmark không được mất khi đổi mode.

- Lợi ích dự kiến: sách khó vẫn đọc được thay vì crash.
- Rủi ro: fallback che lỗi memory leak hoặc làm output thay đổi âm thầm.
- Cách bác bỏ: fault-injection với budget heap thấp; UI/log phải cho biết mode đang dùng.

Trạng thái ngày 28/08/2026: đã triển khai fallback có chủ đích `Standard → Safe` thay vì mang nguyên 4 mode CrossInk. Chỉ lỗi được phân loại là thiếu bộ nhớ mới kích hoạt retry; Safe tắt CSS nhúng và chuyển ảnh sang alt-text, giữ content offset để tái phân trang về đúng vị trí. Section cache tăng lên v43 và tách `.safe.bin`, tránh Standard xóa cache Safe. Suite fault-injection gồm 2 case cold/warm chạy 2 lần; warm log xác nhận nạp cache 115 trang thay vì dựng lại. Mode hiện chỉ giữ trong phiên đọc và được ghi rõ trong log/result; không tự hạ chất lượng vĩnh viễn cho sách.

#### 3.4. CSS visual extras — ưu tiên 4, chỉ làm theo corpus

Các ứng viên gồm simple black background/redaction (`e11e792`) và small caps. Chỉ triển khai khi có fixture/sách thực tế cần; không đưa vào critical path trước descendant CSS, typography và OOM fallback.

Trạng thái ngày 29/08/2026: đã triển khai small caps theo hướng chọn lọc, không chép nguyên ánh xạ Unicode của CrossInk. `font-variant: small-caps` và `font-variant-caps: small-caps` đi qua CSS cascade, block/inline inheritance, cache và renderer; giá trị `normal` trên phần tử con khôi phục chữ thường đúng cách. Renderer dùng glyph hoa ở tỉ lệ 75%, giữ cùng đường đo advance/kerning với đường vẽ và lấy mẫu theo footprint để tránh làm mất nét mảnh hoặc dấu tiếng Việt. Bảng ánh xạ bổ sung đầy đủ `ă`, `đ`, `ơ`, `ư` và các cặp chữ Việt dựng sẵn trong Latin Extended Additional; ký tự cần biến đổi thành nhiều codepoint như `ß` được giữ nguyên để không phá offset/layout. CSS cache tăng lên v12, section cache lên v44. Quá trình triển khai cũng sửa lỗi có sẵn khiến style chữ của block cha bị mất sau khi đóng block con.

Fixture chuẩn bổ sung checkpoint small caps riêng với tiếng Việt NFC/NFD, font built-in, font SD và chế độ không AA. 196/196 host test đạt; full render 51 case, font 5 case và Safe 2 case đều ổn định qua 2 lần chạy. Golden thay đổi ngoài checkpoint mới chỉ nằm ở số trang/chỉ số tiến độ do fixture có thêm 1 trang; vùng nội dung cũ không đổi. Kiểm chứng nét chữ trên panel thật vẫn chưa thực hiện.

### Giai đoạn 4 — Tối ưu image tone và panel waveform

Mục tiêu: xử lý chất lượng ảnh sau khi layout đã ổn định.

1. Tách pipeline scale, chuyển luminance, quantize/dither và waveform để benchmark độc lập.
2. So sánh thuật toán dither hiện tại với baseline giữ đúng 4 mức xám gốc trên photo và line art; dither phải chạy sau khi scale về kích thước đích.
3. Kiểm tra polarity, alpha/background và vùng clipping bằng golden.
4. Trên thiết bị, chụp chuỗi trang: text → image → text; đo thời gian và đánh giá ghosting sau refresh.
5. Không nhận thuật toán mới nếu chỉ đẹp ở screenshot nhưng tăng ghosting hoặc làm page turn chậm rõ rệt trên panel.

Trạng thái ngày 28/08/2026: phần mềm Giai đoạn 4 đã hoàn thành. Khảo sát mã đã sửa lại giả định ban đầu: pipeline hiện dùng Bayer 4 × 4 không trạng thái, không phải Atkinson; tọa độ dither là tọa độ đích sau scale. Thuật toán cũ cộng offset quanh ngưỡng nên đầu vào đúng mức panel 85/170 vẫn bị trộn sang mức lân cận và lệch độ sáng trung bình. Thuật toán mới neo chính xác 0/85/170/255, chỉ dùng mật độ Bayer cho phần dư giữa 2 mức kề nhau. 4 host test khóa mức xám gốc, độ sáng trung bình, tính đơn điệu và version cache; `.pxc` tăng lên format v2 để cache ảnh đã tạo trên thiết bị được xóa và giải mã lại.

12 golden ảnh thay đổi có chủ đích trên X3, X4 và X4 không text AA; JPEG, PNG alpha, gradient và ảnh cao đã được review trực quan. Line art, image → text và toàn bộ layout/text không đổi. Full 48 case, font 4 case và Safe 2 case đều qua 2 lần chạy sau khi đổi format cache. Build `default` dùng RAM tĩnh 57.180 B, flash 5.647.509 B; `sticky` dùng RAM tĩnh 66.828 B, flash 5.435.779 B. So với cuối Giai đoạn 3, RAM tĩnh không tăng, flash tăng lần lượt 1.084 B và 840 B.

Không đổi waveform trong giai đoạn phần mềm này. Reader hiện đã tách base refresh cho trang ảnh, tiled grayscale và cleanup; HAL/driver tự chọn chuỗi riêng cho X3, X4 và Paper Mono, trong đó Paper Mono ghép base với gray planes. Simulator không mô hình hóa LUT, ghosting hoặc độ đậm panel nên mọi chỉnh waveform khi chưa có ảnh/đo thiết bị sẽ là suy đoán. Gate text → image → text trên thiết bị vẫn phải được ghi rõ là chưa thực hiện khi phát hành.

## Rà soát hợp nhất với CrossInk trước phát hành

Ngày 29/08/2026, mã được đối chiếu lại với `uxjulia/CrossInk` tại commit `cab4f24922f`. Kết luận là không có một nhánh thắng toàn bộ; bản tích hợp giữ hoặc nhập từng cơ chế theo chi phí thực tế trên X3/X4.

| Hạng mục | Quyết định cho VNS | Căn cứ |
| --- | --- | --- |
| Font mặc định | Giữ Noto Serif/Sans của VNS | Bao phủ tiếng Việt trực tiếp, NFC hóa NFD trước layout, đủ 4 style và có golden cho font built-in/SD. Không thêm Bitter/Lexend vào flash chỉ để tăng số lựa chọn. |
| Độ đậm chữ AA | Chưa dùng ngưỡng `--darken-aa` của CrossInk làm mặc định | CrossInk hạ ngưỡng lượng tử để nét đậm hơn, nhưng simulator không xác nhận được bết nét, counter và ghosting trên panel. VNS vẫn cho phép tắt text AA; đổi mặc định cần A/B thiết bị. |
| CSS selector/cache | Giữ kho phẳng có giới hạn của VNS; giữ descendant selector và page break đã nhập chọn lọc | Mọi lần tăng dung lượng đều có thể thất bại an toàn, selector/style pool bị chặn; phù hợp heap X3 hơn `unordered_map` của CrossInk. CrossInk hỗ trợ tối đa 100 descendant rule và disk fallback, còn VNS chủ động chặn ở 64 để dùng mask 64 bit không cấp phát. |
| CSS visual extras | Nhập small caps có sửa ánh xạ tiếng Việt; chưa nhập black background | Bản VNS hỗ trợ cascade/inheritance/reset, font built-in/SD và NFC/NFD; ánh xạ bổ sung `ơ`, `ư` cùng Latin Extended Additional thay vì dùng nguyên bản CrossInk. Black background chưa có fixture/lợi ích đủ rõ để nhận thêm nhánh render đảo màu. |
| Layout khi thiếu RAM | Nhập ngưỡng bảo vệ và giải phóng cache font, giữ fallback `Standard → Safe` của VNS | Parser dừng trước các vùng tăng STL/shared pointer ở 44 KiB free heap hoặc 32 KiB largest block, thử giải phóng cache font SD 1 lần rồi trả lỗi có phân loại để reader dựng lại Safe. CrossInk vẫn hơn về kiến trúc arena và 4 cấp giảm chất lượng; đây là phần còn lại cho vòng sau. |
| Ảnh ngoài viewport | VNS đã nhập clipping nhưng siết chặt hơn CrossInk | PXC chỉ đọc hàng/cột nhìn thấy; decode một phần không được ghi cache. PNG cold path chặn cả tọa độ âm lẫn vượt mép, tránh ghi ngoài framebuffer. |
| Kiểm thử render | Giữ render lab của VNS | Fixture tiếng Việt, NFC/NFD, 4 style SD font, CSS, bảng, ảnh và cold/warm được khóa pixel/cấu trúc qua nhiều profile. |

Đợt rà soát phát hiện 2 lỗi có thể ảnh hưởng production và đã sửa: ảnh giao mép trước đây bị bỏ toàn bộ; PNG cold decode có thể ghi ngoài framebuffer khi tọa độ âm. Đồng thời đường dựng section được chặn sớm khi heap phân mảnh, thử nhả cache font SD và chuyển sang Safe thay vì tiếp tục đi vào các allocation không thể bắt lỗi khi firmware tắt exception.

Sau bản sửa small caps, 196/196 host test đạt; full render 51 case, font 5 case và Safe 2 case đều ổn định qua 2 lần chạy. Build phát triển `default`/`sticky` dùng 57.180 B/66.828 B RAM tĩnh và 5.651.755 B/5.440.439 B flash. So với mốc rà soát trước đó, RAM tĩnh không đổi; flash tăng lần lượt 2.656 B và 2.800 B. Không tạo artifact release trong bước này. Chưa có thiết bị vật lý nên boot, OTA, độ đậm font, LUT và ghosting vẫn là gate chưa xác nhận.

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
