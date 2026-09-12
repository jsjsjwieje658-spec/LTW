# Reference Directory (NOT BUILT)

This directory contains **reference material only** extracted from
[MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) (LGPL-2.1).

## Quy tắc quan trọng

**NOTHING in this directory is compiled into libLTW.dylib.**

- `ltw/CMakeLists.txt` và `ltw/src/main/tinywrapper/Android.mk` liệt kê nguồn
  một cách tường minh (không glob), nên việc thêm file vào đây KHÔNG bao giờ
  ảnh hưởng đến build.
- Mục đích: tham chiếu kiến trúc cho các cải tiến tương lai. Mỗi file ở đây
  mô tả một cơ chế đã được MobileGlues chứng minh, kèm ghi chú cách port sang
  phong cách C mỏng của LTW.

## Nội dung tham chiếu

| File | Cơ chế MobileGlues | Ứng dụng cho LTW |
|------|--------------------|-------------------|
| `pixel-transfer.md` | `mg_upload_fix_t`: chuyển đổi pixel tại biên transfer, tôn trọng PBO + unpack state, zero-query | Sửa `swizzle.c`/`glformats.c` khi cần hỗ trợ format lạ mà không thêm glGetIntegerv |
| `shader-cache.md` | Cache GLSL→ESSL theo SHA-256, persist xuống đĩa | Giảm thời gian khởi động: `optimize_shader()` hiện chạy mỗi lần `glShaderSource` |
| `multidraw-compute.md` | Gộp multi-draw bằng compute shader + prefix-sum, không CPU readback | Nâng cấp `multidraw.c` (hiện copy index qua glBufferSubData từng draw) |
| `redundant-bind-skip.md` | Bỏ qua bind trùng lặp bằng shadow state | Đã áp dụng một phần trong `swizzle.c` (binding shadow) |
| `fsr1-notes.md` | FSR1 upscale qua FBO trước khi swap | Hỗ trợ render độ phân giải thấp → upscale, tăng FPS trên máy yếu |

Xem từng file để biết chi tiết và điều kiện áp dụng.
