# MobileGlues Redundant-Bind-Skip Reference

Nguồn gốc: `MobileGlues-cpp/gl/texture.cpp` (`glBindTexture`) (LGPL-2.1,
MobileGL-Dev). **Tham chiếu — không được include vào build LTW.**

## Cơ chế

Re-binding cùng texture là lệnh gọi dư thừa phổ biến nhất Minecraft tạo ra.
MobileGlues so sánh binding hiện tại với shadow-state của wrapper và bỏ lệnh
driver tương ứng — nhưng vẫn cập nhật state wrapper (record có thể bị hỏng cần
sửa lại bằng chính lệnh bind đó).

Nhánh kiểm tra của họ tách làm hai nửa (frontend record + driver slot) vì có
emulation buffer-texture viết vào slot driver mà app không hề nêu tên.

## Cách áp dụng vào LTW (đã áp dụng một phần)

`swizzle.c` của LTW giờ đã có binding shadow (`swizzle_shadow_bind_texture`).
Bước kế tiếp nếu đo lường cho thấy bind-trùng là điểm nóng:

```c
void glBindTexture(GLenum target, GLuint texture) {
    ...
    if (ctx->shadow_tmu_bindings[tmu][slot] == texture
        && driver_trustworthy) return;      // bỏ hẳn lệnh driver
    es3_functions.glBindTexture(target, texture);
    swizzle_shadow_bind_texture(target, texture);
}
```

CẢNH BÁO từ chính MobileGlues: phải chắc chắn KHÔNG code path nào khác của
wrapper đổi binding driver mà không qua record (họ dùng
`driver_texture_shadow_trustworthy()`). LTW an toàn hơn ở chỗ các hàm khác
(of_buffer_copier, basevertex) luôn save/restore qua glGetIntegerv trực tiếp —
nhưng nếu skip-bind được bật, mọi chỗ đó phải được audit lại trước.

Vì rủi ro này, LTW hiện mới dùng shadow để *đọc* (tránh glGetIntegerv trong
upload path) — an toàn tuyệt đối vì một shadow stale chỉ gây một lần fallback
query, không sai render.
