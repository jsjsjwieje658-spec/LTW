# MobileGlues Multidraw-Compute Reference

Nguồn gốc: `MobileGlues-cpp/gl/multidraw.cpp` + `shaders/multidraw_compute.comp`
(LGPL-2.1, MobileGL-Dev). **Tham chiếu — không được include vào build LTW.**

## Cơ chế

`glMultiDrawElements(BaseVertex)` phải gộp N draw-call thành 1. Cách làm của
MobileGlues (backend "compute", chọn tự động theo khả năng driver):

1. Chuẩn bị: mảng firstIndex/baseVertex ghép thành `ivec2` (giữ đúng 4 SSBO —
   mức tối thiểu GLES 3.1 đảm bảo), prefix-sum số index mỗi draw.
2. Compute shader (local_size_x=64) đọc index buffer nguồn (1/2/4 bytes —
   tự decode theo `uElementSize`), ghi ra buffer đích SẮP XẾP LIỀN.
3. GPU tự compaction số lệnh theo drawcount (glMultiDrawIndirectCount với
   count buffer đọc TRỰC TIẾP trên GPU — không CPU readback, không stall).
4. Một `glDrawElementsIndirect` duy nhất tiêu thụ buffer đã gộp.

Điểm mấu chốt: **không có vòng lặp glBufferSubData trên CPU** như LTW đang làm
trong `multidraw.c` (copy từng draw vào buffer tạm). Mọi merge diễn ra
trên GPU, pipeline không bao giờ phải chờ CPU.

## Cách áp dụng vào LTW

LTW hiện (multidraw.c): cấp buffer GL_COPY_WRITE, copy từng draw-call bằng
`glCopyBufferSubData` — N draw = N lệnh copy + 1 draw. Với chunk render của
Minecraft (hàng chục draw một frame), chi phí copy từng cái cộng dồn.

Nâng cấp theo MobileGlues khi thiết bị có ES 3.1:

- Dùng prefix-sum trên CPU (drawcount nhỏ, rẻ) cho firstIndex array, upload
  MỘT lần; compute shader gộp index buffer; kết thúc bằng một indirect draw.
- Cần: `GL_ES_VERSION_3_1` (glDispatchCompute), 4 SSBO. Kiểm
  `current_context->es31` như basevertex.c đã làm.
- Threshold: chỉ dùng compute khi `primcount > 8` — dưới ngưỡng đó vòng lặp
  glDrawElements thường thắng vì setup compute shader có chi phí cố định.
- Bảng `mg_index_size()` / decode theo elementSize của shader MobileGlues là
  mẫu tham chiếu trực tiếp cho việc đọc index 1-byte trong SSBO.
