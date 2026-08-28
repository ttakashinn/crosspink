# CrossPoint agent guide

## Mục tiêu ưu tiên

Khi các mục tiêu xung đột, ưu tiên theo thứ tự sau:

1. Firmware đúng chức năng, ổn định trên thiết bị và không làm hỏng dữ liệu đọc.
2. Chất lượng render có thể quan sát hoặc đo được, đặc biệt với EPUB, font và ảnh.
3. Hiệu năng, RAM/heap, kích thước firmware và thời gian phản hồi trên phần cứng giới hạn.
4. Chất lượng mã nguồn: dễ hiểu, có phạm vi thay đổi nhỏ và có kiểm thử phù hợp.
5. Tốc độ giao hàng.

Bảo mật vẫn phải đúng với phạm vi thay đổi, nhưng không thêm quy trình, lớp trừu tượng hoặc bước phê duyệt chỉ để “cứng hóa” agent khi không có rủi ro cụ thể.

## Cách làm việc

- Chủ động đọc mã, sửa file, build và chạy kiểm thử nằm trong phạm vi yêu cầu mà không chờ xác nhận từng bước.
- Bắt đầu bằng baseline tái lập được. Với tối ưu render hoặc hiệu năng, giữ số đo hoặc ảnh so sánh trước/sau.
- Ưu tiên thay đổi nhỏ ở nguyên nhân gốc. Không refactor diện rộng nếu chưa cần để đạt mục tiêu.
- Không tuyên bố đã xác nhận trên thiết bị khi mới chỉ build hoặc chạy host test. Ghi rõ phần nào còn cần kiểm tra phần cứng.
- Giữ nguyên thay đổi không liên quan của người dùng. Không commit, push, rebase hoặc đổi lịch sử Git nếu chưa được yêu cầu.
- Với công việc firmware, parser, font, EPUB, ảnh hoặc pipeline render, dùng skill `.agents/skills/crosspoint-firmware/SKILL.md`.
- `.skills/` là thư viện hướng dẫn upstream theo từng chủ đề; chỉ tra cứu skill liên quan khi cần, không dùng toàn bộ thư viện làm checklist mặc định.

## Ràng buộc kỹ thuật chính

- X3/X4 dùng ESP32-C3 với RAM hạn chế và chế độ single framebuffer. Tránh tăng peak heap hoặc tạo allocation lặp trong vòng render/parse nóng nếu chưa có số đo chứng minh an toàn.
- Sticky dùng ESP32-S3 nhưng vẫn phải giữ hành vi tương thích giữa các env khi mã dùng chung.
- Dùng lớp lưu trữ của dự án (`HalStorage`, `HalFile`) thay vì truy cập filesystem trực tiếp trong mã ứng dụng mới.
- Chuỗi hiển thị cho người dùng phải đi qua hệ thống i18n hiện có.
- Thay đổi cấu trúc dữ liệu nhị phân hoặc cache phải xử lý version/invalidation tương ứng.
- Không thêm dependency firmware nếu lợi ích không vượt rõ chi phí RAM, flash, build và bảo trì.

## Lệnh chuẩn

Cài môi trường cục bộ một lần:

```sh
python3 scripts/codex_setup.py bootstrap
python3 scripts/codex_setup.py doctor
```

Host unit tests:

```sh
python3 scripts/codex_setup.py test
```

Build firmware mặc định:

```sh
python3 scripts/codex_setup.py build --env default
```

Kiểm tra nhanh trước khi bàn giao thay đổi firmware:

```sh
python3 scripts/codex_setup.py verify --level quick
```

Kiểm tra tương đương CI cho thay đổi lớn hoặc liên quan nhiều thiết bị:

```sh
python3 scripts/codex_setup.py verify --level full
```

## Mức kiểm chứng theo loại thay đổi

- Tài liệu hoặc cấu hình agent: kiểm tra cú pháp, link/file tham chiếu và diff.
- Logic host-testable: chạy suite liên quan; chạy toàn bộ host tests trước khi bàn giao nếu thời gian hợp lý.
- Mã firmware dùng chung: host tests phù hợp và build `default`.
- Driver, display, board config hoặc mã phụ thuộc MCU: build các env bị ảnh hưởng; dùng `default` và `sticky` khi thay đổi chạm mã dùng chung giữa C3/S3.
- Render, font, EPUB hoặc ảnh: thêm fixture/regression test khi có thể; so sánh output hoặc ảnh chụp thiết bị; ghi heap và thời gian cho đường chạy bị thay đổi.

Tài liệu kiến trúc và workflow hiện hành nằm trong `docs/contributing/`; CI chuẩn nằm trong `.github/workflows/ci.yml`.
