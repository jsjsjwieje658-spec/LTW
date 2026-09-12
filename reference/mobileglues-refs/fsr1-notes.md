# MobileGlues FSR1 Upscale Reference

Nguồn gốc: `MobileGlues-cpp/gl/FSR1/FSR1.{h,cpp}` + `FSRShaderSource.h`
(LGPL-2.1, MobileGL-Dev, FSR1 của AMD). **Tham chiếu — không include vào
build LTW.**

## Cơ chế

Render ở độ phân giải thấp → upscale bằng AMD FSR1 (ESRDukan) lên full
resolution trước khi present:

- Framebuffer 0 bị redirect (framebuffer.cpp) vào `g_renderFBO` (kích thước
  render). Viewport bị scale lại theo tỉ lệ.
- Trước `eglSwapBuffers`: full-screen pass `g_fsrProgram` lấy
  `g_renderTexture` → vẽ vào `g_targetTexture` (kích thước surface thật).
- Depth-stencil: RBO riêng ở render resolution.
- Resolution change được phát hiện sau swap (surface có thể resize).
- Damage-rect swap bị DROP khi FSR on — upscale viết toàn bộ surface nên
  rectangles không còn mô tả vùng thay đổi.
- Mọi tên GL object (FBO/texture/program) thuộc về context tạo nó — cache
  theo ctx_id và swap khi current context đổi.

## Cách áp dụng vào LTW

LTW là thin-wrapper — KHÔNG theo dõi state render, không intercept
glViewport/swap, nên redirect FBO mặc định không port được nguyên trạng. Nhưng
vì đã có `ltw_swap.c` (của cải tiến này) hook present path, một "post-process
pass" kiểu này nằm ngoài phạm vi thin-wrapper; cách làm đúng với triết lý LTW
là để launcher/Java side bật FSR qua Metal layer hoặc chờ app tự chọn render
scale.

**Kết luận tham chiếu:** chỉ đáng port khi LTW có intention layer riêng. Ghi
nhận ở đây cho đầy đủ vì đây là cấu trúc tăng-GPU-throughput hiệu quả nhất
MobileGlues có, và là pattern để đánh giá nếu ngày nào LTW cần precent-hook
nặng hơn.
