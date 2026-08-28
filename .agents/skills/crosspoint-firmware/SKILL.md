---
name: crosspoint-firmware
description: Dùng khi sửa, tối ưu, debug hoặc review firmware CrossPoint; đặc biệt cho pipeline render EPUB/text/image, font, pagination, cache, heap, timing, display driver và khác biệt giữa X3/X4/Sticky.
---

# CrossPoint firmware

## Quy trình mặc định

1. Xác định thiết bị/env, tài liệu đầu vào và đường chạy bị ảnh hưởng.
2. Tạo baseline nhỏ nhất tái lập được: test đang có, EPUB/ảnh mẫu, log timing/heap hoặc ảnh chụp màn hình.
3. Lần theo dữ liệu từ parse/layout đến framebuffer/display; tìm nguyên nhân gốc trước khi thay đổi thuật toán hoặc cache.
4. Sửa trong phạm vi nhỏ nhất có thể và thêm regression test ở lớp thấp nhất chạy được trên host.
5. Kiểm chứng từ rẻ đến đắt: test liên quan, toàn bộ host tests, build env liên quan, rồi mới đo trên thiết bị.
6. Báo cáo riêng kết quả đã đo và phần còn là giả thuyết/chưa có phần cứng xác nhận.

## Render và hiệu năng

- Đánh giá text và image riêng; một cải thiện cho ảnh grayscale không chứng minh chất lượng chữ tốt hơn.
- Với lỗi pagination/layout, giữ fixture EPUB tối thiểu thể hiện đúng lỗi và kiểm tra ổn định qua cache miss/cache hit nếu có cache.
- Với font, kiểm tra ít nhất glyph Latin có dấu, combining marks, kerning/ligature và fallback liên quan trực tiếp đến thay đổi.
- Với ảnh, kiểm tra scaling, clipping, alpha/background, dither/quantization và đường decode theo đúng loại ảnh bị tác động.
- Với thay đổi hiệu năng, ghi trước/sau cho cùng input và build. Ưu tiên các mốc log sẵn có như `prewarm`, `bw_render`, thời gian grayscale, free heap, largest block và peak allocation.
- Không đổi chất lượng lấy tốc độ một cách âm thầm. Nếu dùng heuristic hoặc giảm precision, nêu trường hợp xấu nhất và cách bật/tắt hoặc fallback.
- Khi binary layout, pagination artifact, glyph bitmap hoặc image cache thay đổi, cập nhật version/invalidation; kiểm tra cả dữ liệu cache cũ.
- Tránh allocation theo glyph/scanline/node trong hot loop. Nếu allocation là cần thiết, đo peak heap và fragmentation trên C3.

## So sánh với CrossInk

CrossInk là nguồn tham khảo, không phải đặc tả mặc định. Khi port một ý tưởng:

1. Ghi rõ commit/file/đoạn mã nguồn được đối chiếu.
2. Tách cơ chế cốt lõi khỏi API, driver và giả định phần cứng riêng của CrossInk.
3. Kiểm tra giấy phép và dependency trước khi chép mã; ưu tiên tự triển khai cơ chế nếu nguồn hoặc giấy phép chưa rõ.
4. Chứng minh lợi ích trên fixture/thiết bị CrossPoint. Không giữ một port chỉ vì upstream dùng nó.

## Bản đồ mã nguồn

- Reader và luồng hiển thị: `src/activities/reader/`
- EPUB/layout: `lib/Epub/`, `lib/EpdFont/`, `lib/GfxRenderer/`
- Ảnh: `lib/PngToBmpConverter/`, `lib/JpegToBmpConverter/`
- Display SDK và board support: `freeink-sdk/libs/display/`, `freeink-sdk/libs/hardware/`
- Host regression tests: `test/`
- Cấu hình build/env: `platformio.ini`
- Hướng dẫn kiến trúc và kiểm thử: `docs/contributing/`

## Tiêu chí bàn giao

- Nêu cơ chế lỗi/cải thiện, không chỉ mô tả triệu chứng.
- Có lệnh và kết quả kiểm thử/build thực tế.
- Có số đo hoặc ảnh A/B nếu tuyên bố tăng chất lượng/tốc độ/giảm RAM.
- Ghi rõ env/thiết bị chưa kiểm tra và rủi ro còn lại.
