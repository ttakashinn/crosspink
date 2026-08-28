# Baseline tích hợp upstream cho `vns-next`

Tài liệu này khóa baseline trước khi nhập `upstream/develop` vào nhánh tích hợp. Các số liệu dưới đây được tạo từ cùng một checkout và chỉ dùng để so sánh trước–sau; chúng không thay thế kiểm chứng trên thiết bị thật.

## Nguồn và môi trường

| Thuộc tính | Giá trị |
| --- | --- |
| Ngày đo | 28/08/2026 |
| Nhánh nguồn | `vns-next` |
| Commit nguồn | `00ab726a8d7b011651197c3495485ff21669736c` |
| Nhánh tích hợp | `codex/vns-next-upstream-integration` |
| Python | 3.14.3 |
| PlatformIO Core | 6.1.19 |
| CMake | 4.4.2 |
| clang-format | 23.1.0 |
| Submodule | Đã đồng bộ tại commit nguồn |

`python3 scripts/codex_setup.py doctor` đạt toàn bộ yêu cầu bắt buộc. Ninja hệ thống chưa được cài nhưng là công cụ tăng tốc tùy chọn; PlatformIO có tool Ninja riêng cho firmware build.

## Upstream mục tiêu

| Thuộc tính | Giá trị |
| --- | --- |
| Remote | `upstream` (`crosspoint-reader/crosspoint-reader`) |
| Nhánh | `upstream/develop` |
| Commit sau fetch | `4638119b2686f88b00ca00fa67aeb2482be91892` |
| Merge base | `e00f5958dfeea2a3e640c39eb78186fd20996f4b` |
| Divergence | 16 commit riêng phía `vns-next`; 87 commit riêng phía upstream |

`git merge-tree` dự báo 10 file conflict: `.github/workflows/release.yml`, `AGENTS.md`, 2 file dịch Anh/Việt, `CrossPointSettings.h`, `SettingsList.h`, `OtaUpdateActivity.cpp`, `main.cpp`, `OtaUpdater.cpp` và `test/CMakeLists.txt`. Các conflict phải được giải theo hành vi: giữ tính năng Văn Nhân Số và OTA đã kiểm thử, đồng thời nhận cấu trúc reader/render mới của upstream.

## Kết quả trước merge

Lệnh kiểm chứng:

```sh
python3 scripts/codex_setup.py test
python3 scripts/codex_setup.py build --env default --env sticky
```

| Hạng mục | Kết quả | RAM tĩnh theo PlatformIO | Flash theo PlatformIO | Kích thước `firmware.bin` | SHA-256 `firmware.bin` |
| --- | --- | ---: | ---: | ---: | --- |
| Host tests | 141/141 đạt | — | — | — | — |
| `default` (ESP32-C3, X3/X4) | Đạt | 51.308/327.680 B | 5.804.201/6.553.600 B | 5.818.048 B | `5be2dd64eece49e1859d9d6978fc33bc8ef4c1540ce932e495667d793d44e372` |
| `sticky` (ESP32-S3) | Đạt | 60.972/327.680 B | 5.601.003/6.553.600 B | 5.601.504 B | `65573d7b327b1f3daf410169110b6156393b5528c4a46510305836967e509c8b` |

Build `default` có 1 cảnh báo deprecation từ dependency `WebSockets` gọi `NetworkClient::flush()`; không có lỗi build. Cảnh báo này là trạng thái baseline, không được quy cho merge upstream nếu vẫn xuất hiện sau merge.

## Phạm vi chưa kiểm chứng

- Chưa flash lên X3, X4 hoặc Sticky.
- Chưa đo free heap, largest free block, thời gian `prewarm`, `bw_render` hoặc thời gian chuyển trang trên thiết bị.
- Chưa tạo framebuffer/golden image từ fixture `crosspoint-render-reference-v1.0.epub` vì simulator headless thuộc Giai đoạn 1.
- Chưa xác nhận ghosting, waveform, dither và độ đậm nét trên panel thật.

Sau merge phải chạy lại đúng các lệnh và env trên, rồi ghi số chênh lệch tuyệt đối so với bảng này. Cache build chỉ được dùng để tăng tốc; số liệu RAM/flash phải lấy từ linker của commit sau merge.

## Kết quả sau merge

Kết quả dưới đây được đo trên cây merge giữa baseline `00ab726a` và `upstream/develop` tại `4638119b`, sau khi giải quyết toàn bộ conflict. Build `default` và `sticky` đều dùng cùng source tree; `sticky` đã dựng lại thư viện ESP-IDF trước khi build firmware ứng dụng.

| Hạng mục | Kết quả | RAM tĩnh theo PlatformIO | Chênh lệch RAM | Flash theo PlatformIO | Chênh lệch flash | Kích thước `firmware.bin` | SHA-256 `firmware.bin` |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| Host tests | 180/180 đạt | — | +39 test | — | — | — | — |
| `default` (ESP32-C3, X3/X4) | Đạt | 57.180/327.680 B | +5.872 B | 5.641.493/6.553.600 B | -162.708 B | 5.655.200 B | `1265cf64f6dd9982ccb6b4284d7cf3edde20b6dd9f969f7fe83c587f9888d707` |
| `sticky` (ESP32-S3) | Đạt | 66.828/327.680 B | +5.856 B | 5.430.115/6.553.600 B | -170.888 B | 5.430.624 B | `29e8c1d42a11481fc0997b92a85ba3c260e340e60ea1af5db650981d446c8346` |

Flash giảm trên cả 2 env, phù hợp với việc nhận định dạng font nén mới của upstream. Static RAM tăng khoảng 5,8 KiB trên mỗi env; đây là số linker, chưa cho biết peak heap khi render và phải được theo dõi tiếp trên thiết bị ở Giai đoạn 1–2.

Các gate không-build giữ nguyên đúng nợ nền đã ghi trước merge:

- Format toàn repo vẫn chỉ thất bại tại `lib/MiniBidi/minibidi.c`.
- Cppcheck vẫn chỉ báo 1 cảnh báo mức `low` (`useStlAlgorithm`) tại `src/features/vannhanso/VanNhanSoCache.cpp:23`.
- Dependency `WebSockets` vẫn phát cảnh báo deprecation về `NetworkClient::flush()`; không có cảnh báo mới từ mã tích hợp.

Hai nợ nền format/cppcheck sẽ được sửa bằng commit riêng sau merge. Chưa có kết quả kiểm chứng trên X3, X4 hoặc Sticky thật, vì vậy trạng thái hiện tại mới xác nhận compile, host behavior và kích thước binary.
