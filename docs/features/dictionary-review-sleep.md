# Thiết kế màn hình ngủ ôn từ đã tra

Trạng thái: triển khai trong `1.6.0-vns.7`

## Mục tiêu và ranh giới dữ liệu

Người dùng có thể chọn **Ôn từ đã tra** trong danh sách màn hình ngủ. Mỗi lần máy đi vào deep sleep, firmware chọn 1 từ trong lịch sử. Một thẻ chỉ bắt buộc có từ và phần giải nghĩa; phiên âm, ví dụ và collocation là thông tin bổ sung khi nguồn từ điển thực sự có dữ liệu.

Lịch sử hiện hành tại `/.crosspoint/vns_dictionary_history.txt` chỉ lưu tối đa 15 truy vấn đã tra thành công, không lưu bản sao toàn bộ định nghĩa. Vì vậy `vns.7` tra lại từ bằng bộ từ điển StarDict đang được chọn trên thẻ SD. “Ngoại tuyến” trong tài liệu này nghĩa là không bật Wi-Fi và không gọi dịch vụ mạng; nó không có nghĩa là định nghĩa đã được chép vào file lịch sử.

## Luồng xử lý

1. Chế độ chỉ chạy khi người dùng chủ động chọn **Ôn từ đã tra**. Nó không thay đổi lựa chọn màn ngủ hiện tại khi nâng cấp firmware.
2. Firmware kiểm tra lịch sử và từ điển đã chọn. Thiếu 1 trong 2 điều kiện thì dùng ngay màn ngủ mặc định. Để có nghĩa tiếng Việt, người dùng cần chọn bộ từ điển Anh–Việt/Việt phù hợp; firmware không tự dịch hoặc đoán ngôn ngữ của định nghĩa.
3. Cache font SD được giải phóng trước khi mở từ điển để giảm phân mảnh heap trên ESP32-C3.
4. Firmware không dựng hoặc cập nhật `.qidx/.sidx` lúc ngủ. Nếu index thiếu hoặc cũ, màn mặc định được dùng; lần tra từ chủ động tiếp theo sẽ xử lý index như trước.
5. Chọn một vị trí bắt đầu ngẫu nhiên và thử tối đa 4 mục. Hash của từ vừa hiển thị được lưu trong `state.json`; khi lịch sử có hơn 1 mục, lần sau tránh lặp ngay từ đó.
6. Định nghĩa HTML được chuyển thành text. Bộ tách có giới hạn lấy từ, phiên âm/IPA, nghĩa, ví dụ và collocation dựa trên nhãn ở đầu trường dữ liệu. Thiếu từ hoặc giải nghĩa thì thử mục lịch sử tiếp theo; phiên âm, ví dụ và collocation không có căn cứ được bỏ hẳn, không tạo nội dung giả.
7. Khi có thẻ hợp lệ, firmware dựng 1 framebuffer và phát `HALF_REFRESH`, sau đó lưu hash của từ. Mọi lỗi mở/đọc/giải nén, thiếu trường bắt buộc hoặc hết số lần thử đều rơi về màn mặc định.

## Bố cục

- Tiêu đề nhỏ **ÔN TỪ ĐÃ TRA** ở đầu trang.
- Từ chính dùng Source Sans 3 18 đậm, căn giữa và tối đa 2 dòng; token quá dài được cắt tại biên UTF-8 với dấu `…`.
- Phiên âm, nghĩa, ví dụ và collocation dùng Source Sans 3 14 tích hợp để giữ coverage IPA ổn định; phiên âm tối đa 2 dòng và không chiếm vùng khi không có.
- Các mục có nội dung chia sẻ động toàn bộ chiều cao còn lại. Mỗi mục phía sau được giữ tối thiểu nhãn và 1 dòng nội dung; mục cuối cùng dùng hết số dòng còn lại. Mục không có dữ liệu không được giữ chỗ.
- Text được wrap theo biên UTF-8 và chiều rộng thật của font. Nội dung vượt giới hạn kết thúc bằng dấu `…`.
- Nghĩa vẫn có ưu tiên cao nhất nhưng không còn bị cắt ở giới hạn 10 dòng khi cuối màn hình vẫn còn chỗ.

Lookup dành cho màn ngủ chỉ đọc tối đa 8 KiB định nghĩa. Chuyển HTML sang text chỉ chạy khi heap vượt ngưỡng an toàn; nếu không, firmware phân tích trực tiếp nội dung đã đọc. Kết quả được chặn ở 96 byte cho từ, 160 byte cho phiên âm, 900 byte cho nghĩa, 420 byte cho ví dụ và 360 byte cho collocation. Đây là giới hạn byte UTF-8; hàm cắt lùi về biên codepoint để không tạo chuỗi lỗi.

## Quyết định chống gây phiền

- Không bật mạng, không hiển thị tiến trình và không chặn người dùng bằng hộp thoại khi đi ngủ.
- Không tự chọn chế độ này sau nâng cấp.
- Không biến lỗi từ điển hoặc thẻ thiếu từ/giải nghĩa thành màn báo lỗi tồn tại suốt thời gian ngủ; luôn có màn mặc định an toàn. Thiếu phiên âm, ví dụ hoặc collocation không làm mất thẻ.
- Không lặp ngay từ vừa hiện khi còn lựa chọn khác.
- Giới hạn 4 lần lookup để thời gian vào ngủ không tăng không kiểm soát vì các mục lỗi hoặc thiếu trường bắt buộc.
- Xóa lịch sử tra từ làm chế độ này tự rơi về màn mặc định; không giữ một danh sách học ẩn khác với dữ liệu người dùng nhìn thấy.

## Đánh đổi và hướng sau `vns.7`

Tra lại từ điển tránh tạo thêm format lưu trữ lớn và bảo đảm nội dung theo đúng bộ từ điển hiện được chọn, nhưng phụ thuộc vào file từ điển và index còn tồn tại trên SD. Nếu số đo thiết bị cho thấy lookup lúc ngủ chậm hoặc tạo áp lực heap, phương án tiếp theo là lưu snapshot thẻ ôn tập có version/CRC ngay khi tra thành công. Không đưa snapshot vào `vns.7` khi chưa có số đo vì nó tạo thêm đồng bộ xóa/sửa, migration và 15 bản sao nội dung mà lịch sử hiện tại chưa cam kết lưu.

## Kiểm chứng

Host test khóa trường tối thiểu từ + giải nghĩa, các trường tùy chọn, nhãn ở đầu dòng và separator NUL của StarDict. Build đa thiết bị kiểm tra tích hợp firmware. Thời gian lookup, waveform, độ rõ chữ và heap khi dùng `.dict.dz` vẫn cần smoke test trên X3/X4 Pro thật; build hoặc simulator không thay cho xác nhận này.
