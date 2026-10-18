"""Eye Region preprocessing shared by training and the ESP32 firmware.

Every step uses integer math only, so `eye_preprocess.h` reproduces it bit for bit:

    RGB565 crop --rgb565_to_gray--> uint8 gray (any size)
                --resize_bilinear--> uint8 gray N x N
                --to_int8-------->  int8 model input (gray - 128)

The int8 step is the full-integer quantization of x = gray / 255 with
scale = 1/255 and zero_point = -128 (train.ipynb asserts the exported model
has exactly these parameters).
"""
import numpy as np

FRAC_BITS = 8          # fixed-point fraction bits for bilinear weights
ONE = 1 << FRAC_BITS   # 256


def rgb565_to_gray(px):
    """uint16 RGB565 pixels -> uint8 gray (BT.601 weights 77/150/29 over 256).

    `px` must already be in host order, i.e. (hi_byte << 8) | lo_byte.
    esp32-camera frame buffers store the high byte first.
    """
    px = np.asarray(px, dtype=np.uint32)
    r5 = (px >> 11) & 0x1F
    g6 = (px >> 5) & 0x3F
    b5 = px & 0x1F
    r = (r5 << 3) | (r5 >> 2)
    g = (g6 << 2) | (g6 >> 4)
    b = (b5 << 3) | (b5 >> 2)
    return ((77 * r + 150 * g + 29 * b + 128) >> 8).astype(np.uint8)


def _src_coords(dst, src):
    """Fixed-point source coordinate for each destination pixel (half-pixel centers).

    s = (d + 0.5) * src / dst - 0.5, in 1/256 px, clamped to [0, (src-1)*256].
    Returns (i0, i1, frac) with i1 = min(i0 + 1, src - 1).
    """
    d = np.arange(dst, dtype=np.int64)
    s = ((2 * d + 1) * src * ONE) // (2 * dst) - ONE // 2
    s = np.clip(s, 0, (src - 1) * ONE)
    i0 = s >> FRAC_BITS
    frac = s & (ONE - 1)
    i1 = np.minimum(i0 + 1, src - 1)
    return i0, i1, frac


def resize_bilinear(gray, out_w, out_h=None):
    """uint8 HxW -> uint8 out_h x out_w, integer bilinear identical to eye_preprocess.h."""
    out_h = out_w if out_h is None else out_h
    g = np.asarray(gray, dtype=np.int64)
    h, w = g.shape
    x0, x1, fx = _src_coords(out_w, w)
    y0, y1, fy = _src_coords(out_h, h)
    fx = fx[None, :]
    fy = fy[:, None]
    p00 = g[y0][:, x0]
    p01 = g[y0][:, x1]
    p10 = g[y1][:, x0]
    p11 = g[y1][:, x1]
    acc = (p00 * (ONE - fx) * (ONE - fy) + p01 * fx * (ONE - fy)
           + p10 * (ONE - fx) * fy + p11 * fx * fy)
    return ((acc + (1 << (2 * FRAC_BITS - 1))) >> (2 * FRAC_BITS)).astype(np.uint8)


def to_int8(gray):
    """uint8 gray -> int8 model input (gray - 128)."""
    return (np.asarray(gray, dtype=np.int16) - 128).astype(np.int8)


def to_float(gray):
    """uint8 gray -> float32 in [0, 1], the training-time view of the same input."""
    return np.asarray(gray, dtype=np.float32) / 255.0


def preprocess_gray(gray, size):
    """Gray crop of any size -> int8 size x size model input."""
    return to_int8(resize_bilinear(gray, size))


def preprocess_rgb565(px, size):
    """RGB565 crop (HxW uint16, host order) -> int8 size x size model input."""
    return preprocess_gray(rgb565_to_gray(px), size)
