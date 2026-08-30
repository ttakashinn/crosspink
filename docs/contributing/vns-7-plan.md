# Kế hoạch CrossPoint Văn Nhân Số `1.6.0-vns.7`

Ngày lập: 30/08/2026

Trạng thái: đã hoàn tất mã, host/simulator gate và artifact production; hardware gate X3/X4 Pro còn mở

## Kết luận

`1.6.0-vns.7` nên tập trung vào trải nghiệm đọc ổn định, dễ điều khiển và có vị trí tham chiếu nhất quán. Bản này không nên trở thành một đợt gom tính năng từ các fork.

Phạm vi thực hiện gồm 7 nhóm:

1. Đo đầy đủ vòng đời lật trang và chặn việc nền sau tương tác.
2. Thêm chế độ render trung gian để sách khó không phải rơi thẳng từ `Standard` xuống `Safe`.
3. Tổ chức lại menu reader cho thiết bị dùng nút, đồng thời nhận các sửa giao diện và liên kết cảm ứng phù hợp từ CrossPoint.
4. Bổ sung các tùy chọn đọc thực dụng theo từng sách: khoảng cách từ, sửa thụt đầu dòng và thời gian tự lật trang.
5. Hỗ trợ số trang của nhà xuất bản và thiết kế vị trí tham chiếu ổn định cho hệ sinh thái Văn Nhân Số.
6. Hoàn tất kiểm chứng phần cứng còn thiếu của `vns.6`, sau đó khóa regression trên X3, X4 Pro và mã dùng chung với Sticky.
7. Thêm màn hình ngủ ôn từ đã tra theo lựa chọn rõ ràng, ngoại tuyến và có fallback không gây phiền.

CrossInk và CrossVi tiếp tục là nguồn tham khảo cơ chế. Không nhập toàn bộ renderer, hệ thống setting, thư viện hoặc giao diện của bất kỳ fork nào.

Kết quả release ở mức mã nguồn được ghi tại `docs/releases/1.6.0-vns.7.md`. Các bước cần panel, SD, radio hoặc 2 firmware thật không được đánh dấu hoàn thành chỉ từ simulator/build; cụ thể còn p95 latency/heap, waveform/ghosting, boot/wake, Wi-Fi, dictionary `.dict.dz` lúc ngủ và OTA `vns.6 → vns.7`.

## Đối soát hoàn thành

| Phase | Kết quả mã nguồn/host/simulator | Gate phần cứng |
| --- | --- | --- |
| 0 — Baseline `vns.6` | Đã khóa commit, số host test, render và kích thước build làm mốc so sánh. | Chưa có X3/X4 Pro để chạy lại smoke, Wi-Fi, OTA và chuỗi ghosting của baseline. |
| 1 — Telemetry/scheduler | Đã có FIFO 8 lượt, đủ mốc input–visible, snapshot heap, idle guard dùng chung và host test; giá trị mặc định là 1.000 ms. | A/B 400/750/1.000 ms và p95 trên X3 còn mở. |
| 2 — Render resilience | Đã có 3 mode, phân loại OOM, cache invalidation, setting/migration, fallback chỉ được lưu sau lần dựng thành công và regression test. | Peak/largest heap và EPUB lỗi RAM thực còn mở. |
| 3 — UI/điều khiển | Đã có menu 3 tab cho phím, giữ toolbar cảm ứng, link/footnote có hit-region giới hạn và chọn vùng chạm gần nhất. | Touch target, phím vật lý và orientation trên panel còn mở. |
| 4 — Tùy chọn theo sách | Đã có word spacing, sửa indent, auto-turn 5–120 giây, persistence và golden liên quan; auto-turn không làm nhiễu pace. | Độ rõ typography và cảm nhận nhịp tự lật còn mở. |
| 5 — Vị trí tham chiếu | Đã có `vnspos:1`, QR giữ trọn token, Time Left có confidence gate, `page-list.bin` và parser page-list/pagebreak có giới hạn. Render lab khóa marker trong `span` rỗng trên 3 profile. | Đối chiếu sách thương mại và độ dễ đọc status bar trên panel còn mở. |
| 6 — Release hardening | 344/344 host test, `clang-format`, `cppcheck`, 81 render case × 2 lượt, build `default`, `sticky`, `gh_release` và `x4pro-gh_release` đều qua; 2 image production có checksum/hash hợp lệ. | Smoke X3/X4 Pro, OTA/rollback, Wi-Fi, waveform và ghosting còn mở. |
| 7 — Ôn từ đã tra | Đã thêm mode opt-in, tra ngoại tuyến có giới hạn, tránh lặp, fallback im lặng, parser và UI UTF-8; dữ liệu tối thiểu chỉ còn **từ + giải nghĩa**. | Thời gian lookup `.dict.dz`, heap và bố cục trên màn thật còn mở. |

Kết luận đối soát: phạm vi triển khai `vns.7` đã hoàn tất ở mức mã nguồn, host test, simulator và artifact production. Hardware gate là bước xác nhận phát hành trên thiết bị, không phải phần mã còn thiếu; nó chỉ có thể đóng sau khi có X3 và X4 Pro vật lý cùng thẻ SD/từ điển đại diện.

## Baseline có thể truy vết

| Nguồn | Commit | Trạng thái ngày 30/08/2026 |
| --- | --- | --- |
| Văn Nhân Số | `17b5e1ca874f776f1159a72a8683e42e62cac04b` | `1.6.0-vns.6`; worktree sạch |
| CrossPoint `develop` | `790a0817e6a4d1e920af679ab59673d9d751d2be` | Đã là tổ tiên của `vns-next`; VNS không thiếu commit nào và có 45 commit riêng |
| CrossInk `main` | `cab4f24922f05811e7f44be1057f62ea2d978c52` | Nguồn tham khảo typography, reader mode, menu và vị trí tham chiếu |
| CrossVi `develop` | `b16d0c016ac9f931f432913860ce2f465905d5a7` | Nguồn tham khảo idle guard và telemetry lật trang |

Baseline build đã ghi trong release note `vns.6`:

- 319/319 host unit test thành công.
- 78 case render X3/X4 chạy ổn định qua 2 lần.
- Build `default` và `sticky` thành công.
- Build C3 dùng 57.396 B RAM tĩnh và 5.884.395 B flash trên partition 6.553.600 B, còn 669.205 B.
- Build production X3 và X4 Pro thành công.

Các kết quả trên chưa thay cho kiểm tra thiết bị thật. Wi-Fi, clipping nhiều trang, OTA, waveform, độ trễ phím và ghosting của `vns.6` vẫn cần smoke test phần cứng trước khi khóa baseline `vns.7`.

## Ma trận lựa chọn

| Cơ chế | Nguồn | Quyết định cho `vns.7` | Lý do |
| --- | --- | --- | --- |
| Telemetry `input → queued → render begin → visible` | CrossVi | Nhập bằng mã VNS | Cần có số đo trước khi tối ưu thêm reader |
| Post-visible idle guard | CrossVi | Nhập bằng mã dùng chung cho EPUB/TXT/XTC | Tránh prewarm/indexing chen vào ngay sau thao tác |
| Hàng đợi lật trang nguyên tử tối đa 8 lượt | CrossVi | Không nhập | `BoundedPageTurnQueue` của VNS đã có biên, không cấp phát và xử lý triệt tiêu chiều ngược |
| `Default → Balanced → Light → Safe` | CrossInk | Rút gọn thành `Standard → Simplified → Safe` | Có đường lui vừa phải nhưng không nhân ba số nhánh render |
| Lưu render mode theo sách | CrossInk | Nhập có sửa | Tách chế độ người dùng chọn khỏi fallback tự động để không hạ chất lượng âm thầm |
| Word spacing | CrossInk | Nhập 3 mức theo từng sách | Hữu ích cho khả năng đọc, chi phí triển khai có biên |
| Force paragraph indents | CrossInk | Nhập dưới dạng chế độ sửa EPUB | Chỉ can thiệp khi indent tính được bằng 0, tránh thụt kép |
| Bionic Reading | CrossInk | Không nhập | VNS đã có Focus Reading tương đương |
| Guide Dots | CrossInk | Không nhập | Lợi ích chưa đủ rõ và làm tăng nhiễu thị giác/pagination |
| Bitter/Lexend làm font mặc định | CrossInk | Không nhập | Noto của VNS có độ phủ tiếng Việt và fixture NFC/NFD tốt hơn; font ngoài vẫn dùng qua SD |
| Ngưỡng AA làm chữ đậm mặc định | CrossInk | Chỉ thử nghiệm A/B, không đổi mặc định trong code trước khi có ảnh panel | Simulator không xác nhận được bết nét, counter và ghosting |
| Menu reader chia nhóm | CrossInk | Nhập ý tưởng, dựng trên `UiTabListActivity` | Danh sách reader hiện quá dài trên thiết bị dùng nút |
| Stable Page Numbers | CrossInk | Thiết kế lại thành vị trí tham chiếu VNS | Phù hợp đồng bộ, clipping và nội dung do VNS chuẩn bị |
| Publisher Page Numbers | CrossInk | Nhập theo chuẩn EPUB | Có giá trị với sách đã có page-list/pagebreak; không phụ thuộc backend VNS |
| Time Left trên status bar | CrossInk | Nhập chọn lọc | VNS đã có dữ liệu pace; chỉ hiển thị khi mẫu đủ tin cậy |
| Auto Page Turn 5–120 giây, theo sách | CrossInk | Nhập với picker có biên | Thay preset pages/minute khó hiểu bằng thời gian trực tiếp |
| Nearby Position/Stats Sync qua ESP-NOW | CrossInk | Hoãn | Mã lớn, cần 2 thiết bị, không phù hợp ưu tiên reader `vns.7` |
| Snapshot thống kê theo `deviceId` | CrossInk | Giữ làm thiết kế cho backend sau này | Tránh cộng trùng khi VNS có đồng bộ tài khoản; không cần firmware trong bản này |
| Touch EPUB links/footnotes | CrossPoint `feat/touch-footnotes` | Nhận sau khi upstream merge hoặc audit riêng | Nhánh hiện chỉ hơn `develop` 2 commit và có host test, nhưng vẫn chưa phải production baseline |
| Sửa overlay/tab/frontlight của UI mới | CrossPoint `fix/new-ui-issues` | Nhận sau khi upstream merge; nếu chưa merge chỉ backport lỗi tái hiện được | Nhánh bám đúng `develop` nhưng gồm cả cập nhật submodule |
| Unified reader shortcut actions | CrossPoint `feature/crossink-controls-port` | Cổng điều kiện, không cherry-pick nhánh hiện tại | Nhánh đang thiếu 98 commit `develop` và chạm sâu settings/input/migration |
| Library index CLX1 | CrossPoint `feat/library-view` | Hoãn sang bản sau | Hơn 3.000 dòng, mới có core/index chưa có UX hoàn chỉnh và đang thiếu 13 commit `develop` |
| Chunked SD-font bitmap allocation | CrossPoint `feat-chunk-bitmaps` | Hoãn, chỉ quay lại khi heap profile chứng minh cần | Nhánh thiếu 104 commit; VNS đã có heap gates và font cache riêng |
| Indexed/streaming clipping store | CrossVi | Hoãn | `vns.6` vừa phát hành clipping format v3; không tạo format v4 ngay khi chưa có số đo heap thực |
| Managed sleep-image catalog | CrossVi | Không đưa vào `vns.7` | Chồng lấn sản phẩm màn hình ngủ Văn Nhân Số và không cải thiện reader |
| Ôn từ đã tra trên màn hình ngủ | Ý tưởng Văn Nhân Số | Nhập dưới dạng mode tự chọn | Tận dụng lịch sử hiện có; không bật mạng, không tự index và luôn có fallback |
| Nhiều Home layout, carousel, achievements | CrossVi/CrossInk | Không đưa vào `vns.7` | Tăng flash, i18n và nhánh UI nhưng không giải quyết độ ổn định khi đọc |

## Thiết kế mục tiêu

### 1. Telemetry và khoảng yên tĩnh của reader

Thêm một lớp dùng chung cho EPUB, TXT và XTC với các trạng thái:

- `input`: nhận cạnh phím hoặc gesture hợp lệ.
- `queued`: intent được đưa vào hàng đợi.
- `render_begin`: reader bắt đầu dựng framebuffer cho intent đó.
- `visible`: lệnh refresh tương ứng đã được phát và trang được xem là hiển thị.
- `idle_work_begin/end`: prewarm hoặc indexing nền bắt đầu/kết thúc.

Mỗi lượt có sequence ID, hướng, vị trí trước/sau, queue depth, thời gian và snapshot `free heap`/`largest block`. Telemetry chỉ bật ở build phát triển; production không ghi SD và không tạo allocation theo lượt.

Idle guard chỉ cho phép việc tùy chọn khi:

- Trang đầu đã đạt trạng thái `visible`.
- Không có input, overlay, auto-turn hoặc page-turn intent chờ xử lý.
- Không có render lock.
- Một khoảng yên tĩnh liên tục đã trôi qua kể từ input cuối.
- Heap vượt ngưỡng riêng của việc nền.

Không chốt 400, 750 hay 1.000 ms bằng suy đoán. Build thử nghiệm phải cho phép so sánh 3 giá trị này trên X3; chọn giá trị có p95 `input → visible` tốt mà không làm prewarm/indexing đói kéo dài.

### 2. Render mode 3 cấp

| Mode | Giữ lại | Giản lược |
| --- | --- | --- |
| `Standard` | Toàn bộ hành vi production hiện tại | Không |
| `Simplified` | Text, ảnh, định dạng inline cơ bản, hidden content, page break, spacing an toàn | Selector descendant đắt, table phức tạp và decoration không thiết yếu |
| `Safe` | Text và định dạng tối thiểu | CSS nhúng; ảnh chuyển thành alt-text như hiện tại |

Chỉ lỗi đã phân loại là thiếu bộ nhớ mới được tự fallback. Parse error, hỏng ZIP, lỗi SD hoặc cache sai không được che bằng đổi mode.

Per-book store phải tách:

- `preferredRenderMode`: do người dùng chọn.
- `lastWorkingFallback`: mode đã dựng thành công sau lỗi thiếu RAM, kèm render signature.

Khi dùng fallback đã lưu, UI phải báo ngắn gọn “Chế độ tương thích” và có hành động “Thử chất lượng đầy đủ”. Không ghi đè setting toàn cục. Đổi mode phải invalid đúng CSS/section cache nhưng giữ progress, bookmark, clipping và reading stats.

### 3. Menu reader và điều khiển

Giữ toolbar `Contents / Text / More` trên thiết bị cảm ứng. Chuyển menu danh sách trên thiết bị dùng nút thành 3 tab:

1. `Đọc`: chương, tra từ, text settings, tự lật, đi tới phần trăm.
2. `Đánh dấu`: bookmark, clipping, QR và đồng bộ tiến độ.
3. `Khác`: xoay màn hình, thống kê, screenshot, về Home và xóa cache.

Các hàng chỉ xuất hiện khi có khả năng tương ứng, ví dụ footnote, dictionary, bookmark hoặc frontlight. Thứ tự phím và touch phải dùng chung action model; không tạo một enum hành động thứ ba nếu nhánh Controls của CrossPoint được hợp nhất trước feature freeze.

Nhận touch link/footnote từ CrossPoint theo cơ chế hit-region gắn với page cache. Thay đổi binary page cache phải bump version và kiểm tra cache cũ bị loại bỏ an toàn.

### 4. Tùy chọn đọc theo từng sách

Mở rộng `PerBookReaderSettings` và codec có version/migration cho:

- `wordSpacing`: 0, 1, 2.
- `repairParagraphIndent`: tắt/bật.
- `autoPageTurnSeconds`: 0 hoặc 5–120 giây.
- `preferredRenderMode` và metadata fallback nêu trên.

Word spacing phải dùng cùng phép đo khi layout và render, kể cả justify, punctuation, CJK, RTL và Focus Reading hiện hữu. Repair indent chỉ thêm indent khi phần tử là paragraph và computed indent bằng 0; không ghi đè indent âm/hanging indent hoặc indent có chủ ý của sách.

Auto-turn hiển thị bằng giây/phút trực tiếp. Giá trị 0 là tắt; thay đổi trong sách không làm đổi mặc định của sách khác.

### 5. Vị trí tham chiếu và số trang

Triển khai theo 2 lớp độc lập:

1. **Publisher page numbers:** đọc page-list hoặc marker `epub:type="pagebreak"`/`role="doc-pagebreak"`, lưu nhãn và content offset có biên, rồi hiển thị nhãn tại trang chứa marker.
2. **VNS reference positions:** định nghĩa metadata version hóa, gắn với hash nội dung, ánh xạ content offset sang vị trí ổn định không phụ thuộc font, margin hoặc orientation.

Firmware phải hoạt động bình thường khi không có metadata VNS. Khi metadata sai version, sai hash, thiếu hoặc vượt giới hạn, firmware bỏ qua và dùng content-based position hiện tại.

Vị trí tham chiếu được dùng cho status bar và QR. Clipping tiếp tục lưu `spineIndex`/`visibleTextOffset`, là cùng content anchor đầu vào; không bump clipping format lần nữa. Progress/KOReader sync giữ protocol hiện hành vì chưa có consumer phía server cho token VNS; không tự nhét metadata riêng vào trường tương thích bên thứ ba.

Time Left dùng pace đã lọc của `ReadingStats`. Chỉ hiển thị sau khi có số mẫu tối thiểu được xác định bằng test; jump chương, bookmark, footnote và auto-turn không được làm nhiễu pace. Khi dữ liệu chưa đủ, status bar ẩn trường này thay vì hiện ước lượng giả.

### 6. Màn hình ngủ ôn từ đã tra

Chế độ mới chỉ hoạt động khi người dùng chọn **Ôn từ đã tra**. Lịch sử hiện tại chỉ lưu truy vấn, nên firmware tra lại bằng từ điển StarDict trên SD và không dùng mạng. Luồng bị chặn tối đa 4 lần thử, không dựng index lúc ngủ, tránh lặp ngay từ vừa hiển thị và dùng màn mặc định nếu không đủ dữ liệu.

Thẻ chỉ bắt buộc có từ và giải nghĩa. Phiên âm, ví dụ và collocation chỉ hiện khi nguồn thực sự cung cấp; thiếu các trường này không làm mất thẻ. Nội dung dài được wrap UTF-8, phần nghĩa được ưu tiên và các vùng tùy chọn co lại khi thiếu chỗ. Thiết kế chi tiết và đánh đổi về snapshot nằm tại `docs/features/dictionary-review-sleep.md`.

## Trình tự triển khai

### Phase 0 — Khóa baseline `vns.6`

1. Chạy lại `python3 scripts/codex_setup.py verify --level full` trên commit baseline.
2. Smoke test X3 và X4 Pro: boot/wake, phím, EPUB, TXT/XTC, OTA, Wi-Fi, KOReader sync, update Văn Nhân Số và clipping 1–4 trang.
3. Ghi log baseline cho 3 EPUB: nhẹ, nhiều CSS/ảnh và chương rất lớn; đo thời gian mở cold/warm, `input → visible`, free heap và largest block.
4. Chụp chuỗi text → image → text để khóa ghosting/waveform.

Điều kiện qua phase: không còn lỗi blocker của `vns.6`; nếu có, sửa riêng trước khi thêm tính năng `vns.7`.

### Phase 1 — Reader observability và scheduler

1. Thêm telemetry phát triển và unit test sequence/queue.
2. Thêm post-visible idle guard dùng chung.
3. Gắn guard vào SD-font prewarm và incremental section build hiện tại.
4. A/B 400/750/1.000 ms trên X3 rồi chốt giá trị.

Điều kiện qua phase: không mất intent khi bấm liên tiếp 1, 2 và 4 lượt; p95 `input → visible` không xấu hơn baseline quá 10%; không có việc nền bắt đầu trong quiet window đã chọn.

### Phase 2 — Render resilience

1. Tách feature profile `Standard`, `Simplified`, `Safe`.
2. Bump cache version/signature và thêm fault injection từng cấp.
3. Mở rộng per-book store, recovery `.tmp/.bak` và migration.
4. Thêm thông báo mode cùng hành động reset/thử lại.

Điều kiện qua phase: fixture khó mở được bằng mode nhẹ hơn; fixture bình thường giữ nguyên golden ở `Standard`; lỗi không phải OOM không kích hoạt fallback; progress/bookmark/clipping không đổi khi chuyển mode.

### Phase 3 — Reader UI và điều khiển

1. Nhận các sửa UI CrossPoint đã merge vào `develop` sau baseline.
2. Chuyển menu list thành 3 tab, giữ toolbar cảm ứng.
3. Nhận touch EPUB link/footnote sau review cache format và hit region.
4. Chỉ nhận unified Controls nếu upstream đã rebase/merge và migration setting qua test; nếu chưa, để ngoài release.

Điều kiện qua phase: toàn bộ action truy cập được bằng phím; touch target không chồng; mở/đóng menu không làm lật trang; orientation và safe-area đúng trên simulator X3/X4/X4 Pro; mã dùng chung tiếp tục build được cho Sticky.

### Phase 4 — Tùy chọn đọc theo sách

1. Thêm word spacing và repair indent vào render spec/cache.
2. Thêm auto-turn interval picker và persistence.
3. Bổ sung golden tiếng Việt, punctuation, justify, CJK, RTL, hanging indent và EPUB cố tình thiếu indent.
4. Kiểm tra cache cold/warm và thay đổi pagination có chủ đích.

Điều kiện qua phase: layout và render dùng cùng advance; không mất dấu tiếng Việt; không double-indent; thay đổi của một sách không rò sang sách khác.

### Phase 5 — Vị trí tham chiếu

1. Viết đặc tả metadata, version và hash trước khi sửa firmware.
2. Parse publisher page markers bằng cấu trúc có giới hạn.
3. Thêm resolver VNS reference position và fixture generator.
4. Tích hợp status bar và QR; clipping tiếp tục dùng cùng content anchor. Giữ nguyên KOReader sync vì protocol hiện tại không có trường VNS tương thích hai phía.
5. Thêm Time Left có confidence gate.

Điều kiện qua phase: cùng một EPUB/hash cho cùng vị trí khi đổi font, margin và orientation; metadata sai bị bỏ qua an toàn; EPUB không có metadata không regress.

### Phase 6 — Release hardening

1. Chạy toàn bộ host tests, render lab cold/warm và cache migration tests.
2. Build `default`, `sticky`, `gh_release` và `x4pro-gh_release`.
3. Chạy A/B thiết bị cho latency, heap, typography, publisher markers và ghosting.
4. Kiểm tra OTA từ `vns.6` lên `vns.7`, rồi rollback có kiểm soát về firmware trước.
5. Ghi release note tách rõ kết quả host/build và phần đã xác nhận trên thiết bị.

### Phase 7 — Màn hình ngủ ôn từ

1. Thêm mode persisted bằng cách nối enum, không đổi giá trị các mode cũ.
2. Tách thẻ ôn tập có giới hạn và host test cho dữ liệu đủ/thiếu/NUL separator.
3. Tích hợp random không lặp ngay, tối đa 4 lookup, không dùng mạng/không build index và fallback mặc định.
4. Rà UI trên portrait, dữ liệu dài và từ điển HTML/plain; ghi rõ phần còn cần thiết bị thật.

## Gate tài nguyên và chất lượng

Các ngưỡng dưới đây là tiêu chí kế hoạch, không phải số đo phần cứng đã có:

| Nhóm | Gate `vns.7` |
| --- | --- |
| Flash C3 | Giữ ít nhất 512 KiB trống trong partition; với baseline hiện tại, tổng tăng ròng không quá khoảng 145 KiB nếu toolchain không đổi |
| RAM tĩnh C3 | Tổng tăng không quá 4 KiB nếu chưa có phân tích heap tương ứng |
| Heap runtime | Không nhận PR làm giảm free heap/largest block vượt mức nhiễu baseline mà không có lợi ích đo được và fallback |
| Reader latency | p95 `input → visible` không regress quá 10% trên cùng thiết bị/EPUB/SD; mục tiêu là giảm ở đường có prewarm/indexing |
| Correctness | Không mất page-turn intent; progress, bookmark, clipping và sync giữ đúng qua re-pagination/cache migration |
| Render | `Standard` không đổi golden ngoài thay đổi đã review; mode mới có golden riêng |
| Storage | Mọi format mới có magic/version/CRC hoặc validation tương đương, `.tmp/.bak`, fail-closed với version mới hơn |
| Hardware | X3 và X4 Pro phải qua smoke; Sticky phải build và chạy simulator/host path cho mã dùng chung |

## Các phần chủ động để ngoài `vns.7`

- Library index/search diện rộng: chờ `feat/library-view` có UI, rebase và số đo flash/heap.
- Clipping format v4 dạng indexed/streaming: chỉ mở lại khi profile 64 clipping × 512 byte chứng minh v3 gây áp lực heap thực.
- Nearby sync qua ESP-NOW và đồng bộ thống kê đa thiết bị.
- Managed custom sleep-image catalog.
- Nhiều Home layout, carousel hoặc achievement.
- Port toàn bộ CrossInk renderer, font bundle, CSS store hoặc settings tree.
- Các nhánh CrossPoint chưa merge nhưng đã lệch xa `develop`, gồm `feature/book-page-counts`, `feat-aa-fonts`, `feat-epub-parser-refactor` và `feat-chunk-bitmaps`.

Các mục này không bị bác bỏ vĩnh viễn. Chúng bị loại khỏi `vns.7` vì chưa đủ ổn định, chưa có số đo hoặc làm loãng mục tiêu reader của bản phát hành.

## Cách chia commit/PR

1. Baseline và log phần cứng.
2. Reader telemetry.
3. Post-visible idle guard.
4. `Simplified` render profile và cache invalidation.
5. Per-book render fallback/migration/UI.
6. Reader menu 3 tab và các UI fix upstream.
7. Touch EPUB links/footnotes.
8. Word spacing và repair indent.
9. Auto-turn interval theo sách.
10. Publisher page markers.
11. VNS reference-position schema và resolver.
12. Status bar/Time Left cùng consumer integrations.
13. Release hardening và tài liệu `vns.7`.
14. Màn hình ngủ ôn từ đã tra và tài liệu thiết kế.

Không gộp render mode, typography, page metadata và UI navigation vào một commit series. Mỗi nhóm phải bisect và rollback độc lập.

## Nguồn đối chiếu

- Pipeline và quyết định CrossInk trước đây: `docs/render-quality-plan.md`.
- Audit CrossVi: `docs/contributing/crossvi-audit-2026-08-29.md`.
- Baseline `vns.6`: `docs/releases/1.6.0-vns.6.md`.
- Kết quả release mã nguồn `vns.7`: `docs/releases/1.6.0-vns.7.md`.
- Reader hiện tại: `src/activities/reader/EpubReaderActivity.cpp`.
- Per-book settings hiện tại: `src/activities/reader/PerBookReaderSettings*`.
- CrossPoint pending refs đã audit: `feat/touch-footnotes`, `fix/new-ui-issues`, `feature/crossink-controls-port`, `feat/library-view` và `feat-chunk-bitmaps`.
- CrossInk: <https://github.com/uxjulia/CrossInk/tree/cab4f24922f05811e7f44be1057f62ea2d978c52>.
- CrossVi: <https://github.com/tvhdc/crossvi/tree/b16d0c016ac9f931f432913860ce2f465905d5a7>.
