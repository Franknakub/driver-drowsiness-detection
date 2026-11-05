// Eye Region preprocessing for the eye-state model, bit-exact with ml/preprocess.py.
//
//   RGB565 crop -> eye_rgb565_to_gray -> uint8 gray (any size)
//               -> eye_resize_bilinear -> uint8 gray N x N
//               -> eye_to_int8         -> int8 model input (gray - 128)
//
// Integer math only. Verified against preprocess.py by ml/test_preprocess.py.
#pragma once
#include <stdint.h>

#define EYE_FRAC_BITS 8
#define EYE_ONE (1 << EYE_FRAC_BITS)

// RGB565 pixel in host order ((hi << 8) | lo) -> gray, BT.601 weights 77/150/29.
// esp32-camera frame buffers store the high byte first.
static inline uint8_t eye_rgb565_to_gray(uint16_t px) {
  uint32_t r5 = (px >> 11) & 0x1F, g6 = (px >> 5) & 0x3F, b5 = px & 0x1F;
  uint32_t r = (r5 << 3) | (r5 >> 2);
  uint32_t g = (g6 << 2) | (g6 >> 4);
  uint32_t b = (b5 << 3) | (b5 >> 2);
  return (uint8_t)((77 * r + 150 * g + 29 * b + 128) >> 8);
}

// Fixed-point source coordinate of destination pixel d (half-pixel centers),
// in 1/256 px, clamped to [0, (src-1)*256].
static inline int32_t eye_src_coord(int32_t d, int32_t dst, int32_t src) {
  int32_t s = ((2 * d + 1) * src * EYE_ONE) / (2 * dst) - EYE_ONE / 2;
  if (s < 0) s = 0;
  if (s > (src - 1) * EYE_ONE) s = (src - 1) * EYE_ONE;
  return s;
}

// Bilinear resize of a gray image. `src_stride` = bytes per source row,
// so a crop can be read straight out of a larger frame.
static inline void eye_resize_bilinear(const uint8_t *src, int src_w, int src_h, int src_stride,
                                       uint8_t *dst, int dst_w, int dst_h) {
  for (int y = 0; y < dst_h; y++) {
    int32_t sy = eye_src_coord(y, dst_h, src_h);
    int32_t y0 = sy >> EYE_FRAC_BITS, fy = sy & (EYE_ONE - 1);
    int32_t y1 = (y0 + 1 < src_h) ? y0 + 1 : src_h - 1;
    const uint8_t *r0 = src + y0 * src_stride, *r1 = src + y1 * src_stride;
    for (int x = 0; x < dst_w; x++) {
      int32_t sx = eye_src_coord(x, dst_w, src_w);
      int32_t x0 = sx >> EYE_FRAC_BITS, fx = sx & (EYE_ONE - 1);
      int32_t x1 = (x0 + 1 < src_w) ? x0 + 1 : src_w - 1;
      int32_t acc = r0[x0] * (EYE_ONE - fx) * (EYE_ONE - fy) + r0[x1] * fx * (EYE_ONE - fy) +
                    r1[x0] * (EYE_ONE - fx) * fy + r1[x1] * fx * fy;
      dst[y * dst_w + x] = (uint8_t)((acc + (1 << (2 * EYE_FRAC_BITS - 1))) >> (2 * EYE_FRAC_BITS));
    }
  }
}

// gray -> int8 model input. Valid because the model's input tensor has
// scale = 1/255, zero_point = -128 (asserted in train.ipynb).
static inline void eye_to_int8(const uint8_t *gray, int8_t *out, int n) {
  for (int i = 0; i < n; i++) out[i] = (int8_t)((int)gray[i] - 128);
}
