# MobileGlues Shader-Cache Reference

Nguồn gốc: `MobileGlues-cpp/gl/glsl/cache.{h,cpp}` (LGPL-2.1, MobileGL-Dev).
**Tham chiếu — không được include vào build LTW.**

## Cơ chế

MobileGlues chuyển desktop GLSL → GLSL ES bằng glslang → SPIR-V → SPIRV-Cross.
Quá trình này tốn hàng trăm ms mỗi shader. Minecraft + shaderpack có hàng trăm
shader → thời gian khởi động. MobileGlues cache kết quả convert:

- Key: **SHA-256 của source gốc** (streamed, chỉ materialize block cuối).
- Value: chuỗi ESSL đã convert.
- LRU list + map chỉ vào list (evict rẻ, không rehash key).
- Persist xuống đĩa (JSON) theo chu kỳ, không phải mỗi insert.
- Entries không biết được vẫn được giữ nguyên khi ghi lại (forward-compat).

## Cách áp dụng vào LTW

`shader_wrapper.c:glShaderSource()` gọi `optimize_shader()` (Mesa IR
optimizer) mỗi lần. Vì Minecraft gửi lại cùng một source cho cùng shader khi
khởi động lại, cache giúp bỏ hẳn giai đoạn tốn nhất:

1. Thêm `shader_cache.c` nhỏ: hash source (FNV-1a 64-bit là đủ cho in-memory;
   SHA chỉ khi persist để tránh collision trên đĩa).
2. Map `hash → converted string` bằng `unordered_map` có sẵn của LTW.
3. Cache trong RAM trước — persist đĩa là bước 2 (tùy chọn, vì iOS sandbox
   thường chỉ cho ghi vào Documents/).

Gắn vào: `glShaderSource()` sau khi ghép chuỗi, tra cache trước
`optimize_shader()`; `glDeleteShader()` KHÔNG xóa entry (source có thể tái
hiện). Giới hạn kích thước bằng LRU đơn giản (list + map như MobileGlues).
