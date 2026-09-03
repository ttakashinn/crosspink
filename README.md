# CrossPink

CrossPink là firmware đọc sách mã nguồn mở cho thiết bị e-ink ESP32. Đây là fork cá nhân của [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), tập trung vào trải nghiệm đọc tiếng Việt, EPUB khó và tính năng Lịch Âm Việt Nam nhưng vẫn giữ thay đổi nhỏ, có kiểm thử và phù hợp với giới hạn RAM của thiết bị.

## Thiết bị hỗ trợ

- Xteink X3 và X4 dùng chung firmware ESP32-C3.
- Seeed Studio Sticky dùng firmware ESP32-S3 riêng.
- Xteink X4 Pro, X4 Classic và M5Stack Paper Mono có profile build riêng. Release tự động hiện phát hành C3 và X4 Pro; X4 Classic/Paper Mono cần build từ source cho tới khi có asset chính thức tương ứng.

## Điểm khác biệt của CrossPink

CrossPink kế thừa kiến trúc activity, cache EPUB và lớp phần cứng của CrossPoint. Khi cần sửa UI hoặc phần cứng dùng chung, repository ghim một nhánh `freeink-sdk` đã hợp nhất upstream với các chỉnh sửa tương thích riêng; thay đổi vẫn được giữ nhỏ và build trên cả ESP32-C3 lẫn ESP32-S3.

### Các điểm chính

- Màn hình ngủ Văn Nhân Số có cache ngoại tuyến, cập nhật khi cần và backoff khi mạng lỗi.
- Màn hình ngủ ôn từ đã tra, lấy ngẫu nhiên từ lịch sử StarDict khi có đủ từ và nghĩa; thiếu dữ liệu sẽ quay về màn mặc định an toàn.
- EPUB có 3 cấp dàn trang `Standard`, `Simplified`, `Safe`; chỉ hạ cấp khi thiếu bộ nhớ và cho phép thử lại chất lượng đầy đủ.
- Tùy chọn theo từng sách: khoảng cách từ, sửa thụt đầu dòng, tự lật trang 5–120 giây và lưu trạng thái tắt tự lật.
- Liên kết/footnote chạm được, số trang nhà xuất bản, vị trí tham chiếu độc lập với font và hướng màn hình, cùng ước lượng thời gian đọc còn lại khi dữ liệu đủ tin cậy.
- Cache và dữ liệu tạm có version, validation và phục hồi backup sau khi ghi bị gián đoạn.

CrossInk và CrossVi là nguồn tham khảo cho một số cơ chế typography, fallback, menu và telemetry. Mã được đưa vào CrossPink được đối chiếu và điều chỉnh theo kiến trúc CrossPoint thay vì chép nguyên fork.

## Khả năng đọc sách

- Đọc `.epub`, `.xtc/.xtch`, `.txt` và `.bmp`.
- EPUB 2/3 với CSS nhúng, hyphenation, kerning, ảnh, bookmark, footnote, từ điển StarDict, chuyển chương và đi tới phần trăm.
- Font tích hợp và font `.cpfont` từ thẻ SD.
- Đồng bộ tiến độ KOReader, OPDS có máy chủ đã lưu, Calibre Wireless và WebDAV.
- Screenshot, recent books, xóa cache sách, ẩn file và xoay màn hình.
- Theme, màn ngủ tùy chỉnh, thanh trạng thái, ánh xạ nút, chế độ refresh, hành vi nút nguồn và lật trang bằng nghiêng trên X3.
- Giao diện nhiều ngôn ngữ, bao gồm tiếng Việt và hỗ trợ RTL.

## Mẹo để đọc ổn định

ESP32-C3 có khoảng 380 KB RAM khả dụng, vì vậy CrossPink ưu tiên cache trên thẻ SD hơn là giữ dữ liệu EPUB lớn trong RAM.

- Chia thư viện thành các thư mục nhỏ thay vì để hàng trăm sách trong thư mục gốc.
- EPUB chủ yếu là văn bản hoạt động tốt nhất. Sách scan, comic, ảnh độ phân giải cao hoặc file có hàng nghìn section có thể tải chậm hoặc thiếu bộ nhớ.
- Dùng thẻ SD ổn định và giữ dung lượng trống cho cache, tiến độ, setting và dữ liệu màn ngủ.
- Khi EPUB nặng, mở File Transfer để tối ưu ảnh hoặc tách omnibus thành các phần nhỏ hơn.

## Cài đặt

Tải image đúng thiết bị từ [Releases](https://github.com/ttakashinn/crosspink/releases), rồi flash bằng công cụ web hỗ trợ nạp file `.bin` tùy chỉnh hoặc bằng `esptool`.

```sh
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x10000 firmware.bin
```

Thay `/dev/ttyACM0` bằng cổng thiết bị của bạn. Dùng `firmware.bin` cho X3/X4; X4 Pro dùng `firmware-x4pro.bin`. X4 Classic/Paper Mono hiện cần build image từ source.

### Thiết bị bị khóa USB

Không flash CrossPink bằng Xteink Unlocker trừ khi nhà cung cấp unlocker xác nhận rõ image này được hỗ trợ. Công cụ đó hiện chỉ công bố hỗ trợ các firmware chính thức mà họ nêu tên; nạp fork ngoài danh sách có thể khiến máy không còn đường khôi phục qua USB.

## Cập nhật OTA

CrossPink nhận OTA từ GitHub Releases của chính repository này. Phiên bản phát hành có dạng `1.6.0-cp.N.M`, ví dụ `1.6.0-cp.3.0`; OTA so sánh đủ `N.M` nhưng chỉ giữa các bản cùng nhánh `cp`, do đó không tự chuyển qua lại giữa CrossPink, VNS và CrossPoint.

Lần chuyển từ firmware VNS/CrossPoint sang CrossPink cần flash thủ công một lần, vì firmware cũ không biết format phiên bản `-cp.N.M` và không truy vấn release repository CrossPink.

## Font từ thẻ SD

Bạn có thể chuyển TTF/OTF thành `.cpfont` mà không phải build lại firmware.

1. Tạo font bằng công cụ SD-card font builder của CrossPoint hoặc script `lib/EpdFont/scripts/fontconvert_sdcard.py`.
2. Chép font vào `/fonts/TenFont/` hoặc `/.fonts/TenFont/` trên thẻ SD.
3. Chọn font trong cài đặt đọc sách.

CrossPink vẫn dùng manifest font công khai của CrossPoint; không có kho font CrossPink riêng.

## Tài liệu

- [Tài liệu đóng góp và kiến trúc](./docs/contributing/README.md)
- [Màn ngủ ôn từ đã tra](./docs/features/dictionary-review-sleep.md)
- [Vị trí tham chiếu VNS](./docs/contributing/vns-reference-position.md)
- [Web server](./docs/webserver.md) và [API/endpoints](./docs/webserver-endpoints.md)
- [Font từ thẻ SD](./docs/sd-card-fonts.md)
- [Định dạng file và cache](./docs/file-formats.md)
- [Khắc phục thiết bị Xteink bị brick](./docs/fix-bricked-xteink.md)
- [Ghi chú phát hành 1.6.0-cp.3.6](./docs/releases/1.6.0-cp.3.6.md)

## Phát triển nhanh

```sh
git clone --recursive git@github.com:ttakashinn/crosspink.git
cd crosspink
python3 scripts/codex_setup.py bootstrap
python3 scripts/codex_setup.py doctor
python3 scripts/codex_setup.py test
python3 scripts/codex_setup.py build --env default
```

Build `default` dùng cho X3/X4; Sticky cần build riêng:

```sh
python3 scripts/codex_setup.py build --env sticky
```

Trước khi bàn giao thay đổi firmware, chạy:

```sh
python3 scripts/codex_setup.py verify --level quick
```

## Cấu trúc repository

- `src/`: ứng dụng, activity, state, network, boot và sleep.
- `lib/`: EPUB/layout, font, i18n, parser, lưu trữ và HAL dùng chung.
- `freeink-sdk/`: SDK display, input, storage, nguồn và board support.
- `test/`: host unit tests, fixture EPUB và regression test.
- `docs/`: tài liệu thiết kế, vận hành và phát hành.
- `scripts/`: build, codegen, render lab, kiểm thử và công cụ phát hành.

## Dữ liệu trên thẻ SD

CrossPink giữ thư mục `/.crosspoint` của CrossPoint để thiết bị nâng cấp không mất setting, lịch sử đọc và cache. Không đổi tên hoặc xóa thư mục này chỉ vì rebrand; xóa nó sẽ buộc firmware dựng lại cache và mất dữ liệu cục bộ liên quan.

## Đóng góp

CrossPink là fork cá nhân. Nếu bạn có lỗi tái lập được, hãy mở issue kèm thiết bị, phiên bản `cp`, sách/fixture tối thiểu và log serial khi có thể. Thay đổi kiến trúc lớn nên được thảo luận trước, đặc biệt nếu chúng ảnh hưởng RAM ESP32-C3, cache nhị phân hoặc tương thích với CrossPoint upstream.

CrossPink không liên kết với Xteink hay bất kỳ nhà sản xuất thiết bị nào.
