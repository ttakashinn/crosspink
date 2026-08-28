# Khảo sát CrossVi cho `1.6.0-vns.1`

## Phạm vi và nguồn

Khảo sát này dùng mã nguồn CrossVi tại commit
[`0a4c3bd8a93fa98a9da9e8b1fcf70b7a7d09c4c2`](https://github.com/tvhdc/crossvi/tree/0a4c3bd8a93fa98a9da9e8b1fcf70b7a7d09c4c2),
ngày 26/08/2026. CrossVi và CrossPoint đều dùng giấy phép MIT, nhưng phần triển khai trong nhánh này được viết lại theo kiến trúc và giới hạn của baseline Văn Nhân Số, không chép nguyên khối.

Các vùng được đọc trực tiếp gồm:

- `lib/Serialization/AtomicFile.*` cho ghi file có phục hồi.
- `src/activities/reader/PerBookReaderSettings*` cho cấu hình theo sách và migration.
- `src/clippings/ClippingCodec.*`, `ClippingStore.*`, `ClippingPageTools.*` và các activity liên quan cho clipping, anchor và highlight.
- `src/activities/reader/{Epub,Txt,Xtc}ReaderActivity.*` cho hàng đợi lật trang, ghi progress và vòng đời reader.
- `src/activities/boot_sleep/SafeBootActivity.*` và `lib/GfxRenderer/FontCacheManager.*` để đánh giá các hướng cải thiện kế tiếp.

CrossInk được đối chiếu thêm tại commit
[`cab4f24922f05811e7f44be1057f62ea2d978c52`](https://github.com/uxjulia/CrossInk/tree/cab4f24922f05811e7f44be1057f62ea2d978c52)
cho cơ chế gom lượt ghi progress và arena ban đầu.

## Kết luận áp dụng

| Cơ chế | Quyết định | Cách áp dụng cho Văn Nhân Số |
| --- | --- | --- |
| Ghi progress theo lô | Áp dụng | Ghi sau 5 lần đổi vị trí hoặc 2 phút; luôn flush khi rời reader và trước thao tác phá cache/đồng bộ. Thay đổi metadata không bị tính nhầm là lật trang. |
| Arena cho parser | Áp dụng có siết chặt | Dùng slab có ngân sách 16 KB, kiểm tra overflow/alignment/checkpoint và trả lỗi OOM rõ ràng. Chỉ chuyển stack style nóng; không refactor các container không chứng minh được lợi ích. |
| Cấu hình theo sách | Áp dụng | Format cố định có magic, version và CRC; đọc theo thứ tự canonical, backup, temp; từ chối ghi đè format mới hơn; khôi phục cấu hình toàn cục khi thoát reader. |
| Clipping và highlight | Áp dụng theo phạm vi an toàn | Lưu nguyên văn UTF-8 tiếng Việt, giới hạn kích thước, ghi transaction và xác minh read-back theo khối cố định để giảm peak heap; anchor chính xác và tìm lại quote trong cửa sổ tối đa 4 trang. Highlight dùng gạch chân BW và xóa grayscale giao nhau để không làm nhòe nét trên e-ink. |
| Store clipping dạng chỉ mục/stream | Hoãn | CrossVi tránh giữ toàn bộ text trong RAM bằng chỉ mục và đọc theo offset. Đây là hướng tốt khi cần danh sách, export và clipping nhiều trang, nhưng port nguyên vẹn sẽ kéo theo format/migration/UI lớn hơn phạm vi thay đổi này. |
| Hàng đợi lật trang và telemetry chi tiết | Hoãn | Có ích khi tối ưu cảm giác lật nhanh, nhưng thay đổi mô hình input/render rộng và cần đo trên thiết bị trước khi nhập. |
| Safe boot mở rộng | Hoãn | Không liên quan trực tiếp 4 mục đang triển khai và không nên trộn vào thay đổi render/persistence. |

## Giới hạn cần kiểm tra trên thiết bị

- Chưa xác nhận peak heap thực tế trong chương EPUB có CSS lồng sâu, font SD và 64 clipping dài cùng lúc.
- Chưa xác nhận độ rõ của gạch chân qua các chế độ refresh và panel X3/X4/Sticky.
- Tái neo hiện chỉ tìm quote trong cùng spine và cửa sổ tối đa 4 trang; không đoán ở trang xa để tránh highlight nhầm đoạn trùng.
- Selection hiện nằm trong 1 trang. Clipping nhiều trang, danh sách đã lưu, export và jump/reanchor toàn chương cần một giai đoạn riêng cùng fixture và kiểm thử phần cứng.
