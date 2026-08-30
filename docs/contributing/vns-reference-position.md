# Vị trí tham chiếu Văn Nhân Số phiên bản 1

## Mục đích

Vị trí VNS cung cấp một mã tham chiếu ổn định hơn số trang dàn trang. Nó không thay thế progress hiện có trong `vns.7`; trước mắt mã được dùng ở status bar và QR để có thể đối chiếu cùng một nội dung sau khi đổi font, margin hoặc hướng màn hình.

## Chữ ký nội dung

Firmware tính FNV-1a 32 bit, bắt đầu từ `2166136261`, lần lượt trên:

1. Tiêu đề và tác giả.
2. Kích thước EPUB.
3. Với từng spine item: `href` và kích thước tích lũy đến hết item đó.

Đây là chữ ký phát hiện khác nội dung, không phải hàm băm mật mã. Xác suất va chạm không bằng 0; consumer cần kiểm tra thêm metadata sách nếu dùng vị trí cho đồng bộ có hậu quả ghi dữ liệu.

## Schema

Token phiên bản 1 có dạng:

```text
vnspos:1:<content-signature-hex>:<spine-index>:<visible-text-offset>:<ordinal>
```

- `spine-index`: chỉ số spine 0-based, giới hạn `uint16_t`.
- `visible-text-offset`: số codepoint hiển thị được tính từ đầu spine, không phụ thuộc phân trang.
- `ordinal`: `floor((cumulative-source-bytes-before-spine + visible-text-offset) / 1024) + 1`.

Ví dụ minh họa, không phải dữ liệu của một sách thật:

```text
vnspos:1:12ab34cd:4:1536:87
```

Decoder chỉ nhận đúng version 1, đủ 5 trường, không có ký tự thừa, ordinal khác 0 và mọi giá trị nằm trong miền kiểu dữ liệu. Token sai bị bỏ qua; firmware tiếp tục đọc sách bằng progress hiện có.

## Quan hệ với số trang nhà xuất bản

Nhãn trang nhà xuất bản là lớp độc lập. Parser nhận Navigation Document `page-list` cùng các token `epub:type="pagebreak"` hoặc `role="doc-pagebreak"`, lưu sidecar có version rồi nạp tối đa 64 marker cho mỗi spine. Nhãn từ `page-list` được ưu tiên; marker trong nội dung lần lượt dùng `aria-label`, `title`, rồi `id`. Nhãn được giới hạn ở 31 byte cộng NUL và cắt đúng biên UTF-8. Nhãn trang chỉ có ý nghĩa khi EPUB cung cấp metadata; vị trí VNS vẫn tồn tại khi sách không có marker.

## Consumer trong `vns.7`

- Status bar: hiển thị ordinal VNS và nhãn trang nhà xuất bản nếu có.
- QR: thêm token VNS vào payload cùng vị trí hiện tại; khi payload dài, phần mô tả được cắt đúng biên UTF-8 nhưng luôn dành chỗ cho token đầy đủ.
- Clipping: vẫn lưu `spineIndex` và `visibleTextOffset`, là 2 anchor đầu vào của resolver; format clipping không bị bump lần nữa trong bản này.
- KOReader sync: giữ nguyên protocol hiện hành. `vns.7` không nhét token riêng vào trường KOReader khi chưa có đặc tả tương thích hai phía.

Mở rộng đồng bộ backend hoặc metadata sách VNS phải tăng schema khi đổi ngữ nghĩa, có test token cũ và không được suy ra “cùng sách” chỉ từ ordinal.
