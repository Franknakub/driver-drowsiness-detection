"""Check that eye_preprocess.h (C) and preprocess.py (numpy) give identical bytes.

Compiles a tiny harness with gcc, feeds it random RGB565 crops of many sizes,
and compares gray, resized and int8 outputs. Run: python test_preprocess.py
"""
import pathlib
import shutil
import subprocess
import sys
import tempfile

import numpy as np

import preprocess as pp

HERE = pathlib.Path(__file__).resolve().parent

HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "eye_preprocess.h"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
// stdin: w h n, then w*h uint16 RGB565 (host order). stdout: gray(w*h) resized(n*n) int8(n*n)
int main(void) {
  int w, h, n;
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  if (fread(&w, 4, 1, stdin) != 1 || fread(&h, 4, 1, stdin) != 1 || fread(&n, 4, 1, stdin) != 1) return 1;
  uint16_t *px = malloc(w * h * 2);
  uint8_t *gray = malloc(w * h), *small = malloc(n * n);
  int8_t *q = malloc(n * n);
  if (fread(px, 2, w * h, stdin) != (size_t)(w * h)) return 2;
  for (int i = 0; i < w * h; i++) gray[i] = eye_rgb565_to_gray(px[i]);
  eye_resize_bilinear(gray, w, h, w, small, n, n);
  eye_to_int8(small, q, n * n);
  fwrite(gray, 1, w * h, stdout);
  fwrite(small, 1, n * n, stdout);
  fwrite(q, 1, n * n, stdout);
  return 0;
}
"""


def main():
    gcc = shutil.which("gcc")
    if gcc is None:
        sys.exit("gcc not found")
    rng = np.random.default_rng(0)
    with tempfile.TemporaryDirectory() as tmp:
        src = pathlib.Path(tmp) / "harness.c"
        exe = pathlib.Path(tmp) / "harness.exe"
        src.write_text(HARNESS)
        subprocess.run([gcc, "-O2", "-Wall", "-I", str(HERE), str(src), "-o", str(exe)], check=True)
        cases = 0
        for n in (32, 48, 64):
            for _ in range(100):
                w, h = (int(v) for v in rng.integers(8, 200, size=2))
                px = rng.integers(0, 65536, size=(h, w), dtype=np.uint16)
                if rng.random() < 0.3:  # smooth image, closer to a real eye crop
                    px = np.full((h, w), rng.integers(0, 65536), dtype=np.uint16)
                    px[h // 3: 2 * h // 3, w // 4: 3 * w // 4] = rng.integers(0, 65536)
                inp = np.array([w, h, n], dtype=np.int32).tobytes() + px.astype("<u2").tobytes()
                out = subprocess.run([str(exe)], input=inp, capture_output=True, check=True).stdout
                gray_c = np.frombuffer(out[: w * h], np.uint8).reshape(h, w)
                small_c = np.frombuffer(out[w * h: w * h + n * n], np.uint8).reshape(n, n)
                q_c = np.frombuffer(out[w * h + n * n:], np.int8).reshape(n, n)
                gray_py = pp.rgb565_to_gray(px)
                small_py = pp.resize_bilinear(gray_py, n)
                q_py = pp.to_int8(small_py)
                assert np.array_equal(gray_c, gray_py), (w, h, n, "gray")
                assert np.array_equal(small_c, small_py), (w, h, n, "resize")
                assert np.array_equal(q_c, q_py), (w, h, n, "int8")
                assert np.array_equal(q_py, pp.preprocess_rgb565(px, n))
                cases += 1
    print(f"OK: {cases} random crops (8..199 px -> 32/48/64) bit-exact between C and Python")


if __name__ == "__main__":
    main()
