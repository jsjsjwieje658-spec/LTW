# MobileGlues Pixel-Transfer Reference (`mg_upload_fix_t`)

Nguồn gốc: `MobileGlues-cpp/gl/transfer.cpp` + `gl/pixel.cpp` (LGPL-2.1,
MobileGL-Dev). **Tham chiếu — không được include vào build LTW.**

## Cơ chế

Desktop GL chấp nhận client formats mà GLES không có (`GL_BGRA`,
`GL_UNSIGNED_INT_8_8_8_8(_REV)`...). Driver desktop tự sắp xếp byte trong khi
transfer. GLES không có các format đảo ngược, nên MobileGlues làm điều đó ở
biên transfer của wrapper:

```
mg_upload_fix_t fix(width, height, depth, format, type, pixels, want_format, three_d);
// dùng: fix.format, fix.type, fix.pixels, fix.dropped(), fix.has_data()
```

Điểm đáng học cho LTW:

1. **Không bao giờ query driver 6 lần mỗi upload.** Bảng unpack-state
   (alignment, row_length, skip_*) được mirror trong wrapper
   (`current_unpack_state()` đọc từ state wrapper theo dõi, không gọi
   `glGetIntegerv`).
2. **Tôn trọng unpack PBO**: nếu PBO bound, `pixels` là offset — map buffer
   `GL_MAP_READ_BIT`, convert, rồi unmap. Nếu driver từ chối map READ trên
   buffer usage `_DRAW` (phổ biến trên mobile), fallback qua scratch buffer
   `glCopyBufferSubData` sang buffer `GL_STREAM_READ`.
3. **Stream sau convert là tightly-packed** — zero các unpack params cho lời
   gọi driver, destructor khôi phục mọi state đã đụng đến, kể cả PBO binding.
4. **Kiểm tra tràn bộ nhớ trước khi nhân kích thước** (`mg_checked_area`) —
   desktop GL trả `GL_INVALID_VALUE` cho size quá lớn mà không đụng client
   memory.
5. **Bảng lookup tách bạch**: `gl_sizeof` keyed theo TYPE, `pixel_sizeof`
   keyed theo FORMAT — hai bảng không trộn lẫn nhau.

## Cách áp dụng vào LTW (khi cần)

LTW hiện xử lý BGRA bằng texture-swizzle (`swizzle.c`) — rẻ (không copy
byte) nhưng chỉ đúng cho sampling; sai khi app render vào texture hoặc trộn
format upload trên cùng texture. Nếu một ngày cần đúng chuẩn:

- Thêm bảng `upload_rule_t` (format_in, type_in, channels, src_size) như
  `find_upload_rule()`.
- Convert trong `swizzle_process_upload()` trước khi gọi
  `es3_functions.glTexImage2D`, dùng scratch buffer cấp phát một lần.
- Giữ fast-path swizzle cho các trường hợp phổ biến (Minecraft BGRA atlas)
  và chỉ rơi vào convert path khi thật sự cần.

Lưu ý WebGL/Metal: ANGLE trên iOS đã hỗ trợ `GL_APPLE_texture_format_BGRA8888`
nên phần lớn upload BGRA đi thẳng. Chỉ convert khi driver trả
`GL_INVALID_ENUM`/`GL_INVALID_OPERATION`.
