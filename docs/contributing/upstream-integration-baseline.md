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
